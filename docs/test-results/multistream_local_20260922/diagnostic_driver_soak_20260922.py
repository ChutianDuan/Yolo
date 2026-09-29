#!/usr/bin/env python3
"""Bounded local six-stream soak with durable samples; not a real-camera RTSP test."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import secrets
import socket
import subprocess
import sys
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from datetime import datetime, timezone

import cv2


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--service-binary", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--single-model", action="store_true")
    parser.add_argument("--count", type=int, choices=(6,), default=6)
    parser.add_argument("--high-requests", type=int, choices=(1, 2, 4), default=1)
    parser.add_argument("--low-requests", type=int, choices=(1, 2, 4), default=1)
    parser.add_argument("--opencv-threads", type=int, choices=(1, 2, 4, 8), default=4)
    parser.add_argument("--high-threads", type=int, choices=(4, 8, 16), default=8)
    parser.add_argument("--low-threads", type=int, choices=(4, 8, 16), default=8)
    parser.add_argument("--cpu-pinning", choices=("inherit", "true", "false"), default="inherit")
    parser.add_argument("--seconds", type=int, choices=(60, 28800), default=60)
    parser.add_argument("--sample-interval-seconds", type=int, choices=(1, 5), default=5)
    parser.add_argument("--warmup-seconds", type=int, choices=(0, 10, 60), default=60)
    parser.add_argument("--low-detect-fps", type=int, choices=(4, 5), default=4)
    parser.add_argument("--mode", choices=("throughput", "latency"), default="throughput")
    args = parser.parse_args()
    assert args.count == 6, "this soak profile requires six streams"
    repo = args.repo.resolve(strict=True)
    work = args.work_dir.resolve(strict=True)
    binary = args.service_binary.resolve(strict=True)
    assert binary.is_file(), "service binary is not a file"
    assert binary == (repo / "build-openvino/yolo_api").resolve() or binary.parent == work, \
        "binary must be the existing service or a reference in the owned work directory"
    binary_sha256 = hashlib.sha256(binary.read_bytes()).hexdigest()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    output_dir = args.output_dir.resolve()
    journal = (output_dir / "live_samples.jsonl").open("x", encoding="utf-8")
    sys.path.insert(0, str(repo / "yolo_onnx_cpp/tools"))
    from multistream_acceptance import request_json

    cv2.setNumThreads(1)
    video_root = repo / "datasets/bdd100k_tracking_video/bdd100k_videos_train_00/bdd100k/videos/train"
    # Six/eight independent connections deliberately reuse the four baseline contents.
    baseline_names = ("0000f77c-6257be58", "00268999-cb063914",
                      "012fdff1-9d1d0d1d", "0000f77c-cb820c98")
    names = (baseline_names * 2)[:args.count]
    cached: dict[str, list[bytes]] = {}
    metadata = []
    for index, name in enumerate(names, 1):
        path = video_root / (name + ".mov")
        capture = cv2.VideoCapture(str(path))
        assert capture.isOpened(), "local input cannot be opened"
        assert capture.get(cv2.CAP_PROP_FRAME_WIDTH) == 1280
        assert capture.get(cv2.CAP_PROP_FRAME_HEIGHT) == 720
        frames = []
        for _ in range(180):
            ok, frame = capture.read()
            assert ok, "local input is too short"
            ok, encoded = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 80])
            assert ok
            frames.append(encoded.tobytes())
        digest = hashlib.sha256()
        with path.open("rb") as video_file:
            for chunk in iter(lambda: video_file.read(1024 * 1024), b""):
                digest.update(chunk)
        metadata.append({"basename": path.name, "sha256": digest.hexdigest(),
            "original_fps": capture.get(cv2.CAP_PROP_FPS),
            "width": 1280, "height": 720, "cached_frames": len(frames),
            "jpeg_quality": 80, "send_fps": 30})
        capture.release()
        cached[f"/camera/{index}.mjpg"] = frames

    stop = threading.Event()
    observations: list[dict] = []
    observation_lock = threading.Lock()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *values: object) -> None:
            pass

        def do_GET(self) -> None:
            frames = cached.get(self.path)
            if frames is None:
                self.send_error(404)
                return
            self.connection.settimeout(2)
            self.send_response(200)
            self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            started = time.monotonic()
            sent = 0
            last_sent = None
            try:
                while not stop.is_set():
                    encoded = frames[sent % len(frames)]
                    self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: "
                                     + str(len(encoded)).encode() + b"\r\n\r\n" + encoded + b"\r\n")
                    self.wfile.flush()
                    sent += 1
                    last_sent = time.monotonic()
                    stop.wait(max(0.0, started + sent / 30.0 - time.monotonic()))
            except (BrokenPipeError, ConnectionResetError, TimeoutError):
                pass
            finally:
                with observation_lock:
                    observations.append({"path": self.path, "sent_frames": sent,
                        "elapsed_seconds": (last_sent - started) if last_sent else 0,
                        "delivered_fps": ((sent - 1) / (last_sent - started))
                            if sent > 1 and last_sent > started else None})

    with socket.socket() as probe:
        # Ignore only stale TIME_WAIT sockets; active listeners still fail this bind.
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        probe.bind(("0.0.0.0", 8080))
    source_server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    source_server.daemon_threads = False
    source_thread = threading.Thread(target=source_server.serve_forever)
    token = secrets.token_urlsafe(32)
    config_text = (repo / "yolo_onnx_cpp/config.multistream.yaml").read_text()
    primary_model = repo / ("yolo_onnx_cpp/deploy/best_640x384.onnx"
                            if args.single_model else "yolo_onnx_cpp/deploy/best.onnx")
    replacements = {
        "model_path: ./deploy/best.onnx": "model_path: " + json.dumps(str(primary_model)),
        "low_res_model_path: ./deploy/best_640x384.onnx": "low_res_model_path: " + ('""' if args.single_model else json.dumps(str(repo / "yolo_onnx_cpp/deploy/best_640x384.onnx"))),
        "opencv_threads: 4": f"opencv_threads: {args.opencv_threads}",
        "high_model_threads: 8": f"high_model_threads: {args.high_threads}",
        "low_model_threads: 8": f"low_model_threads: {args.low_threads}",
        "video_detect_fps: 4.0": f"video_detect_fps: {args.low_detect_fps}.0",
        "input_width: 1280": "input_width: 640" if args.single_model else "input_width: 1280",
        "input_height: 736": "input_height: 384" if args.single_model else "input_height: 736",
        "max_streams: 4": f"max_streams: {args.count}",
        "openvino_performance_mode: throughput": f"openvino_performance_mode: {args.mode}",
        'api_bearer_token_env: ""': "api_bearer_token_env: YOLO_LOCAL_DIAGNOSTIC_TOKEN",
        "infer_request_count: 0": f"infer_request_count: {args.high_requests}",
        "low_res_infer_request_count: 0": f"low_res_infer_request_count: {args.low_requests}",
    }
    for before, after in replacements.items():
        # Match complete lines: the high-model key is also a suffix of the low key.
        assert config_text.splitlines().count(before) == 1, "baseline configuration changed"
        config_text = "\n".join(after if line == before else line
                                for line in config_text.splitlines()) + "\n"
    assert not any(line.startswith("openvino_cpu_pinning:") for line in config_text.splitlines())
    if args.cpu_pinning != "inherit":
        config_text += f"openvino_cpu_pinning: {args.cpu_pinning}\n"
    config = output_dir / "diagnostic-config.yaml"
    config.write_text(config_text, encoding="utf-8")
    environment = os.environ.copy()
    environment["YOLO_LOCAL_DIAGNOSTIC_TOKEN"] = token
    child = None
    collector_returncode = None
    server_log = output_dir / "server.log"
    metrics_stop = threading.Event()
    metrics_samples: list[dict] = []
    metrics_errors: list[str] = []
    metrics_thread = None
    retirement_evidence = {}
    memory_samples: list[dict] = []
    actual_executable_verified = False
    sample_count = 0
    collector = None
    run_error = None
    run_state = {
        "state": "starting", "driver_pid": os.getpid(),
        "driver_start_ticks": int(Path("/proc/self/stat").read_text().rsplit(") ", 1)[1].split()[19]),
        "started_at": datetime.now(timezone.utc).isoformat(),
        "service_binary": str(binary), "service_binary_sha256": binary_sha256,
        "requested_seconds": args.seconds, "warmup_seconds": args.warmup_seconds,
        "sample_interval_seconds": args.sample_interval_seconds,
        "samples_file": "live_samples.jsonl",
    }

    def write_state() -> None:
        temporary = output_dir / "run_state.tmp"
        temporary.write_text(json.dumps(run_state, indent=2) + "\n", encoding="utf-8")
        temporary.replace(output_dir / "run_state.json")

    write_state()

    def sample_metrics() -> None:
        nonlocal sample_count
        assert owns_listener(), "metrics listener is not the owned service"
        request = urllib.request.Request("http://127.0.0.1:8080/metrics",
            headers={"Authorization": "Bearer " + token})
        with urllib.request.urlopen(request, timeout=3) as response:
            payload = response.read(2_000_001)
        assert len(payload) <= 2_000_000, "metrics payload exceeded diagnostic bound"
        metrics_samples.append({"monotonic_seconds": time.monotonic(),
                                "prometheus_text": payload.decode("utf-8")})
        assert child is not None
        fields = {}
        allowed = {"Rss", "Pss", "Private_Clean", "Private_Dirty", "Shared_Clean",
                   "Shared_Dirty", "Anonymous", "AnonHugePages"}
        for line in (Path("/proc") / str(child.pid) / "smaps_rollup").read_text().splitlines():
            key, separator, value = line.partition(":")
            if separator and key in allowed:
                number, unit = value.split()
                assert unit == "kB" and int(number) >= 0
                fields[key + "_kib"] = int(number)
        assert len(fields) == len(allowed), "incomplete owned process memory evidence"
        thread_lines = [line for line in (Path("/proc") / str(child.pid) / "status").read_text().splitlines()
                        if line.startswith("Threads:")]
        assert len(thread_lines) == 1
        fields["threads"] = int(thread_lines[0].split()[1])
        memory_samples.append({"monotonic_seconds": time.monotonic(), **fields})
        sample_count += 1
        journal.write(json.dumps({"index": sample_count, "metrics": metrics_samples[-1],
                                  "memory": memory_samples[-1]}, allow_nan=False) + "\n")
        journal.flush()
        metrics_samples[:] = metrics_samples[-1:]
        memory_samples[:] = memory_samples[-1:]
        temporary = output_dir / "progress.tmp"
        temporary.write_text(json.dumps({
            "sample_count": sample_count, "observed_at": datetime.now(timezone.utc).isoformat(),
            "service_pid": child.pid, "memory": memory_samples[-1],
            "metrics_monotonic_seconds": metrics_samples[-1]["monotonic_seconds"],
        }, indent=2) + "\n", encoding="utf-8")
        temporary.replace(output_dir / "progress.json")

    def collect_metrics() -> None:
        while not metrics_stop.is_set():
            try:
                sample_metrics()
            except Exception as error:
                # Keep credentials/URLs/exception text out of the evidence manifest.
                metrics_errors.append(type(error).__name__)
                return
            metrics_stop.wait(args.sample_interval_seconds)

    def owns_listener() -> bool:
        if child is None or child.poll() is not None:
            return False
        output = subprocess.run(["ss", "-ltnp", "sport = :8080"], check=True,
                                capture_output=True, text=True, timeout=2).stdout
        import re
        return {int(pid) for pid in re.findall(r"pid=(\d+)", output)} == {child.pid}

    source_thread.start()
    try:
        with server_log.open("wb") as log:
            child = subprocess.Popen([str(binary), str(config)],
                                     cwd=work, env=environment, stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                assert child.poll() is None, "owned server exited before readiness"
                if owns_listener():
                    if request_json("http://127.0.0.1:8080", "GET", "/ready", 3).get("status") == "ready":
                        break
                time.sleep(0.1)
            else:
                raise AssertionError("owned server readiness timed out")
            assert owns_listener(), "listener ownership changed"
            assert (Path("/proc") / str(child.pid) / "exe").resolve(strict=True) == binary, \
                "owned executable is not the requested binary"
            actual_executable_verified = True
            run_state.update({
                "state": "ready", "service_pid": child.pid,
                "service_start_ticks": int((Path("/proc") / str(child.pid) / "stat")
                                          .read_text().rsplit(") ", 1)[1].split()[19]),
                "config_sha256": hashlib.sha256(config.read_bytes()).hexdigest(),
            })
            write_state()
            metrics_thread = threading.Thread(target=collect_metrics)
            metrics_thread.start()
            command = [sys.executable, str(repo / "yolo_onnx_cpp/tools/multistream_acceptance.py"),
                "--duration-seconds", str(args.seconds),
                "--sample-interval-seconds", str(args.sample_interval_seconds),
                "--warmup-seconds", str(args.warmup_seconds),
                "--http-timeout-seconds", "5", "--pid", str(child.pid),
                "--server-config", str(config), "--model-file", str(primary_model),
                "--bearer-token-env", "YOLO_LOCAL_DIAGNOSTIC_TOKEN",
                "--output-dir", str(output_dir / "acceptance")]
            if args.single_model:
                command += ["--min-low-detection-fps", "0", "--min-high-detection-fps", "4"]
            else:
                command += ["--model-file", str(repo / "yolo_onnx_cpp/deploy/best_640x384.onnx")]
            for index in range(1, args.count + 1):
                command += ["--source", f"http://127.0.0.1:{source_server.server_port}/camera/{index}.mjpg"]
            collector = subprocess.Popen(command, env=environment, stdout=subprocess.PIPE,
                                         stderr=subprocess.PIPE, text=True)
            run_state.update({
                "state": "collecting", "collector_pid": collector.pid,
                "collector_start_ticks": int((Path("/proc") / str(collector.pid) / "stat")
                                            .read_text().rsplit(") ", 1)[1].split()[19]),
                "collection_started_at": datetime.now(timezone.utc).isoformat(),
            })
            write_state()
            deadline = time.monotonic() + args.seconds + args.warmup_seconds + 30
            while True:
                try:
                    stdout, stderr = collector.communicate(timeout=5)
                    break
                except subprocess.TimeoutExpired:
                    assert not metrics_errors, "metrics sampling failed during collection"
                    assert child.poll() is None, "service exited during collection"
                    assert time.monotonic() < deadline, "collector exceeded its deadline"
            collector_returncode = collector.returncode
            print(stdout, end="", flush=True)
            assert not stderr, "collector emitted runtime errors"
            assert child.poll() is None and owns_listener(), "server did not survive collection"
            assert request_json("http://127.0.0.1:8080", "GET", "/streams", 3,
                                bearer_token=token)["streams"] == [], "collector did not clean streams"
            metrics_stop.set()
            metrics_thread.join(timeout=5)
            assert not metrics_thread.is_alive(), "metrics sampler did not stop"
            assert not metrics_errors, "metrics sampling failed"
            # Collect manager-lifetime totals after streams have been retired.
            sample_metrics()
            values = {}
            for line in metrics_samples[-1]["prometheus_text"].splitlines():
                if line and not line.startswith("#") and "stream_id=" not in line:
                    key, value = line.rsplit(" ", 1)
                    values[key] = float(value)
            tiers = ("high",) if args.single_model else ("high", "low")
            retirement_evidence = {
                "active": values["yolo_streams_active"],
                "registered": values["yolo_streams_registered"],
                "scheduler": {
                    tier: {kind: int(values[f'yolo_scheduler_{kind}{{tier="{tier}"}}'])
                           for kind in ("cancelled_total", "queue_depth", "in_flight")}
                    for tier in tiers
                },
            }
            assert retirement_evidence["active"] == retirement_evidence["registered"] == 0
            assert all(stats["queue_depth"] == 0 for stats in retirement_evidence["scheduler"].values()), \
                "retired stream left queued model work"
            if args.count == 8:
                assert sum(stats["cancelled_total"] for stats in retirement_evidence["scheduler"].values()) > 0, \
                    "eight-stream regression did not exercise queued cancellation"
    except BaseException as error:
        run_error = type(error).__name__
        raise
    finally:
        if collector is not None and collector.poll() is None:
            collector.terminate()
            try:
                collector.wait(timeout=8)
            except subprocess.TimeoutExpired:
                collector.kill()
                collector.wait(timeout=5)
        metrics_stop.set()
        if metrics_thread is not None:
            metrics_thread.join(timeout=5)
        if child is not None and child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=8)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=5)
        stop.set()
        source_server.shutdown()
        source_server.server_close()
        source_thread.join(timeout=3)
        journal.close()
        (output_dir / "timing_metrics.json").write_text(
            json.dumps({"samples_file": "live_samples.jsonl", "sample_count": sample_count,
                        "errors": metrics_errors}, indent=2) + "\n")
        run_state.update({
            "state": "failed" if run_error else "collected",
            "run_error": run_error, "samples": sample_count,
            "service_returncode": child.returncode if child is not None else None,
            "collector_returncode": collector.returncode if collector is not None else None,
            "stopped_at": datetime.now(timezone.utc).isoformat(),
        })
        write_state()
        runtime_records = []
        if server_log.exists():
            for line in server_log.read_text(errors="replace").splitlines():
                if not line.startswith("{"):
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if record.get("component") == "model_runtime":
                    runtime_records.append(record)
        manifest = {"runtime_records": runtime_records,
            "service_binary": binary.name,
            "service_binary_sha256": binary_sha256,
            "service_binary_sha256_after": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "actual_executable_verified": actual_executable_verified,
            "memory_sample_count": sample_count,
            "retirement_evidence": retirement_evidence,
            "detection_mode": "single640" if args.single_model else "high_low",
            "acceptance_detection_minima": {"high": 4, "low": 0} if args.single_model else {"high": 0.5, "low": 4},
            "configured_opencv_threads": args.opencv_threads,
            "configured_high_threads": args.high_threads,
            "configured_low_threads": args.low_threads,
            "configured_cpu_pinning": args.cpu_pinning,
            "timing_metrics_file": "timing_metrics.json",
            "timing_metrics_sample_count": sample_count,
            "timing_metrics_errors": metrics_errors,
            "purpose": "local six-stream soak; not real-camera RTSP or production acceptance",
            "sample_interval_seconds": args.sample_interval_seconds,
            "stream_count": args.count,
            "configured_high_requests": args.high_requests,
            "configured_low_requests": args.low_requests,
            "distinct_source_contents": len(set(names)),
            "source_content_reused": args.count > len(set(names)),
            "performance_mode": args.mode,
            "requested_seconds": args.seconds, "sampling_includes_stream_startup": args.warmup_seconds == 0,
            "configured_low_detect_fps": args.low_detect_fps,
            "requested_warmup_seconds": args.warmup_seconds,
            "source_loop_seconds": 6, "inputs": metadata, "source_connections": observations,
            "collector_returncode": collector_returncode,
            "server_returncode": child.returncode if child is not None else None,
            "server_log_scope": "full local debug log, not a sanitized production log",
            "cpu_scope": "yolo_api only; source serving uses same host, pre-encoding finishes before collection"}
        (output_dir / "diagnostic.json").write_text(json.dumps(manifest, indent=2) + "\n")
    assert child is not None and child.returncode == 0, "owned server did not exit gracefully"
    assert actual_executable_verified and len(memory_samples) == len(metrics_samples)
    assert manifest["service_binary_sha256"] == manifest["service_binary_sha256_after"], "service binary changed"
    assert len(runtime_records) == (1 if args.single_model else 2), "missing actual runtime records"
    for record in runtime_records:
        assert record.get("runtime_build", "unavailable") != "unavailable", "missing actual runtime version"
        properties = record["properties"]
        assert properties.get("NUM_STREAMS", "unavailable") != "unavailable"
        if args.cpu_pinning != "inherit":
            raw = properties.get("ENABLE_CPU_PINNING", "unavailable").lower()
            assert raw in ("true", "yes", "1", "false", "no", "0"), "missing actual CPU pinning value"
            enabled = raw in ("true", "yes", "1")
            assert enabled == (args.cpu_pinning == "true"), "CPU pinning setting not applied"
    run_state["state"] = "finished"
    write_state()
    print("Local soak finished; retain the original acceptance status.", flush=True)


if __name__ == "__main__":
    main()
