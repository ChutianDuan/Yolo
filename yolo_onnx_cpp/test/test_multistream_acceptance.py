from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
import sys
import unittest
from unittest import mock
from pathlib import Path
from types import SimpleNamespace


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "multistream_acceptance.py"
SPEC = importlib.util.spec_from_file_location("multistream_acceptance", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"failed to load {MODULE_PATH}")
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def arguments() -> SimpleNamespace:
    return SimpleNamespace(
        base_url="http://127.0.0.1:8080",
        pid=42,
        server_config=None,
        model_file=[],
        duration_seconds=10.0,
        sample_interval_seconds=10.0,
        http_timeout_seconds=1.0,
        lifecycle_cycles=0,
        lifecycle_interval_seconds=0.0,
        min_processed_fps=25.0,
        min_low_detection_fps=4.0,
        min_high_detection_fps=0.5,
        max_result_age_ms=300.0,
        max_fairness_spread_percent=10.0,
        max_cpu_percent=85.0,
        max_memory_growth_percent=5.0,
        max_queue_depth=2,
    )


def snapshot(processed: int, low: int, high: int, age_ms: float) -> dict[str, object]:
    return {
        "stream_id": "camera-1",
        "source": "rtsp://***@camera/stream",
        "status": "running",
        "decoded_frame_count": processed,
        "processed_frame_count": processed,
        "low_res_detection_count": low,
        "high_res_detection_count": high,
        "dropped_frame_count": 0,
        "decoder_queue_drop_count": 0,
        "processor_coalesced_frame_count": 0,
        "inference_error_count": 0,
        "max_queue_length": 2,
        "reconnect_count": 0,
        "latest_result_age_ms": age_ms,
    }


def good_samples() -> list[dict[str, object]]:
    return [
        {
            "observed_at_seconds": 0.0,
            "streams": {"camera-1": snapshot(1, 1, 1, 80.0)},
        },
        {
            "observed_at_seconds": 10.0,
            "streams": {"camera-1": snapshot(301, 41, 6, 120.0)},
        },
    ]


def provenance_fixture() -> dict[str, object]:
    return {
        "collector_host": {"cpu_models": ["synthetic CPU"], "logical_cpus": 4},
        "artifacts": [
            {"kind": kind, "name": name, "size_bytes": 1, "sha256": "a" * 64,
             "sha256_after": "a" * 64, "unchanged": True}
            for kind, name in (("config", "config.yaml"), ("model", "model.onnx"))
        ],
        "artifacts_stable": True, "errors": [],
    }


class MultistreamAcceptanceTest(unittest.TestCase):
    def test_real_http_error_preserves_status_without_echo(self) -> None:
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                self.send_response(503)
                self.end_headers()
                self.wfile.write(b"Bearer synthetic-http-secret")

            def log_message(self, *_args):
                pass

        server = HTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with self.assertRaises(RuntimeError) as raised:
                MODULE.request_json(f"http://127.0.0.1:{server.server_port}",
                                    "GET", "/streams", 1.0)
            self.assertEqual(MODULE._safe_error(raised.exception), "HTTP request returned HTTP 503")
            self.assertNotIn("synthetic-http-secret", str(raised.exception))
            self.assertTrue(raised.exception.__suppress_context__)
        finally:
            server.shutdown()
            thread.join(timeout=2.0)
            server.server_close()
        self.assertFalse(thread.is_alive())

    def test_invalid_json_error_does_not_retain_response_in_traceback(self) -> None:
        response = mock.MagicMock()
        response.__enter__.return_value.read.return_value = b"synthetic-json-secret"
        with mock.patch.object(MODULE.urllib.request, "urlopen", return_value=response):
            with self.assertRaises(RuntimeError) as raised:
                MODULE.request_json("http://host", "GET", "/streams", 1.0)
        self.assertEqual(MODULE._safe_error(raised.exception), "HTTP response contained invalid JSON")
        self.assertTrue(raised.exception.__suppress_context__)

    def test_reports_omit_endpoint_and_remote_free_text(self) -> None:
        args = arguments()
        args.base_url = "http://user:synthetic-password@host/private-key?token=query-key#fragment-key"
        samples = good_samples()
        for sample in samples:
            sample["debug"] = "synthetic-debug-secret"
            item = sample["streams"]["camera-1"]
            item["source"] = args.base_url
            item["last_error"] = "synthetic-error-secret"
            item["unknown"] = {"Authorization": "Bearer synthetic-bearer-secret"}
        original = json.dumps(samples)
        report = self.report(args=args, samples=samples)
        exported = json.dumps(report) + MODULE.markdown_report(report)
        for secret in ("synthetic-password", "private-key", "query-key", "fragment-key",
                       "synthetic-debug-secret", "synthetic-error-secret", "synthetic-bearer-secret"):
            self.assertNotIn(secret, exported)
        self.assertEqual(report["overall_status"], "PASS")
        self.assertEqual(json.dumps(samples), original)
        self.assertEqual(report["base_url"], "[redacted]")
        self.assertEqual(report["streams"][0]["source"], "[redacted]")

    def test_request_errors_do_not_read_or_echo_remote_secrets(self) -> None:
        body = mock.Mock()
        body.read.return_value = b"synthetic-response-secret"
        errors = [
            MODULE.urllib.error.HTTPError("http://secret-host", 503, "synthetic-reason-secret", {}, body),
            MODULE.urllib.error.URLError("synthetic-reason-secret"),
        ]
        for error in errors:
            with self.subTest(error=type(error).__name__), \
                 mock.patch.object(MODULE.urllib.request, "urlopen", side_effect=error):
                with self.assertRaises(RuntimeError) as raised:
                    MODULE.request_json("http://host", "GET", "/streams", 1.0)
                self.assertNotIn("synthetic", str(raised.exception))
                self.assertTrue(raised.exception.__suppress_context__)
        body.read.assert_not_called()

    def test_request_payload_keeps_required_source_credentials(self) -> None:
        source = "rtsp://user:synthetic-password@camera/private-key?token=query-key"
        response = mock.MagicMock()
        response.__enter__.return_value.read.return_value = b'{}'
        with mock.patch.object(MODULE.urllib.request, "urlopen", return_value=response) as opened:
            MODULE.request_json("http://host", "POST", "/streams", 1.0,
                                {"source": source}, "synthetic-bearer-secret")
        request = opened.call_args.args[0]
        self.assertEqual(json.loads(request.data)["source"], source)
        self.assertEqual(request.get_header("Authorization"), "Bearer synthetic-bearer-secret")

    def report(self, *, args=None, samples=None, cpu_samples=None, process_samples=None,
               provenance=None):
        return MODULE.build_report(
            arguments() if args is None else args,
            "2026-09-05T00:00:00+00:00",
            good_samples() if samples is None else samples,
            ["camera-1"],
            [0.0] if cpu_samples is None else cpu_samples,
            [
                MODULE.ProcessSample(0.0, 0, 1000),
                MODULE.ProcessSample(10.0, 0, 1000),
            ] if process_samples is None else process_samples,
            provenance=provenance_fixture() if provenance is None else provenance,
        )

    @staticmethod
    def gate(report, name):
        return next(entry for entry in report["gates"] if entry["name"] == name)


    def test_warmup_zero_keeps_original_schema_and_no_requests(self) -> None:
        with mock.patch.object(MODULE, "request_json") as request, \
             mock.patch.object(MODULE.time, "monotonic") as clock:
            warmed = MODULE.run_sampling_warmup(arguments(), ["camera-1"], None)
        request.assert_not_called()
        clock.assert_not_called()
        self.assertTrue(warmed["completed"])
        report = self.report()
        self.assertEqual(report["schema_version"], 4)
        self.assertNotIn("sampling_warmup", report)

    def test_warmup_observes_same_advancing_streams_without_samples(self) -> None:
        args = arguments()
        args.warmup_seconds = 2.0
        args.sample_interval_seconds = 1.0
        responses = [{"streams": [snapshot(n, n, n, 80.0)]} for n in (1, 31, 61)]
        responses[0]["streams"][0]["status"] = "connecting"
        with mock.patch.object(MODULE, "request_json", side_effect=responses) as request, \
             mock.patch.object(MODULE.time, "monotonic", side_effect=[10.0, 10.0, 11.0, 12.0]), \
             mock.patch.object(MODULE.time, "sleep"):
            warmed = MODULE.run_sampling_warmup(args, ["camera-1"], "secret-token")
        self.assertTrue(warmed["completed"])
        self.assertEqual(warmed["elapsed_seconds"], 2.0)
        self.assertEqual(warmed["observation_count"], 3)
        self.assertEqual(warmed["final_counters"]["camera-1"]["processed_frame_count"], 61)
        self.assertNotIn("samples", warmed)
        self.assertNotIn("source", json.dumps(warmed))
        self.assertNotIn("secret-token", json.dumps(warmed))
        self.assertTrue(all(call.args[1:3] == ("GET", "/streams") for call in request.call_args_list))

    def test_warmup_cannot_hide_terminal_errors_resets_or_no_progress(self) -> None:
        args = arguments()
        args.warmup_seconds = 2.0
        for failure in ("terminal", "missing", "infer", "reset", "stalled", "interrupt", "http"):
            items = [snapshot(n, n, n, 80.0) for n in (1, 31, 61)]
            responses = [{"streams": [item]} for item in items]
            if failure == "terminal":
                items[0]["status"] = "failed"
            elif failure == "missing":
                responses[0]["streams"] = []
            elif failure == "infer":
                items[0]["inference_error_count"] = 1
            elif failure == "reset":
                items[1]["processed_frame_count"] = 0
            elif failure == "stalled":
                responses = [{"streams": [items[0]]}] * 3
            elif failure == "interrupt":
                responses = [KeyboardInterrupt()]
            elif failure == "http":
                responses = [RuntimeError("rtsp://private-user:private-secret@host")]
            with self.subTest(failure=failure), \
                 mock.patch.object(MODULE, "request_json", side_effect=responses), \
                 mock.patch.object(MODULE.time, "monotonic", side_effect=[0.0, 0.0, 1.0, 2.0]), \
                 mock.patch.object(MODULE.time, "sleep"):
                warmed = MODULE.run_sampling_warmup(args, ["camera-1"], None)
            self.assertFalse(warmed["completed"])
            self.assertTrue(warmed["errors"])
            self.assertNotIn("private-secret", json.dumps(warmed))

    def test_warmup_report_requires_explicit_valid_evidence(self) -> None:
        args = arguments()
        args.warmup_seconds = 2.0
        valid = {"requested_seconds": 2.0, "elapsed_seconds": 2.0,
                 "observation_count": 3, "completed": True, "errors": [],
                 "final_counters": {"camera-1": {key: snapshot(1, 1, 1, 80.0)[key]
                                                for key in MODULE.COUNTERS}}}
        for evidence, status in ((valid, "PASS"), (None, "INCOMPLETE"),
                                 ({**valid, "elapsed_seconds": 1.0}, "FAIL"),
                                 ({**valid, "errors": ["warmup failed"]}, "FAIL")):
            with self.subTest(status=status, evidence=evidence):
                report = MODULE.build_report(
                    args, "2026-09-14T00:00:00Z", good_samples(), ["camera-1"], [0.0],
                    [MODULE.ProcessSample(0.0, 0, 1000), MODULE.ProcessSample(10.0, 0, 1000)],
                    provenance=provenance_fixture(), sampling_warmup=evidence,
                )
                self.assertEqual(report["overall_status"], status)
                self.assertEqual(report["schema_version"], 5)
                self.assertIn("同一批流采样前预热", MODULE.markdown_report(report))
                self.assertEqual(report["elapsed_seconds"], 10.0)
                self.assertEqual(report["process"]["rss_start_bytes"], 1000)

    def test_warmup_cli_rejects_nonfinite_or_negative_duration(self) -> None:
        for value in ("-1", "nan", "inf"):
            with self.subTest(value=value), mock.patch.object(sys, "argv",
                    ["acceptance", "--warmup-seconds", value]), \
                 contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    MODULE.parse_arguments()
        with mock.patch.object(sys, "argv", ["acceptance", "--warmup-seconds", "2"]):
            self.assertEqual(MODULE.parse_arguments().warmup_seconds, 2.0)


    def test_main_warmup_keeps_owned_streams_cleanup_and_post_warmup_baselines(self) -> None:
        for failure in (None, "terminal", "transition_reset"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                args = arguments()
                args.warmup_seconds = 2.0
                args.source, args.sources_file, args.bearer_token_env = ["one.mp4"], None, None
                args.stream_prefix, args.keep_streams = "acceptance", False
                args.output_dir = Path(temporary) / "run"
                created, deleted = [], []
                polls = 0

                def request(_url, method, path, _timeout, payload=None, **_kwargs):
                    nonlocal polls
                    if path == "/ready":
                        return {"status": "ready"}
                    if method == "POST":
                        self.assertEqual(payload["source"], args.source[len(created)])
                        created.append(payload["stream_id"])
                        return {"stream_id": created[-1]}
                    if method == "DELETE":
                        deleted.append(path.removeprefix("/streams/"))
                        return {"status": "stopped"}
                    counts = (1, 61, 61, 361)
                    item = snapshot(counts[polls], (1, 9, 9, 49)[polls],
                                    (1, 2, 2, 7)[polls], 80.0)
                    item["stream_id"] = created[0]
                    if failure == "terminal" and polls == 0:
                        item["status"] = "failed"
                    if failure == "transition_reset" and polls == 2:
                        item["processed_frame_count"] = 0
                    polls += 1
                    return {"streams": [item]}

                with mock.patch.object(MODULE, "parse_arguments", return_value=args), \
                     mock.patch.object(MODULE, "collect_provenance", return_value=provenance_fixture()), \
                     mock.patch.object(MODULE, "request_json", side_effect=request), \
                     mock.patch.object(MODULE.time, "monotonic", side_effect=[0.0, 0.0, 2.0, 10.0, 20.0]), \
                     mock.patch.object(MODULE.time, "sleep"), \
                     mock.patch.object(MODULE, "read_process_sample", side_effect=[
                         MODULE.ProcessSample(10.0, 0, 1000),
                         MODULE.ProcessSample(20.0, 0, 1000),
                     ]) as process, contextlib.redirect_stdout(io.StringIO()) as stdout, \
                     contextlib.redirect_stderr(io.StringIO()):
                    code = MODULE.main()
                report = json.loads((args.output_dir / "acceptance.json").read_text())
                self.assertEqual(deleted, list(reversed(created)))
                self.assertEqual(len(created), 1)
                self.assertEqual(code, 0 if failure is None else 2)
                self.assertEqual(report["schema_version"], 5)
                self.assertEqual(report["sample_count"], 2 if failure is None else 0)
                self.assertEqual(process.call_count, 2 if failure is None else 0)
                if failure is None:
                    self.assertEqual(report["elapsed_seconds"], 10.0)
                    self.assertEqual(report["sampling_warmup"]["elapsed_seconds"], 2.0)
                    self.assertEqual(report["streams"][0]["processed_frames_delta"], 300)
                    self.assertEqual(report["process"]["rss_start_bytes"], 1000)
                else:
                    self.assertTrue(report["run_errors"])

    def test_missing_reproducibility_evidence_is_incomplete(self) -> None:
        for missing in ("all", "config", "model", "cpu"):
            metadata = provenance_fixture()
            if missing == "all":
                metadata = {}
            elif missing == "cpu":
                metadata["collector_host"] = {}
            else:
                metadata["artifacts"] = [
                    item for item in metadata["artifacts"] if item["kind"] != missing
                ]
            with self.subTest(missing=missing):
                report = self.report(provenance=metadata)
                self.assertEqual(report["overall_status"], "INCOMPLETE")
                self.assertIsNone(self.gate(report, "reproducibility evidence recorded")["passed"])

    def test_fingerprint_is_chunked_and_does_not_copy_contents_or_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "model.onnx"
            contents = b"private-model-content" * 60000
            path.write_bytes(contents)
            result = MODULE.file_fingerprint(path)
            self.assertEqual(result["sha256"], MODULE.hashlib.sha256(contents).hexdigest())
            self.assertEqual(result["size_bytes"], len(contents))
            self.assertEqual(result["name"], "model.onnx")
            self.assertNotIn(temporary, json.dumps(result))
            self.assertNotIn("private-model-content", json.dumps(result))
            with self.assertRaisesRegex(ValueError, "regular file"):
                MODULE.file_fingerprint(Path(temporary))

    def test_fingerprint_rejects_a_change_during_hashing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "model.onnx"
            path.write_bytes(b"weights")
            before = path.stat()
            changed = SimpleNamespace(
                st_dev=before.st_dev, st_ino=before.st_ino, st_size=before.st_size,
                st_mtime_ns=before.st_mtime_ns + 1, st_ctime_ns=before.st_ctime_ns,
            )
            with mock.patch.object(MODULE.Path, "is_file", return_value=True), \
                 mock.patch.object(MODULE.Path, "stat", side_effect=[before, changed]):
                with self.assertRaisesRegex(ValueError, "changed while hashing"):
                    MODULE.file_fingerprint(path)

    def test_artifact_change_or_disappearance_fails_and_is_sanitized(self) -> None:
        for failure in ("change", "missing"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                args = arguments()
                args.server_config = Path(temporary) / "config.yaml"
                args.model_file = [Path(temporary) / "model.onnx"]
                args.server_config.write_text("token: secret-config-value\n")
                args.model_file[0].write_bytes(b"initial weights")
                metadata = MODULE.collect_provenance(args)
                metadata["collector_host"] = provenance_fixture()["collector_host"]
                if failure == "change":
                    args.model_file[0].write_bytes(b"changed weights")
                else:
                    args.model_file[0].unlink()
                MODULE.verify_artifacts(args, metadata)
                self.assertFalse(metadata["artifacts_stable"])
                self.assertTrue(metadata["artifacts"][0]["unchanged"])
                self.assertFalse(metadata["artifacts"][1]["unchanged"])
                report = self.report(provenance=metadata)
                self.assertEqual(report["overall_status"], "FAIL")
                serialized = json.dumps(report) + MODULE.markdown_report(report)
                self.assertNotIn(temporary, serialized)
                self.assertNotIn("secret-config-value", serialized)

    def test_cpu_identity_records_unique_topology_and_separate_affinities(self) -> None:
        cpuinfo = "\n\n".join(
            f"processor : {index}\nmodel name : synthetic CPU\n"
            f"physical id : {index // 2}\ncore id : 0"
            for index in range(4)
        )
        with mock.patch.object(MODULE.Path, "read_text", return_value=cpuinfo), \
             mock.patch.object(MODULE.os, "sched_getaffinity",
                               side_effect=lambda pid: {0, 1} if pid == 0 else {1}):
            metadata = MODULE.collect_provenance(arguments())
        host = metadata["collector_host"]
        self.assertEqual(host["cpu_models"], ["synthetic CPU"])
        self.assertEqual(host["physical_cores"], 2)
        self.assertEqual(host["sockets"], 2)
        self.assertEqual(host["collector_cpu_affinity"], [0, 1])
        self.assertEqual(host["monitored_pid_cpu_affinity"], [1])

    def test_invalid_artifact_is_rejected_before_any_server_state_change(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            args = arguments()
            args.source = ["one.mp4"]
            args.sources_file = None
            args.bearer_token_env = None
            args.server_config = Path(temporary) / "missing-config.yaml"
            args.output_dir = Path(temporary) / "run"
            output = io.StringIO()
            with mock.patch.object(MODULE, "parse_arguments", return_value=args), \
                 mock.patch.object(MODULE, "request_json") as request, \
                 contextlib.redirect_stderr(output):
                self.assertEqual(MODULE.main(), 2)
            request.assert_not_called()
            self.assertFalse(args.output_dir.exists())
            self.assertNotIn(temporary, output.getvalue())

    def test_artifact_cli_accepts_config_and_multiple_models(self) -> None:
        with mock.patch.object(sys, "argv", [
            "acceptance", "--server-config", "server.yaml",
            "--model-file", "high.onnx", "--model-file", "low.onnx",
        ]):
            args = MODULE.parse_arguments()
        self.assertEqual(args.server_config, Path("server.yaml"))
        self.assertEqual(args.model_file, [Path("high.onnx"), Path("low.onnx")])

    def test_bearer_token_is_read_from_environment(self) -> None:
        disabled = SimpleNamespace(bearer_token_env=None)
        self.assertIsNone(MODULE.load_bearer_token(disabled))

        configured = SimpleNamespace(bearer_token_env="ACCEPTANCE_TOKEN")
        with mock.patch.dict(MODULE.os.environ, {"ACCEPTANCE_TOKEN": "secret-token"}):
            self.assertEqual(
                MODULE.load_bearer_token(configured),
                "secret-token",
            )

        with mock.patch.dict(MODULE.os.environ, {}, clear=True):
            with self.assertRaisesRegex(ValueError, "unset or empty"):
                MODULE.load_bearer_token(configured)

    def test_percentile_uses_nearest_rank(self) -> None:
        values = [4.0, 1.0, 3.0, 2.0]
        self.assertEqual(MODULE.percentile(values, 0.50), 2.0)
        self.assertEqual(MODULE.percentile(values, 0.95), 4.0)
        self.assertIsNone(MODULE.percentile([], 0.95))

    def test_passing_stream_report(self) -> None:
        report = self.report()
        self.assertTrue(report["overall_passed"])
        self.assertEqual(report["overall_status"], "PASS")
        stream = report["streams"][0]
        self.assertEqual(stream["processed_fps"], 30.0)
        self.assertEqual(stream["low_detection_fps"], 4.0)
        self.assertEqual(stream["high_detection_fps"], 0.5)
        self.assertEqual(stream["result_age_ms_p95"], 120.0)
        self.assertIn("总体结果：PASS", MODULE.markdown_report(report))

    def test_missing_stream_fails_overall_gate(self) -> None:
        report = MODULE.build_report(
            arguments(),
            "2026-09-05T00:00:00+00:00",
            good_samples(),
            ["camera-1", "camera-2"],
            [],
            [],
        )

        self.assertFalse(report["overall_passed"])
        observed_gate = next(
            entry
            for entry in report["gates"]
            if entry["name"] == "all requested streams observed"
        )
        self.assertFalse(observed_gate["passed"])
        self.assertEqual(observed_gate["actual"], "1/2")


    def test_missing_resources_are_incomplete(self) -> None:
        report = self.report(cpu_samples=[], process_samples=[])
        self.assertFalse(report["overall_passed"])
        self.assertEqual(report["overall_status"], "INCOMPLETE")
        self.assertIn("总体结果：INCOMPLETE", MODULE.markdown_report(report))

    def test_short_run_cannot_pass_a_long_soak(self) -> None:
        args = arguments()
        args.duration_seconds = 28800.0
        report = self.report(args=args)
        self.assertEqual(report["overall_status"], "FAIL")
        self.assertFalse(self.gate(report, "requested duration covered")["passed"])

    def test_stream_must_be_present_in_every_sample(self) -> None:
        samples = good_samples()
        samples.insert(1, {"observed_at_seconds": 5.0, "streams": {}})
        report = self.report(samples=samples)
        self.assertFalse(self.gate(report, "camera-1: every sample observed")["passed"])

    def test_terminal_status_cannot_be_hidden_by_recovery(self) -> None:
        for status in ("completed", "stopped", "expired", "failed"):
            with self.subTest(status=status):
                samples = good_samples()
                middle = snapshot(150, 20, 3, 100.0)
                middle["status"] = status
                samples.insert(1, {"observed_at_seconds": 5.0, "streams": {"camera-1": middle}})
                report = self.report(samples=samples)
                self.assertFalse(self.gate(report, "camera-1: healthy status")["passed"])

    def test_counter_reset_cannot_be_hidden_by_final_totals(self) -> None:
        samples = good_samples()
        samples.insert(1, {
            "observed_at_seconds": 5.0,
            "streams": {"camera-1": snapshot(0, 0, 0, 0.0)},
        })
        report = self.report(samples=samples)
        self.assertFalse(self.gate(report, "camera-1: counters never reset")["passed"])

    def test_errors_before_first_sample_are_counted(self) -> None:
        samples = good_samples()
        for sample in samples:
            sample["streams"]["camera-1"]["inference_error_count"] = 1
        report = self.report(samples=samples)
        self.assertFalse(self.gate(report, "camera-1: inference errors")["passed"])

    def test_cpu_average_weights_time_instead_of_poll_count(self) -> None:
        samples = [
            MODULE.ProcessSample(0.0, 0, 1000),
            MODULE.ProcessSample(1.0, 100, 1000),
            MODULE.ProcessSample(10.0, 100, 1000),
        ]
        with mock.patch.object(MODULE.os, "cpu_count", return_value=1), mock.patch.object(
            MODULE.os, "sysconf", return_value=100
        ):
            report = self.report(cpu_samples=[100.0, 0.0], process_samples=samples)
        self.assertEqual(report["process"]["cpu_average_percent_of_host"], 10.0)
        self.assertEqual(report["process"]["cpu_peak_percent_of_host"], 100.0)

    def test_process_restart_invalidates_resource_measurements(self) -> None:
        report = self.report(process_samples=[
            MODULE.ProcessSample(0.0, 0, 1000, start_ticks=10),
            MODULE.ProcessSample(10.0, 1, 1000, start_ticks=20),
        ])
        self.assertEqual(report["overall_status"], "FAIL")
        self.assertIsNone(report["process"]["cpu_average_percent_of_host"])

    def test_empty_or_unordered_samples_fail(self) -> None:
        unordered = list(reversed(good_samples()))
        for samples in ([], good_samples()[:1], unordered):
            with self.subTest(samples=samples):
                report = self.report(samples=samples)
                self.assertFalse(self.gate(report, "valid sampling timeline")["passed"])

    def test_polling_gap_cannot_be_hidden_by_full_duration(self) -> None:
        args = arguments()
        args.sample_interval_seconds = 1.0
        args.http_timeout_seconds = 1.0
        report = self.report(args=args)
        self.assertFalse(self.gate(report, "sampling continuity")["passed"])

    def test_invalid_telemetry_is_rejected(self) -> None:
        for key, value in (
            ("latest_result_age_ms", float("nan")),
            ("processed_frame_count", -1),
            ("processed_frame_count", True),
            ("status", None),
            ("status", "synthetic-status-secret"),
            ("status", {"token": "synthetic-status-secret"}),
        ):
            with self.subTest(key=key, value=value):
                item = snapshot(1, 1, 1, 50.0)
                item[key] = value
                with self.assertRaises(ValueError):
                    MODULE.stream_snapshots({"streams": [item]}, ["camera-1"])

    def test_invalid_cli_values_are_rejected(self) -> None:
        for option, value in (
            ("--duration-seconds", "nan"), ("--duration-seconds", "inf"),
            ("--sample-interval-seconds", "0"), ("--http-timeout-seconds", "-1"),
            ("--max-cpu-percent", "nan"), ("--pid", "0"),
            ("--lifecycle-cycles", "-1"),
            ("--lifecycle-interval-seconds", "nan"),
            ("--lifecycle-interval-seconds", "-1"),
            ("--stream-prefix", "invalid/id"),
        ):
            with self.subTest(option=option, value=value), mock.patch.object(
                sys, "argv", ["acceptance", option, value]
            ), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    MODULE.parse_arguments()
                self.assertEqual(error.exception.code, 2)

    def test_lifecycle_churn_reuses_ids_and_verifies_cleanup(self) -> None:
        args = arguments()
        args.lifecycle_cycles = 3
        stream_ids = ["camera-1", "camera-2"]
        sources = ["one.mp4", "two.mp4"]
        active: dict[str, str] = {}

        def request(_url, method, path, _timeout, payload=None, **_kwargs):
            if method == "POST":
                self.assertNotIn(payload["stream_id"], active)
                active[payload["stream_id"]] = payload["source"]
                return {"stream_id": payload["stream_id"]}
            if method == "DELETE":
                stream_id = path.removeprefix("/streams/")
                self.assertIn(stream_id, active)
                del active[stream_id]
                return {"status": "stopped"}
            return {
                "streams": [
                    {**snapshot(1, 1, 1, 0.0), "stream_id": stream_id}
                    for stream_id in active
                ]
            }

        with mock.patch.object(MODULE, "request_json", side_effect=request), \
             mock.patch.object(MODULE.time, "monotonic", side_effect=[1.0, 4.0]), \
             mock.patch.object(MODULE.time, "sleep") as sleep:
            result = MODULE.run_lifecycle_churn(args, stream_ids, sources, None)

        self.assertEqual(result["completed_cycles"], 3)
        self.assertEqual(result["created_streams"], 6)
        self.assertEqual(result["delete_attempts"], 6)
        self.assertEqual(result["deleted_streams"], 6)
        self.assertEqual(result["elapsed_seconds"], 3.0)
        self.assertEqual(result["errors"], [])
        self.assertEqual(active, {})
        sleep.assert_not_called()

    def test_lifecycle_churn_cleans_partial_cycle_and_stops(self) -> None:
        args = arguments()
        args.lifecycle_cycles = 3
        active: set[str] = set()

        def request(_url, method, path, _timeout, payload=None, **_kwargs):
            if method == "POST":
                if payload["stream_id"] == "camera-2":
                    raise RuntimeError("synthetic-creation-secret")
                active.add(payload["stream_id"])
                return {"stream_id": payload["stream_id"]}
            if method == "DELETE":
                active.remove(path.removeprefix("/streams/"))
                return {"status": "stopped"}
            return {"streams": []}

        with mock.patch.object(MODULE, "request_json", side_effect=request), \
             mock.patch.object(MODULE.time, "monotonic", side_effect=[1.0, 2.0]):
            result = MODULE.run_lifecycle_churn(
                args, ["camera-1", "camera-2"], ["one.mp4", "two.mp4"], None
            )

        self.assertEqual(result["completed_cycles"], 0)
        self.assertEqual(result["created_streams"], 1)
        self.assertEqual(result["deleted_streams"], 1)
        self.assertEqual(active, set())
        self.assertEqual(result["errors"], ["cycle 1: RuntimeError"])

    def test_lifecycle_churn_reports_failed_cleanup_and_remaining_stream(self) -> None:
        args = arguments()
        args.lifecycle_cycles = 2
        active = {"camera-1"}

        def request(_url, method, path, _timeout, payload=None, **_kwargs):
            if method == "POST":
                return {"stream_id": payload["stream_id"]}
            if method == "DELETE":
                raise RuntimeError("delete unavailable")
            return {
                "streams": [
                    {**snapshot(1, 1, 1, 0.0), "stream_id": stream_id}
                    for stream_id in active
                ]
            }

        with mock.patch.object(MODULE, "request_json", side_effect=request), \
             mock.patch.object(MODULE.time, "monotonic", side_effect=[1.0, 2.0]):
            result = MODULE.run_lifecycle_churn(
                args, ["camera-1"], ["one.mp4"], "token"
            )

        self.assertEqual(result["completed_cycles"], 0)
        self.assertEqual(result["delete_attempts"], 1)
        self.assertEqual(result["deleted_streams"], 0)
        self.assertEqual(len(result["errors"]), 2)
        self.assertIn("cleanup failed for camera-1", result["errors"][0])
        self.assertIn("still registered after cleanup", result["errors"][1])

    def test_main_stops_after_lifecycle_failure_and_writes_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            args = arguments()
            args.source = ["one.mp4"]
            args.sources_file = None
            args.bearer_token_env = None
            args.stream_prefix = "acceptance"
            args.output_dir = Path(temporary) / "run"
            args.keep_streams = False
            args.lifecycle_cycles = 2
            post_count = 0

            def request(_url, method, path, _timeout, payload=None, **_kwargs):
                nonlocal post_count
                if path == "/ready":
                    return {"status": "ready"}
                if method == "POST":
                    post_count += 1
                    raise RuntimeError("creation rejected")
                return {"streams": []}

            with mock.patch.object(MODULE, "parse_arguments", return_value=args), \
                 mock.patch.object(MODULE, "request_json", side_effect=request), \
                 mock.patch.object(MODULE.time, "monotonic", side_effect=[1.0, 2.0]), \
                 contextlib.redirect_stdout(io.StringIO()), \
                 contextlib.redirect_stderr(io.StringIO()):
                code = MODULE.main()

            report = json.loads((args.output_dir / "acceptance.json").read_text())
            self.assertEqual(code, 2)
            self.assertEqual(post_count, 1)
            self.assertEqual(report["schema_version"], 4)
            self.assertEqual(report["lifecycle_churn"]["completed_cycles"], 0)
            self.assertEqual(report["sample_count"], 0)
            self.assertIn("lifecycle churn failed", report["run_errors"][0])
            self.assertFalse(self.gate(report, "lifecycle churn completed")["passed"])

    def test_main_preserves_requested_scope_and_failure_evidence(self) -> None:
        for failure in (None, "create", "collect", "cleanup", "artifact"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                args = arguments()
                args.source = ["rtsp://user:synthetic-source-secret@camera-one/private-key?token=query-key",
                               "rtsp://camera-two/stream"]
                args.base_url = "http://user:synthetic-endpoint-secret@host/private-key"
                args.sources_file = None
                args.bearer_token_env = None
                args.stream_prefix = "acceptance"
                args.output_dir = Path(temporary) / "run"
                args.keep_streams = False
                args.server_config = Path(temporary) / "server.yaml"
                args.model_file = [Path(temporary) / "model.onnx"]
                args.server_config.write_text("token: secret-config-value\n")
                args.model_file[0].write_bytes(b"synthetic weights")
                created, deleted = [], []
                poll_count = 0

                def request(_url, method, path, _timeout, payload=None, **_kwargs):
                    nonlocal poll_count
                    if path == "/ready":
                        return {"status": "ready"}
                    if method == "POST":
                        if failure == "create" and created:
                            raise RuntimeError("creation rejected")
                        created.append(payload["stream_id"])
                        return {"stream_id": created[-1]}
                    if method == "DELETE":
                        deleted.append(path.removeprefix("/streams/"))
                        if failure == "artifact":
                            args.model_file[0].write_bytes(b"changed weights")
                        if failure == "cleanup":
                            raise RuntimeError("synthetic-cleanup-secret")
                        return {"status": "stopped"}
                    if failure == "collect":
                        raise RuntimeError("synthetic-sampling-secret")
                    snapshots = []
                    for stream_id in created:
                        item = snapshot(1 + poll_count * 300, 1 + poll_count * 40,
                                        1 + poll_count * 5, 100.0)
                        item["stream_id"] = stream_id
                        item["source"] = args.source[created.index(stream_id)]
                        item["last_error"] = "synthetic-snapshot-secret"
                        item["unknown"] = {"token": "synthetic-unknown-secret"}
                        snapshots.append(item)
                    poll_count += 1
                    return {"streams": snapshots}

                metadata = MODULE.collect_provenance(args)
                metadata["collector_host"] = provenance_fixture()["collector_host"]
                with mock.patch.object(MODULE, "parse_arguments", return_value=args), \
                     mock.patch.object(MODULE, "collect_provenance", return_value=metadata), \
                     mock.patch.object(MODULE, "request_json", side_effect=request), \
                     mock.patch.object(MODULE.time, "monotonic", side_effect=[0.0, 10.0]), \
                     mock.patch.object(MODULE.time, "sleep"), \
                     mock.patch.object(MODULE, "read_process_sample", side_effect=[
                         MODULE.ProcessSample(0.0, 0, 1000),
                         MODULE.ProcessSample(10.0, 0, 1000),
                     ]), contextlib.redirect_stdout(io.StringIO()) as stdout, \
                     contextlib.redirect_stderr(io.StringIO()) as stderr:
                    code = MODULE.main()
                report = json.loads((args.output_dir / "acceptance.json").read_text())
                self.assertEqual(report["requested_stream_count"], 2)
                self.assertEqual(len(report["streams"]), 2)
                self.assertEqual(deleted, list(reversed(created)))
                self.assertTrue((args.output_dir / "acceptance.md").is_file())
                self.assertEqual(code, 0 if failure is None else 2)
                self.assertEqual(report["overall_status"], "PASS" if failure is None else "FAIL")
                exported = (json.dumps(report) + (args.output_dir / "acceptance.md").read_text()
                            + stdout.getvalue() + stderr.getvalue())
                for secret in ("secret-config-value", "synthetic-source-secret", "query-key",
                               "private-key", "synthetic-endpoint-secret", "synthetic-creation-secret",
                               "synthetic-cleanup-secret", "synthetic-sampling-secret",
                               "synthetic-snapshot-secret", "synthetic-unknown-secret"):
                    self.assertNotIn(secret, exported)
                if failure == "artifact":
                    self.assertFalse(report["provenance"]["artifacts_stable"])
                    self.assertIn("changed during run", report["run_errors"][-1])

    def test_existing_report_directory_does_not_create_streams(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            args = arguments()
            args.source = ["rtsp://camera/stream"]
            args.sources_file = None
            args.bearer_token_env = None
            args.stream_prefix = "acceptance"
            args.output_dir = Path(temporary)
            with mock.patch.object(MODULE, "parse_arguments", return_value=args), \
                 mock.patch.object(MODULE, "request_json") as request, \
                 contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(MODULE.main(), 2)
            request.assert_not_called()


if __name__ == "__main__":
    unittest.main()
