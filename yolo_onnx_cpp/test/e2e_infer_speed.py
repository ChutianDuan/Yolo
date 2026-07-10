#!/usr/bin/env python3
"""End-to-end YOLO API smoke test with simple latency metrics."""

from __future__ import annotations

import argparse
import http.client
import json
import mimetypes
import random
import socket
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from statistics import mean, median
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BINARY = REPO_ROOT / "build" / "yolo_api"
DEFAULT_CONFIG = REPO_ROOT / "yolo_onnx_cpp" / "config.yaml"
DEFAULT_IMAGE_DIR = REPO_ROOT / "model" / "data" / "bdd100k_yolo_det" / "images" / "val"
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "yolo_onnx_cpp" / "test_outputs" / "e2e_infer_speed"
IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp"}
MEASURED_SCOPE = (
    "HTTP /infer round trip: multipart upload, server image preprocessing, "
    "ONNX inference, postprocessing, and JSON response read"
)


class TestError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run a YOLO API end-to-end inference test and save speed metrics."
    )
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument(
        "--model-path",
        type=Path,
        help="Override model_path in a copied runtime config, e.g. deploy/best_int8.onnx.",
    )
    parser.add_argument("--image", type=Path, help="Specific image to test.")
    parser.add_argument("--image-dir", type=Path, default=DEFAULT_IMAGE_DIR)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--requests", type=int, default=5)
    parser.add_argument("--seed", type=int)
    parser.add_argument("--startup-timeout", type=float, default=90.0)
    parser.add_argument("--request-timeout", type=float, default=120.0)
    parser.add_argument(
        "--use-running-server",
        action="store_true",
        help="Use an already running yolo_api on host:port instead of starting one.",
    )
    parser.add_argument(
        "--no-annotate",
        action="store_true",
        help="Skip drawing detection boxes onto the tested image.",
    )
    return parser.parse_args()


def resolve_path(path: Path) -> Path:
    if path.is_absolute():
        return path
    return (REPO_ROOT / path).resolve()


def select_image(image: Path | None, image_dir: Path, seed: int | None) -> Path:
    if image is not None:
        image_path = resolve_path(image)
        if not image_path.is_file():
            raise TestError(f"Image does not exist: {image_path}")
        return image_path

    image_dir = resolve_path(image_dir)
    if not image_dir.is_dir():
        raise TestError(f"Image directory does not exist: {image_dir}")

    images = [
        item
        for item in image_dir.iterdir()
        if item.is_file() and item.suffix.lower() in IMAGE_SUFFIXES
    ]
    if not images:
        raise TestError(f"No images found in: {image_dir}")

    rng = random.Random(seed)
    return rng.choice(images)


def port_is_open(host: str, port: int, timeout: float = 0.5) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def wait_for_port(host: str, port: int, timeout: float, process: subprocess.Popen[Any]) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise TestError(f"Server exited before accepting connections: {process.returncode}")
        if port_is_open(host, port):
            return
        time.sleep(0.25)
    raise TestError(f"Timed out waiting for {host}:{port}")


def start_server(args: argparse.Namespace, output_dir: Path) -> tuple[subprocess.Popen[Any] | None, Path | None]:
    if args.use_running_server:
        if not port_is_open(args.host, args.port):
            raise TestError(f"No running server detected at {args.host}:{args.port}")
        return None, None

    binary = resolve_path(args.binary)
    config = resolve_path(args.config)
    if not binary.is_file():
        raise TestError(f"Server binary does not exist: {binary}")
    if not config.is_file():
        raise TestError(f"Config file does not exist: {config}")
    if port_is_open(args.host, args.port):
        raise TestError(
            f"{args.host}:{args.port} is already open. "
            "Use --use-running-server or stop the existing process."
        )

    runtime_config = make_runtime_config(args, output_dir, config)
    log_path = output_dir / "server.log"
    log_file = log_path.open("w", encoding="utf-8")
    process = subprocess.Popen(
        [str(binary), str(runtime_config)],
        cwd=str(REPO_ROOT),
        stdout=log_file,
        stderr=subprocess.STDOUT,
        text=True,
    )
    log_file.close()
    wait_for_port(args.host, args.port, args.startup_timeout, process)
    return process, log_path


def make_runtime_config(args: argparse.Namespace, output_dir: Path, config: Path) -> Path:
    if args.model_path is None:
        return config

    model_path = resolve_path(args.model_path)
    if not model_path.is_file():
        raise TestError(f"Model file does not exist: {model_path}")

    lines = config.read_text(encoding="utf-8").splitlines()
    replaced = False
    runtime_lines: list[str] = []
    for line in lines:
        stripped = line.lstrip()
        if stripped.startswith("model_path:"):
            indent = line[: len(line) - len(stripped)]
            runtime_lines.append(f"{indent}model_path: {model_path}")
            replaced = True
        else:
            runtime_lines.append(line)

    if not replaced:
        runtime_lines.insert(0, f"model_path: {model_path}")

    runtime_config = output_dir / "runtime_config.yaml"
    runtime_config.write_text("\n".join(runtime_lines) + "\n", encoding="utf-8")
    return runtime_config


def selected_model_path(args: argparse.Namespace) -> Path:
    if args.model_path:
        return resolve_path(args.model_path)

    config = resolve_path(args.config)
    for line in config.read_text(encoding="utf-8").splitlines():
        stripped = line.lstrip()
        if not stripped.startswith("model_path:"):
            continue
        value = stripped.split(":", 1)[1].split("#", 1)[0].strip().strip("\'\"")
        model_path = Path(value)
        return model_path if model_path.is_absolute() else (config.parent / model_path).resolve()

    return config


def stop_server(process: subprocess.Popen[Any] | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)


def post_image(host: str, port: int, image_path: Path, timeout: float) -> tuple[int, dict[str, Any], float]:
    boundary = f"----codex-yolo-boundary-{time.time_ns()}"
    content_type = mimetypes.guess_type(image_path.name)[0] or "application/octet-stream"
    image_bytes = image_path.read_bytes()
    head = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="image"; filename="{image_path.name}"\r\n'
        f"Content-Type: {content_type}\r\n\r\n"
    ).encode("utf-8")
    tail = f"\r\n--{boundary}--\r\n".encode("utf-8")
    body = head + image_bytes + tail

    headers = {
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Content-Length": str(len(body)),
    }

    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    start = time.perf_counter()
    try:
        conn.request("POST", "/infer", body=body, headers=headers)
        response = conn.getresponse()
        response_body = response.read()
    finally:
        elapsed_ms = (time.perf_counter() - start) * 1000.0
        conn.close()

    try:
        payload = json.loads(response_body.decode("utf-8"))
    except json.JSONDecodeError as exc:
        snippet = response_body[:500].decode("utf-8", errors="replace")
        raise TestError(f"Response is not JSON: {snippet}") from exc

    return response.status, payload, elapsed_ms


def require_success(status: int, payload: dict[str, Any]) -> None:
    if status != 200:
        raise TestError(f"Expected HTTP 200, got {status}: {payload}")
    if payload.get("code") != 0 or payload.get("message") != "success":
        raise TestError(f"Unexpected API response: {payload}")
    output_shapes = payload.get("output_shapes")
    if output_shapes != [[1, 300, 6]]:
        raise TestError(f"Unexpected output_shapes: {output_shapes}")
    if not isinstance(payload.get("detections"), list):
        raise TestError("Response detections is not a list")


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    sorted_values = sorted(values)
    index = min(len(sorted_values) - 1, max(0, int(round((pct / 100.0) * (len(sorted_values) - 1)))))
    return sorted_values[index]


def rounded(value: float) -> float:
    return round(value, 3)


def build_metrics(
    image_path: Path,
    model_path: Path,
    payload: dict[str, Any],
    warmup_count: int,
    latency_ms: list[float],
    annotated_path: Path | None,
    server_log_path: Path | None,
) -> dict[str, Any]:
    mean_latency = mean(latency_ms)
    return {
        "image": str(image_path),
        "model_path": str(model_path),
        "warmup_requests": warmup_count,
        "measured_requests": len(latency_ms),
        "measured_scope": MEASURED_SCOPE,
        "latency_ms": {
            "min": rounded(min(latency_ms)),
            "max": rounded(max(latency_ms)),
            "mean": rounded(mean_latency),
            "median": rounded(median(latency_ms)),
            "p95": rounded(percentile(latency_ms, 95.0)),
        },
        "throughput": {
            "images_per_second": rounded(1000.0 / mean_latency),
            "seconds_per_image": rounded(mean_latency / 1000.0),
        },
        "output_shapes": payload.get("output_shapes"),
        "detections_count": len(payload.get("detections", [])),
        "annotated_image": str(annotated_path) if annotated_path else None,
        "server_log": str(server_log_path) if server_log_path else None,
    }


def draw_detections(image_path: Path, payload: dict[str, Any], output_dir: Path) -> Path:
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError as exc:
        raise TestError("Pillow is required to save annotated images. Use --no-annotate to skip.") from exc

    colors = [
        (36, 180, 75),
        (80, 120, 255),
        (255, 165, 0),
        (255, 80, 80),
        (180, 80, 255),
        (0, 210, 210),
    ]
    image = Image.open(image_path).convert("RGB")
    draw = ImageDraw.Draw(image)
    width, height = image.size
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 18)
    except OSError:
        font = ImageFont.load_default()

    for detection in payload.get("detections", []):
        box = detection.get("box", {})
        try:
            x1 = int(round(float(box["x1"])))
            y1 = int(round(float(box["y1"])))
            x2 = int(round(float(box["x2"])))
            y2 = int(round(float(box["y2"])))
        except (KeyError, TypeError, ValueError):
            continue

        x1 = max(0, min(width - 1, x1))
        y1 = max(0, min(height - 1, y1))
        x2 = max(0, min(width - 1, x2))
        y2 = max(0, min(height - 1, y2))
        class_id = int(detection.get("class_id", 0))
        class_name = str(detection.get("class_name", class_id))
        score = float(detection.get("score", 0.0))
        label = f"{class_name} {score:.2f}"
        color = colors[class_id % len(colors)]

        for offset in range(3):
            draw.rectangle((x1 - offset, y1 - offset, x2 + offset, y2 + offset), outline=color)

        text_box = draw.textbbox((0, 0), label, font=font)
        text_w = text_box[2] - text_box[0]
        text_h = text_box[3] - text_box[1]
        label_y = y1 - text_h - 8 if y1 - text_h - 8 >= 0 else y1 + 2
        label_x2 = min(width - 1, x1 + text_w + 8)
        draw.rectangle((x1, label_y, label_x2, label_y + text_h + 6), fill=color)
        draw.text((x1 + 4, label_y + 2), label, fill=(0, 0, 0), font=font)

    output_path = output_dir / f"{image_path.stem}_annotated.jpg"
    image.save(output_path, quality=95)
    return output_path


def run() -> int:
    args = parse_args()
    if args.requests <= 0:
        raise TestError("--requests must be positive")
    if args.warmup < 0:
        raise TestError("--warmup cannot be negative")

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_dir = resolve_path(args.output_root) / timestamp
    output_dir.mkdir(parents=True, exist_ok=True)

    image_path = select_image(args.image, args.image_dir, args.seed)
    process: subprocess.Popen[Any] | None = None
    server_log_path: Path | None = None
    last_payload: dict[str, Any] | None = None
    latency_ms: list[float] = []

    try:
        process, server_log_path = start_server(args, output_dir)

        total_requests = args.warmup + args.requests
        for index in range(total_requests):
            status, payload, elapsed_ms = post_image(
                args.host,
                args.port,
                image_path,
                args.request_timeout,
            )
            require_success(status, payload)
            last_payload = payload
            if index >= args.warmup:
                latency_ms.append(elapsed_ms)

        if last_payload is None:
            raise TestError("No response was collected")

        response_path = output_dir / "last_response.json"
        response_path.write_text(json.dumps(last_payload, indent=2), encoding="utf-8")

        annotated_path = None
        if not args.no_annotate:
            annotated_path = draw_detections(image_path, last_payload, output_dir)

        metrics = build_metrics(
            image_path,
            selected_model_path(args),
            last_payload,
            args.warmup,
            latency_ms,
            annotated_path,
            server_log_path,
        )
        metrics_path = output_dir / "metrics.json"
        metrics_path.write_text(json.dumps(metrics, indent=2), encoding="utf-8")

        print(f"image: {image_path}")
        print(f"output_dir: {output_dir}")
        print(f"metrics: {metrics_path}")
        if annotated_path:
            print(f"annotated_image: {annotated_path}")
        print(f"detections_count: {metrics['detections_count']}")
        print(f"mean_latency_ms: {metrics['latency_ms']['mean']}")
        print(f"throughput_images_per_second: {metrics['throughput']['images_per_second']}")
        return 0
    finally:
        stop_server(process)


def main() -> int:
    try:
        return run()
    except TestError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
