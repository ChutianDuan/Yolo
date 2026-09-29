#!/usr/bin/env python3
"""Exercise both production upload routes through a temporary-directory failure."""

from __future__ import annotations

import argparse
import json
import os
import re
import secrets
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


def request(path: str, token: str, body: bytes | None = None) -> tuple[int, bytes, str]:
    headers = {"Authorization": f"Bearer {token}"}
    if body is not None:
        headers["Content-Type"] = "multipart/form-data; boundary=yolo-staging-boundary"
    req = urllib.request.Request(
        "http://127.0.0.1:8080" + path, data=body, headers=headers,
        method="POST" if body is not None else "GET",
    )
    try:
        response = urllib.request.urlopen(req, timeout=3)
    except urllib.error.HTTPError as error:
        return error.code, error.read(), error.headers.get("X-Request-ID", "")
    with response:
        return response.status, response.read(), response.headers.get("X-Request-ID", "")


def owns_listener(process: subprocess.Popen[bytes]) -> bool:
    if process.poll() is not None:
        return False
    output = subprocess.run(
        ["ss", "-ltnp", "sport = :8080"], check=True,
        capture_output=True, text=True, timeout=2,
    ).stdout
    owners = {int(pid) for pid in re.findall(r"pid=(\d+)", output)}
    return owners == {process.pid}


def queue_metrics(token: str) -> dict[str, int]:
    status, body, _request_id = request("/metrics", token)
    assert status == 200, "metrics endpoint stopped responding"
    metrics = {}
    for line in body.decode().splitlines():
        if line.startswith("yolo_video_jobs_"):
            name, value = line.split()
            metrics[name] = int(value)
    assert len(metrics) == 5, "video-job telemetry was missing"
    return metrics


def run_test(server: Path, model: Path, backend: str) -> None:
    # Refuse an occupied port; never send uploads to an unrelated service.
    with socket.socket() as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        probe.bind(("0.0.0.0", 8080))

    with tempfile.TemporaryDirectory(prefix="yolo_staging_http_") as temporary:
        root = Path(temporary)
        staging = root / "private-not-a-directory"
        staging.write_bytes(b"marker")
        config = root / "config.yaml"
        config.write_text(
            f"model_path: {json.dumps(str(model))}\n"
            f"low_res_model_path: {json.dumps(str(model))}\n"
            f"model_backend: {backend}\n"
            "input_width: 640\ninput_height: 384\n"
            "low_res_input_width: 640\nlow_res_input_height: 384\n"
            "num_classes: 10\nthread_num: 1\nserver_io_threads: 2\n"
            "high_model_threads: 1\nlow_model_threads: 1\nopencv_threads: 1\n"
            "infer_request_count: 1\nlow_res_infer_request_count: 1\n"
            "video_job_threads: 1\nvideo_job_queue_depth: 1\n"
            "client_max_body_mb: 1\nclient_max_memory_body_mb: 1\n"
            "api_bearer_token_env: YOLO_STAGING_HTTP_TOKEN\n",
            encoding="utf-8",
        )
        token = secrets.token_urlsafe(32)
        environment = os.environ.copy()
        environment["TMPDIR"] = str(staging)
        environment["YOLO_STAGING_HTTP_TOKEN"] = token
        body = (
            b"--yolo-staging-boundary\r\n"
            b'Content-Disposition: form-data; name="video"; filename="private-upload-marker.mp4"\r\n'
            b"Content-Type: video/mp4\r\n\r\nnot-a-video\r\n"
            b"--yolo-staging-boundary--\r\n"
        )
        routes = ("/infer_video", "/infer_video_high_low")
        expected_logs: dict[str, tuple[int, str]] = {}
        with (root / "server.log").open("wb") as output:
            process = subprocess.Popen(
                [str(server), str(config)], cwd=root, env=environment,
                stdout=output, stderr=subprocess.STDOUT,
            )
            try:
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    assert process.poll() is None, "test server exited before readiness"
                    if owns_listener(process):
                        try:
                            status, payload, _request_id = request("/ready", token)
                            if status == 200 and json.loads(payload).get("status") == "ready":
                                break
                        except (OSError, urllib.error.URLError):
                            pass
                    time.sleep(0.05)
                else:
                    raise AssertionError("test server did not become ready")

                assert owns_listener(process), "test server does not exclusively own port 8080"
                baseline = queue_metrics(token)
                assert all(value == 0 for value in baseline.values()), "test queue was not empty"
                for route in routes:
                    assert owns_listener(process), "upload listener ownership changed"
                    status, payload, request_id = request(route, token, body)
                    assert status == 500, f"{route}: staging failure did not return HTTP 500"
                    assert json.loads(payload) == {
                        "code": 500, "message": "Failed to stage uploaded video"
                    }, f"{route}: staging response changed or leaked private details"
                    assert request_id.startswith("api-"), "staging response lost its request ID"
                    assert request_id not in expected_logs, "request ID was reused"
                    expected_logs[request_id] = (500, "video_staging_failed")
                    assert queue_metrics(token) == baseline, "failed staging entered the job queue"

                # Repair only the owned path, without restarting the service.
                staging.unlink()
                staging.mkdir()
                for route in routes:
                    assert owns_listener(process), "upload listener ownership changed"
                    status, payload, request_id = request(route, token, body)
                    assert status == 400 and json.loads(payload)["code"] == 400, (
                        f"{route}: repaired staging did not reach invalid-video validation"
                    )
                    assert request_id.startswith("api-") and request_id not in expected_logs
                    expected_logs[request_id] = (400, "invalid_video")

                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    metrics = queue_metrics(token)
                    if (metrics["yolo_video_jobs_submitted_total"] == 2
                        and metrics["yolo_video_jobs_completed_total"] == 2
                        and metrics["yolo_video_jobs_queued"] == 0
                        and metrics["yolo_video_jobs_in_flight"] == 0
                        and not list(staging.iterdir())):
                        break
                    time.sleep(0.05)
                else:
                    raise AssertionError("repaired staging jobs did not finish or clean up")
                assert metrics["yolo_video_jobs_rejected_total"] == 0
                assert not list(staging.iterdir()), "completed jobs leaked staged video files"
                assert request("/ready", token)[0] == 200, "service did not survive staging faults"
            finally:
                # Only this Popen child is signalled; never terminate an existing service.
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
            assert process.returncode == 0, "test server did not exit gracefully"

        records = []
        for line in (root / "server.log").read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("{"):
                record = json.loads(line)
                if record.get("component") == "api_inference":
                    records.append(record)
        serialized = json.dumps(records)
        assert token not in serialized and str(root) not in serialized
        assert "private-upload-marker" not in serialized, "structured API log leaked upload filename"
        for request_id, (status, reason) in expected_logs.items():
            selected = [record for record in records if record.get("request_id") == request_id]
            assert len(selected) == 2, "request did not emit exactly one start and one terminal log"
            assert selected[0]["event"] == "request_started"
            assert selected[1]["reason_code"] == reason and selected[1]["status_code"] == status
        print(f"video_staging_http_test passed: backend={backend}, "
              "2 staging failures, 2 recovered invalid-video jobs, correlated terminal logs")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--backend", choices=("onnx", "openvino"), required=True)
    arguments = parser.parse_args()
    run_test(arguments.server.resolve(strict=True), arguments.model.resolve(strict=True),
             arguments.backend)


if __name__ == "__main__":
    main()
