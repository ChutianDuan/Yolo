"""Fixed-prefix local-soak diagnostics; approximate warmup, not final acceptance."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path

from multistream_resource_probe import interval

STREAM = re.compile(r'yolo_stream_(processed_frames|low_detections|high_detections)_by_stream_total\{stream_id="([^"]+)"\}')
KINDS = ('processed_frames', 'low_detections', 'high_detections')


def parse_sample(sample: dict) -> dict:
    streams, values = {}, {}
    for line in sample['metrics']['prometheus_text'].splitlines():
        if not line or line.startswith('#'):
            continue
        key, raw = line.rsplit(' ', 1)
        match = STREAM.fullmatch(key)
        if match:
            streams.setdefault(match[2], {})[match[1]] = int(raw)
        elif 'stream_id=' not in key:
            values[key] = float(raw)
    return {'time': sample['metrics']['monotonic_seconds'], 'streams': streams, 'global': values,
            'rss_kib': sample['memory']['Rss_kib'], 'resources': sample['resources']}


def window(start: dict, end: dict, origin: float, peak_rss: int) -> dict:
    duration = end['time'] - start['time']
    assert duration > 0 and start['streams'].keys() == end['streams'].keys()
    rates = {}
    for sid, initial in start['streams'].items():
        rates[sid] = {}
        for kind, before in initial.items():
            difference = end['streams'][sid][kind] - before
            assert difference >= 0
            rates[sid][kind] = difference / duration
    ranges = {key: [min(row[key] for row in rates.values()), max(row[key] for row in rates.values())]
              for key in KINDS}
    slow, fast = ranges['processed_frames']
    fairness = 100 * (fast - slow) / fast if fast else None

    def delta(key: str) -> float:
        value = end['global'][key] - start['global'][key]
        assert math.isfinite(value) and value >= 0
        return value

    def mean(prefix: str, labels: str) -> float | None:
        count = delta(prefix + '_count' + labels)
        return 1000 * delta(prefix + '_sum' + labels) / count if count else None

    throttling = {}
    before_groups, after_groups = start['resources']['cpu_cgroups'], end['resources']['cpu_cgroups']
    for group in before_groups.keys() & after_groups.keys():
        a, b = before_groups[group], after_groups[group]
        if a.get('cpu.stat') is None or b.get('cpu.stat') is None:
            continue
        if (a.get('cpu.cfs_quota_us'), a.get('cpu.cfs_period_us')) != (b.get('cpu.cfs_quota_us'), b.get('cpu.cfs_period_us')):
            throttling[group] = {'scope_changed': True}
            continue
        first = {key: int(value) for key, value in (line.split() for line in a['cpu.stat'].splitlines())}
        last = {key: int(value) for key, value in (line.split() for line in b['cpu.stat'].splitlines())}
        changes = {key: last[key] - first[key] for key in first.keys() & last.keys()}
        assert all(value >= 0 for value in changes.values())
        throttling[group] = {'scope_changed': False, 'deltas': changes,
                             'quota_us': b.get('cpu.cfs_quota_us'), 'period_us': b.get('cpu.cfs_period_us')}
    return {
        'from_seconds': start['time'] - origin, 'to_seconds': end['time'] - origin,
        'duration_seconds': duration, 'fps_ranges': ranges, 'per_stream_fps': rates,
        'fairness_percent': fairness,
        'diagnostic_rate_checks': {'output_ge_25': slow >= 25, 'low_ge_4': ranges['low_detections'][0] >= 4,
            'high_ge_0_5': ranges['high_detections'][0] >= 0.5, 'fairness_le_10': fairness is not None and fairness <= 10},
        'rss_kib': {'start': start['rss_kib'], 'end': end['rss_kib'], 'peak': peak_rss},
        'weak_flow_mean_ms': mean('yolo_stream_processing_stage_seconds', '{stage="weak_flow"}'),
        'prepare_mean_ms': mean('yolo_stream_processing_stage_seconds', '{stage="prepare"}'),
        'inference': {tier: {
            'mean_ms': mean('yolo_stream_async_stage_seconds', f'{{tier="{tier}",stage="infer"}}'),
            'applied': int(delta(f'yolo_stream_async_results_total{{tier="{tier}",outcome="applied"}}')),
            'expired': int(delta(f'yolo_stream_async_results_total{{tier="{tier}",outcome="expired"}}'))}
            for tier in ('high', 'low')},
        'resources': interval(start['resources'], end['resources']),
        'cgroup_throttling': throttling,
        'numa_mapping_pages_start_end': [start['resources']['numa_mapping_pages_by_node'], end['resources']['numa_mapping_pages_by_node']],
        'frequency_endpoints': [start['resources']['scaling_frequency_khz'], end['resources']['scaling_frequency_khz']],
    }


def summarize(case: Path, requested_samples: int) -> dict:
    assert requested_samples > 1
    state = json.loads((case / 'run_state.json').read_text())
    wanted = state['stream_count']
    assert wanted in (4, 6, 8)
    sha = hashlib.sha256()
    count = peak_rss = current_peak = 0
    first_full = steady = start = last = previous = None
    windows = []
    retirement_seen = False
    with (case / 'live_samples.jsonl').open('rb') as journal:
        for raw in journal:
            if count == requested_samples:
                break
            assert raw.endswith(b'\n'), 'requested prefix includes an incomplete record'
            sha.update(raw)
            sample = json.loads(raw)
            count += 1
            assert sample['index'] == count
            current = parse_sample(sample)
            assert math.isfinite(current['time'])
            if previous is not None:
                assert current['time'] > previous['time']
                interval(previous['resources'], current['resources'])
                if len(previous['streams']) == len(current['streams']) == wanted:
                    assert previous['streams'].keys() == current['streams'].keys()
                cumulative = ('yolo_stream_async_', 'yolo_stream_processing_stage_', 'yolo_stream_weak_flow_')
                assert all(math.isfinite(value) and value >= previous['global'].get(key, 0)
                           for key, value in current['global'].items() if key.startswith(cumulative))
                if current['streams'].keys() == previous['streams'].keys():
                    assert all(current['streams'][sid][key] >= row[key]
                               for sid, row in previous['streams'].items() for key in KINDS)
            previous = current
            if len(current['streams']) != wanted:
                if first_full is not None:
                    retirement_seen = True
                continue
            assert not retirement_seen, 'streams reappeared after partial retirement'
            assert all(set(row) == set(KINDS) for row in current['streams'].values())
            if first_full is None:
                first_full = current['time']
            if current['time'] - first_full < state['warmup_seconds']:
                continue
            peak_rss = max(peak_rss, current['rss_kib'])
            current_peak = max(current_peak, current['rss_kib'])
            if start is None:
                start = steady = current
            elif current['time'] - start['time'] >= 300:
                windows.append(window(start, current, first_full, current_peak))
                start, current_peak = current, current['rss_kib']
            last = current
    assert count == requested_samples and steady is not None and last is not None
    assert last['time'] > steady['time'], 'prefix does not cover post-warmup progress'
    return {
        'scope': 'fixed prefix; five-minute rates/resources only, not formal acceptance or causal proof',
        'warmup_scope': 'first all-stream metric plus configured warmup; approximate collector boundary',
        'case': str(case.resolve()), 'stream_count': wanted, 'samples': count,
        'journal_prefix_sha256': sha.hexdigest(), 'last_full_stream_monotonic_seconds': last['time'],
        'retirement_seen': retirement_seen, 'complete_windows': len(windows),
        'failed_window_counts': {key: sum(not row['diagnostic_rate_checks'][key] for row in windows)
                                for key in ('output_ge_25', 'low_ge_4', 'high_ge_0_5', 'fairness_le_10')},
        'observed_total': window(steady, last, first_full, peak_rss), 'windows': windows,
        'unfinished_tail': window(start, last, first_full, current_peak) if last['time'] > start['time'] else None,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('case', type=Path)
    parser.add_argument('--samples', type=int, required=True)
    args = parser.parse_args()
    print(json.dumps(summarize(args.case, args.samples), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
