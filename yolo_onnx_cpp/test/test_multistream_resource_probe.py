from __future__ import annotations

import copy
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('multistream_resource_probe',
    Path(__file__).resolve().parents[1] / 'tools/multistream_resource_probe.py')
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def sample() -> dict:
    return {'monotonic_seconds': 10.0, 'service_pid': 5, 'clock_ticks_per_second': 100,
            'host_logical_cpus': 4, 'service': {'start_ticks': 10, 'cpu_ticks': 200},
            'driver': {'start_ticks': 2, 'cpu_ticks': 100},
            'host_cpu_ticks_first_eight': [100, 0, 100, 300, 0, 0, 0, 0],
            'threads': {'5': {'start_ticks': 10, 'cpu_ticks': 200, 'last_cpu': 0}}}


class ResourceProbeTest(unittest.TestCase):
    def test_proc_stat_with_parentheses_and_spaces(self) -> None:
        fields = ['0'] * 37
        fields[0], fields[7], fields[9] = 'S', '7', '2'
        fields[11], fields[12], fields[19], fields[36] = '100', '20', '88', '3'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'stat'
            path.write_text('42 (worker (long) name) ' + ' '.join(fields))
            self.assertEqual(MODULE.process_stat(path), {'state': 'S', 'cpu_ticks': 120,
                'start_ticks': 88, 'last_cpu': 3, 'minor_faults': 7, 'major_faults': 2})

    def test_host_normalization_and_endpoint_migration(self) -> None:
        first, last = sample(), sample()
        last['monotonic_seconds'] = 12
        last['host_cpu_ticks_first_eight'] = [300, 0, 300, 700, 0, 0, 0, 0]
        last['service']['cpu_ticks'] += 200
        last['driver']['cpu_ticks'] += 40
        last['threads']['5']['cpu_ticks'] += 200
        last['threads']['5']['last_cpu'] = 2
        result = MODULE.interval(first, last)
        self.assertEqual(result['host_busy_percent'], 50)
        self.assertEqual(result['service_cpu_percent_of_host'], 25)
        self.assertEqual(result['driver_cpu_percent_of_host'], 5)
        self.assertEqual(result['host_busy_minus_service_and_driver_percent'], 20)
        self.assertEqual(result['active_threads_with_different_endpoint_cpu'], 1)
        last['threads']['5']['start_ticks'] += 1
        self.assertEqual(MODULE.interval(first, last)['active_threads_with_different_endpoint_cpu'], 0)

    def test_restart_reset_and_invalid_time_are_rejected(self) -> None:
        first, good = sample(), sample()
        good['monotonic_seconds'] += 2
        good['host_cpu_ticks_first_eight'][0] += 10
        for field in ('service_pid', 'monotonic_seconds', 'host_logical_cpus', 'clock_ticks_per_second'):
            bad = copy.deepcopy(good)
            bad[field] = 0
            with self.subTest(field=field), self.assertRaises(ValueError):
                MODULE.interval(first, bad)
        for name, key, value in [('service', 'start_ticks', 11), ('driver', 'start_ticks', 3),
                                  ('service', 'cpu_ticks', 0), ('driver', 'cpu_ticks', 0)]:
            bad = copy.deepcopy(good)
            bad[name][key] = value
            with self.subTest(name=name, key=key), self.assertRaises(ValueError):
                MODULE.interval(first, bad)
        bad = copy.deepcopy(good)
        bad['host_cpu_ticks_first_eight'][3] = 0
        with self.assertRaises(ValueError):
            MODULE.interval(first, bad)

    def test_real_owned_process_and_wrong_identity(self) -> None:
        pid = os.getpid()
        start = MODULE.process_stat(Path('/proc/self/stat'))['start_ticks']
        binary = Path('/proc/self/exe').resolve()
        row = MODULE.snapshot(pid, start, binary)
        self.assertEqual(row['service_pid'], pid)
        self.assertEqual(row['service']['start_ticks'], start)
        self.assertIn(str(pid), row['threads'])
        self.assertEqual(len(row['host_cpu_ticks_first_eight']), 8)
        self.assertGreaterEqual(row['observation_seconds'], 0)
        with self.assertRaises(RuntimeError):
            MODULE.snapshot(pid, start + 1, binary)
        with self.assertRaises(RuntimeError):
            MODULE.snapshot(pid, start, binary.parent / 'wrong-executable')


if __name__ == '__main__':
    unittest.main()
