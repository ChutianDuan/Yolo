"""Read-only live identity checks and final audit of the local resource soak."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import subprocess
import time
from pathlib import Path

from multistream_acceptance import stream_summary
from multistream_resource_probe import interval, process_stat

PREFIXES = ('yolo_stream_async_', 'yolo_scheduler_updated_total',
            'yolo_scheduler_cancelled_total', 'yolo_stream_processing_stage_', 'yolo_stream_weak_flow_')


def read_json(path: Path) -> dict:
    return json.loads(path.read_text())


def sha256(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            sha.update(chunk)
    return sha.hexdigest()


def owned_alive(pid: int, start: int) -> bool:
    try:
        stat = process_stat(Path('/proc') / str(pid) / 'stat')
    except FileNotFoundError:
        return False
    return stat['state'] != 'Z' and stat['start_ticks'] == start


def audit(case: Path, live: bool = False) -> dict:
    state, progress = read_json(case / 'run_state.json'), read_json(case / 'progress.json')
    binary = Path(state['service_binary'])
    assert sha256(binary) == state['service_binary_sha256']
    alive = {name: owned_alive(state[name + '_pid'], state[name + '_start_ticks'])
             for name in ('driver', 'service', 'collector')}
    assert state['stream_count'] in (4, 6, 8) and state['resource_schema_version'] == 1
    if live:
        assert state['state'] == 'collecting' and all(alive.values()), (state['state'], alive)
        assert (Path('/proc') / str(state['service_pid']) / 'exe').resolve() == binary.resolve()
        listeners = subprocess.check_output(['ss', '-ltnp', 'sport = :8080'], text=True)
        assert {int(pid) for pid in re.findall(r'pid=(\d+)', listeners)} == {state['service_pid']}
        age = time.monotonic() - progress['metrics_monotonic_seconds']
        assert 0 <= age < 30
        return {'state': 'RUNNING', 'case': str(case), 'owned_processes_alive': alive,
                'stream_count': state['stream_count'], 'requested_seconds': state['requested_seconds'],
                'collection_started_at': state['collection_started_at'], 'sample_count': progress['sample_count'],
                'latest_sample_age_seconds': age, 'memory': progress['memory']}
    assert state['state'] == 'finished' and state['run_error'] is None and not any(alive.values())
    if 'code_sha256' in state:
        assert state['code_sha256_after'] == state['code_sha256']
    manifest, timing = read_json(case / 'diagnostic.json'), read_json(case / 'timing_metrics.json')
    report = read_json(case / 'acceptance/acceptance.json')
    assert state['service_returncode'] == manifest['server_returncode'] == 0
    assert manifest['service_binary_sha256'] == manifest['service_binary_sha256_after'] == state['service_binary_sha256']
    assert manifest['actual_executable_verified'] and manifest['resource_schema_version'] == 1
    assert manifest['detection_mode'] == 'high_low'
    assert sha256(case / 'diagnostic-config.yaml') == state['config_sha256']
    assert len(manifest['source_connections']) == manifest['stream_count'] == state['stream_count']
    assert manifest['requested_seconds'] == state['requested_seconds'] == report['requested_duration_seconds']
    assert manifest['requested_warmup_seconds'] == state['warmup_seconds']
    assert report['elapsed_seconds'] >= state['requested_seconds']
    assert report['sampling_warmup']['completed'] and not report['sampling_warmup']['errors']
    assert not timing['errors'] and not manifest['timing_metrics_errors'] and not report['run_errors']
    assert all(a['unchanged'] and a['sha256'] == a['sha256_after'] for a in report['provenance']['artifacts'])
    assert len(report['streams']) == report['requested_stream_count'] == state['stream_count']
    assert len(report['samples']) == report['sample_count']
    assert report['samples'][-1]['observed_at_seconds'] == report['elapsed_seconds']
    for row in report['streams']:
        assert stream_summary(row['stream_id'], report['samples'], report['elapsed_seconds']) == row
        assert row['inference_errors_max'] == row['reconnects_delta'] == 0
    all_passed = all(g['passed'] is True for g in report['gates'])
    assert report['overall_status'] == ('PASS' if all_passed else 'FAIL')
    assert state['collector_returncode'] == manifest['collector_returncode'] == (0 if all_passed else 2)
    previous, per_stream, selected = {}, {}, None
    first = last = None
    weighted, maximum_other, min_other = {}, float('-inf'), float('inf')
    resource_seconds, peak_rss, peak_probe, maximum_gap, count = 0.0, 0, 0.0, 0.0, 0
    cgroup_endpoints = []
    sha = hashlib.sha256()
    with (case / 'live_samples.jsonl').open('rb') as source:
        for raw in source:
            assert raw.endswith(b'\n')
            sha.update(raw)
            sample = json.loads(raw)
            count += 1
            assert sample['index'] == count
            now = sample['metrics']['monotonic_seconds']
            memory, resources = sample['memory'], sample['resources']
            assert 0 <= memory['monotonic_seconds'] - now < 3
            assert resources['service_pid'] == state['service_pid']
            assert resources['service']['start_ticks'] == state['service_start_ticks']
            assert resources['driver']['start_ticks'] == state['driver_start_ticks']
            assert resources['service_cpu_affinity'] == report['provenance']['collector_host']['monitored_pid_cpu_affinity']
            assert resources['schema_version'] == 1 and resources['observation_seconds'] >= 0
            peak_probe = max(peak_probe, resources['observation_seconds'])
            peak_rss = max(peak_rss, memory['Rss_kib'])
            if first is None:
                first = sample
                cgroup_endpoints.append(resources['cpu_cgroups'])
            if last is not None:
                gap = now - last['metrics']['monotonic_seconds']
                assert gap > 0
                maximum_gap = max(maximum_gap, gap)
                observed = interval(last['resources'], resources)
                elapsed = observed['elapsed_seconds']
                resource_seconds += elapsed
                for key in ('host_busy_percent', 'service_cpu_percent_of_host', 'driver_cpu_percent_of_host',
                            'host_busy_minus_service_and_driver_percent'):
                    weighted[key] = weighted.get(key, 0) + observed[key] * elapsed
                other = observed['host_busy_minus_service_and_driver_percent']
                maximum_other, min_other = max(maximum_other, other), min(min_other, other)
            values = {}
            for line in sample['metrics']['prometheus_text'].splitlines():
                if not line or line.startswith('#'):
                    continue
                key, raw_value = line.rsplit(' ', 1)
                value = float(raw_value)
                if 'stream_id=' not in key:
                    values[key] = value
                elif key.startswith(PREFIXES):
                    assert math.isfinite(value) and value >= per_stream.get(key, 0)
                    per_stream[key] = value
            if selected is None:
                selected = [key for key in values if key.startswith(PREFIXES)]
                assert len(selected) == 111
            assert all(math.isfinite(values[key]) and values[key] >= previous.get(key, 0) for key in selected)
            previous, last = values, sample
    assert count == state['samples'] == timing['sample_count'] == manifest['memory_sample_count'] == progress['sample_count']
    assert count == manifest['timing_metrics_sample_count'] and maximum_gap <= 10
    assert last['metrics']['monotonic_seconds'] == progress['metrics_monotonic_seconds']
    assert last['metrics']['monotonic_seconds'] - first['metrics']['monotonic_seconds'] >= state['requested_seconds']
    assert previous['yolo_streams_active'] == previous['yolo_streams_registered'] == 0
    for tier in ('high', 'low'):
        for key in ('queue_depth', 'in_flight'):
            assert previous[f'yolo_scheduler_{key}{{tier="{tier}"}}'] == 0
            assert manifest['retirement_evidence']['scheduler'][tier][key] == 0
    assert manifest['retirement_evidence']['active'] == manifest['retirement_evidence']['registered'] == 0
    cgroup_endpoints.append(last['resources']['cpu_cgroups'])
    return {'case': str(case), 'scope': 'local MJPEG, resource means include warmup and retirement',
        'original_acceptance_status': report['overall_status'],
        'failed_gates': [g['name'] for g in report['gates'] if g['passed'] is not True],
        'duration_seconds': report['elapsed_seconds'], 'acceptance_samples': report['sample_count'],
        'durable_samples': count, 'selected_global_monotonic_series': len(selected),
        'metrics_maximum_gap_seconds': maximum_gap, 'peak_probe_seconds': peak_probe,
        'rates': {key: [min(row[key] for row in report['streams']), max(row[key] for row in report['streams'])]
                  for key in ('processed_fps', 'low_detection_fps', 'high_detection_fps')},
        'maximum_p95_ms': max(row['result_age_ms_p95'] for row in report['streams']),
        'fairness_percent': report['fairness_spread_percent'], 'process': report['process'],
        'peak_rss_kib': peak_rss, 'resource_weighted_means': {key: value / resource_seconds for key, value in weighted.items()},
        'resource_other_busy_range_percent': [min_other, maximum_other],
        'cpu_cgroup_endpoints': cgroup_endpoints,
        'numa_pages_start_end': [first['resources']['numa_mapping_pages_by_node'], last['resources']['numa_mapping_pages_by_node']],
        'service_binary_sha256': state['service_binary_sha256'], 'journal_sha256': sha.hexdigest(),
        'retirement_evidence': manifest['retirement_evidence'], 'owned_processes_alive': alive}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('case', type=Path)
    parser.add_argument('--live', action='store_true')
    args = parser.parse_args()
    print(json.dumps(audit(args.case.resolve(), args.live), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
