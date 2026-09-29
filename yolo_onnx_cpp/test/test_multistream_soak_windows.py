from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location('soak_windows', TOOLS / 'summarize_multistream_soak_windows.py')
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def sample(index: int, second: int, retired: bool = False) -> dict:
    lines = []
    if not retired:
        for sid in range(4):
            for key, rate in [('processed_frames', 30), ('low_detections', 5), ('high_detections', 1)]:
                lines.append(f'yolo_stream_{key}_by_stream_total{{stream_id="camera-{sid}"}} {second * rate}')
    for stage in ('weak_flow', 'prepare'):
        lines.extend([f'yolo_stream_processing_stage_seconds_count{{stage="{stage}"}} {120 * second}',
                      f'yolo_stream_processing_stage_seconds_sum{{stage="{stage}"}} {2.4 * second}'])
    for tier, rate, mean in [('high', 4, 0.12), ('low', 20, 0.05)]:
        lines.extend([f'yolo_stream_async_stage_seconds_count{{tier="{tier}",stage="infer"}} {rate * second}',
                      f'yolo_stream_async_stage_seconds_sum{{tier="{tier}",stage="infer"}} {mean * rate * second}',
                      f'yolo_stream_async_results_total{{tier="{tier}",outcome="applied"}} {rate * second}',
                      f'yolo_stream_async_results_total{{tier="{tier}",outcome="expired"}} 0'])
    resources = {'monotonic_seconds': second + 0.01, 'service_pid': 42,
        'clock_ticks_per_second': 100, 'host_logical_cpus': 4,
        'service': {'start_ticks': 3, 'cpu_ticks': 100 * second},
        'driver': {'start_ticks': 2, 'cpu_ticks': 20 * second}, 'threads': {},
        'host_cpu_ticks_first_eight': [100 * second, 0, 100 * second, 200 * second, 0, 0, 0, 0],
        'cpu_cgroups': {'test': {'cpu.cfs_quota_us': '-1', 'cpu.cfs_period_us': '100000',
                               'cpu.stat': f'nr_throttled {second}\nthrottled_time {second * 1000}'}},
        'numa_mapping_pages_by_node': {'0': 10}, 'scaling_frequency_khz': {'by_cpu': {'cpu0': 2000000}}}
    return {'index': index, 'metrics': {'monotonic_seconds': second, 'prometheus_text': '\n'.join(lines)},
            'memory': {'Rss_kib': 1000 + second}, 'resources': resources}


class SoakWindowsTest(unittest.TestCase):
    def write_case(self, directory: str, rows: list[dict], partial: bool = False) -> Path:
        case = Path(directory)
        (case / 'run_state.json').write_text(json.dumps({'stream_count': 4, 'warmup_seconds': 60}))
        text = ''.join(json.dumps(row) + '\n' for row in rows)
        (case / 'live_samples.jsonl').write_text(text.rstrip('\n') if partial else text)
        return case

    def test_complete_window_tail_and_exact_prefix(self) -> None:
        rows = [sample(i, second) for i, second in enumerate((0, 60, 120, 360, 420), 1)]
        with tempfile.TemporaryDirectory() as directory:
            case = self.write_case(directory, rows)
            result = MODULE.summarize(case, 5)
            self.assertEqual(result['complete_windows'], 1)
            self.assertTrue(all(value == 0 for value in result['failed_window_counts'].values()))
            window = result['windows'][0]
            self.assertEqual((window['from_seconds'], window['to_seconds']), (60, 360))
            self.assertEqual(window['fps_ranges']['low_detections'], [5, 5])
            self.assertAlmostEqual(window['inference']['high']['mean_ms'], 120)
            self.assertAlmostEqual(window['inference']['low']['mean_ms'], 50)
            self.assertEqual(window['resources']['host_busy_percent'], 50)
            self.assertEqual(window['resources']['service_cpu_percent_of_host'], 25)
            self.assertEqual(window['cgroup_throttling']['test']['deltas']['nr_throttled'], 300)
            self.assertEqual(result['unfinished_tail']['duration_seconds'], 60)
            prefix = MODULE.summarize(case, 4)
            with (case / 'live_samples.jsonl').open('a') as output:
                output.write(json.dumps(sample(6, 480)) + '\n')
            self.assertEqual(MODULE.summarize(case, 4), prefix)

    def test_incomplete_or_too_short_prefix_is_rejected(self) -> None:
        rows = [sample(1, 0), sample(2, 60)]
        with tempfile.TemporaryDirectory() as directory:
            case = self.write_case(directory, rows)
            with self.assertRaises(AssertionError):
                MODULE.summarize(case, 2)
            with self.assertRaises(AssertionError):
                MODULE.summarize(case, 3)
            self.write_case(directory, rows, partial=True)
            with self.assertRaises(AssertionError):
                MODULE.summarize(case, 2)

    def test_counter_reset_and_process_reuse_are_rejected(self) -> None:
        original = [sample(1, 0), sample(2, 60), sample(3, 120)]
        with tempfile.TemporaryDirectory() as directory:
            rows = copy.deepcopy(original)
            rows[-1]['metrics']['prometheus_text'] = rows[-1]['metrics']['prometheus_text'].replace(
                'stream_id="camera-0"} 3600', 'stream_id="camera-0"} 0')
            with self.assertRaises(AssertionError):
                MODULE.summarize(self.write_case(directory, rows), 3)
            rows = copy.deepcopy(original)
            rows[-1]['resources']['service']['start_ticks'] += 1
            with self.assertRaises(ValueError):
                MODULE.summarize(self.write_case(directory, rows), 3)

    def test_intermediate_global_reset_cannot_be_hidden_by_recovery(self) -> None:
        rows = [sample(i, second) for i, second in enumerate((0, 60, 120, 360), 1)]
        rows[2]['metrics']['prometheus_text'] = rows[2]['metrics']['prometheus_text'].replace(
            'stage="weak_flow"} 14400', 'stage="weak_flow"} 1')
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(AssertionError):
                MODULE.summarize(self.write_case(directory, rows), 4)

    def test_retirement_excluded_and_reappearance_rejected(self) -> None:
        rows = [sample(1, 0), sample(2, 60), sample(3, 120), sample(4, 130, retired=True)]
        with tempfile.TemporaryDirectory() as directory:
            result = MODULE.summarize(self.write_case(directory, rows), 4)
            self.assertTrue(result['retirement_seen'])
            self.assertEqual(result['observed_total']['to_seconds'], 120)
            rows.append(sample(5, 140))
            with self.assertRaises(AssertionError):
                MODULE.summarize(self.write_case(directory, rows), 5)


if __name__ == '__main__':
    unittest.main()
