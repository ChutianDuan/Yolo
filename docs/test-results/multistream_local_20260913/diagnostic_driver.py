#!/usr/bin/env python3
"""One-off local MJPEG performance diagnostic; not a production RTSP acceptance."""
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
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import cv2


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--count", type=int, choices=(1, 2, 4), default=4)
    parser.add_argument("--requests", type=int, choices=(0, 1, 2, 4), default=0)
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--mode", choices=("throughput", "latency"), default="throughput")
    args = parser.parse_args()
    assert 0 < args.seconds <= 60, "diagnostic must be bounded to 60 seconds"
    repo = args.repo.resolve(strict=True)
    work = args.work_dir.resolve(strict=True)
    args.output_dir.mkdir(parents=True, exist_ok=False)
    output_dir = args.output_dir.resolve()
    sys.path.insert(0, str(repo / "yolo_onnx_cpp/tools"))
    from multistream_acceptance import request_json

    cv2.setNumThreads(1)
    video_root = repo / "datasets/bdd100k_tracking_video/bdd100k_videos_train_00/bdd100k/videos/train"
    names = ("0000f77c-6257be58", "00268999-cb063914",
             "012fdff1-9d1d0d1d", "0000f77c-cb820c98")[:args.count]
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
        probe.bind(("0.0.0.0", 8080))
    source_server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    source_server.daemon_threads = False
    source_thread = threading.Thread(target=source_server.serve_forever)
    token = secrets.token_urlsafe(32)
    config_text = (repo / "yolo_onnx_cpp/config.multistream.yaml").read_text()
    replacements = {
        "model_path: ./deploy/best.onnx": "model_path: " + json.dumps(str(repo / "yolo_onnx_cpp/deploy/best.onnx")),
        "low_res_model_path: ./deploy/best_640x384.onnx": "low_res_model_path: " + json.dumps(str(repo / "yolo_onnx_cpp/deploy/best_640x384.onnx")),
        "max_streams: 4": f"max_streams: {args.count}",
        "openvino_performance_mode: throughput": f"openvino_performance_mode: {args.mode}",
        'api_bearer_token_env: ""': "api_bearer_token_env: YOLO_LOCAL_DIAGNOSTIC_TOKEN",
        "infer_request_count: 0": f"infer_request_count: {args.requests}",
        "low_res_infer_request_count: 0": f"low_res_infer_request_count: {args.requests}",
    }
    for before, after in replacements.items():
        # Match complete lines: the high-model key is also a suffix of the low key.
        assert config_text.splitlines().count(before) == 1, "baseline configuration changed"
        config_text = "\n".join(after if line == before else line
                                for line in config_text.splitlines()) + "\n"
    config = output_dir / "diagnostic-config.yaml"
    config.write_text(config_text, encoding="utf-8")
    environment = os.environ.copy()
    environment["YOLO_LOCAL_DIAGNOSTIC_TOKEN"] = token
    child = None
    collector_returncode = None
    server_log = output_dir / "server.log"

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
            child = subprocess.Popen([str(repo / "build-openvino/yolo_api"), str(config)],
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
            command = [sys.executable, str(repo / "yolo_onnx_cpp/tools/multistream_acceptance.py"),
                "--duration-seconds", str(args.seconds), "--sample-interval-seconds", "1",
                "--http-timeout-seconds", "5", "--pid", str(child.pid),
                "--server-config", str(config), "--model-file", str(repo / "yolo_onnx_cpp/deploy/best.onnx"),
                "--model-file", str(repo / "yolo_onnx_cpp/deploy/best_640x384.onnx"),
                "--bearer-token-env", "YOLO_LOCAL_DIAGNOSTIC_TOKEN",
                "--output-dir", str(output_dir / "acceptance")]
            for index in range(1, args.count + 1):
                command += ["--source", f"http://127.0.0.1:{source_server.server_port}/camera/{index}.mjpg"]
            result = subprocess.run(command, env=environment, check=False, capture_output=True,
                                    text=True, timeout=args.seconds + 30)
            collector_returncode = result.returncode
            print(result.stdout, end="")
            assert not result.stderr, "collector emitted runtime errors"
            assert child.poll() is None and owns_listener(), "server did not survive collection"
            assert request_json("http://127.0.0.1:8080", "GET", "/streams", 3,
                                bearer_token=token)["streams"] == [], "collector did not clean streams"
    finally:
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
        manifest = {"purpose": "bounded local MJPEG diagnostic, not RTSP or production acceptance",
            "stream_count": args.count, "configured_requests": args.requests,
            "performance_mode": args.mode,
            "requested_seconds": args.seconds, "sampling_includes_stream_startup": True,
            "source_loop_seconds": 6, "inputs": metadata, "source_connections": observations,
            "collector_returncode": collector_returncode,
            "server_returncode": child.returncode if child is not None else None,
            "server_log_scope": "full local debug log, not a sanitized production log",
            "cpu_scope": "yolo_api only; source serving uses same host, pre-encoding finishes before collection"}
        (output_dir / "diagnostic.json").write_text(json.dumps(manifest, indent=2) + "\n")
    assert child is not None and child.returncode == 0, "owned server did not exit gracefully"
    print("Local diagnostic finished; retain collector FAIL/INCOMPLETE without lowering gates.")


if __name__ == "__main__":
    main()
