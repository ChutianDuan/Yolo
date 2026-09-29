"""Read-only per-stream accounting on six frozen node0 diagnostic windows."""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path

from verify_high12_20260928 import verify as verify_high12
from verify_high_threads_20260928 import verify as verify_high16
from summarize_multistream_soak_windows import parse_sample

STAGES = ('preprocess', 'queue_wait', 'execution', 'infer', 'postprocess',
          'completion_pickup', 'result_age', 'replay', 'commit_age')
PROCESSING = ('frame_work', 'prepare', 'poll', 'publish', 'weak_flow')
OUTCOMES = ('completed', 'applied', 'expired', 'evicted')
SCHEDULER = ('submitted', 'completed', 'replaced', 'updated', 'stale', 'failed', 'cancelled')


def metrics(sample: dict) -> dict[str, float]:
    result = {}
    for line in sample['metrics']['prometheus_text'].splitlines():
        if not line or line.startswith('#'):
            continue
        key, raw = line.rsplit(' ', 1)
        assert key not in result, key
        result[key] = float(raw)
        assert math.isfinite(result[key]) and result[key] >= 0, key
    return result


def analyze_window(samples: list[dict], seconds: float, stream_ids: list[str]) -> dict:
    assert len(samples) > 1 and seconds > 0
    parsed = [metrics(sample) for sample in samples]
    cumulative = ('yolo_stream_async_', 'yolo_stream_processing_stage_', 'yolo_scheduler_')
    for before, after in zip(parsed, parsed[1:]):
        for key, value in before.items():
            if key.startswith(cumulative) and ('_total' in key or '_seconds_' in key):
                assert key in after and after[key] >= value, ('reset or missing', key)
    start, end = parsed[0], parsed[-1]

    def delta(key: str) -> float:
        value = end[key] - start[key]
        assert math.isfinite(value) and value >= 0, key
        return value

    def stage(prefix: str, labels: str) -> dict:
        count = delta(prefix + '_count' + labels)
        assert count.is_integer()
        total = delta(prefix + '_sum' + labels)
        assert count > 0 or total == 0
        return {'count': int(count), 'mean_ms': 1000 * total / count if count else None}

    rows = {}
    for sid in stream_ids:
        row = {'processing': {}, 'tiers': {}}
        for name in PROCESSING:
            labels = f'{{stage="{name}",stream_id="{sid}"}}'
            row['processing'][name] = stage('yolo_stream_processing_stage_by_stream_seconds', labels)
        for tier in ('high', 'low'):
            counts = {outcome: delta(f'yolo_stream_async_results_by_stream_total'
                      f'{{tier="{tier}",outcome="{outcome}",stream_id="{sid}"}}')
                      for outcome in OUTCOMES}
            assert all(value.is_integer() for value in counts.values())
            counts = {key: int(value) for key, value in counts.items()}
            assert counts['completed'] == sum(counts[k] for k in ('applied', 'expired', 'evicted'))
            stages = {name: stage('yolo_stream_async_stage_by_stream_seconds',
                      f'{{tier="{tier}",stage="{name}",stream_id="{sid}"}}') for name in STAGES}
            assert all(stages[name]['count'] == counts['completed'] for name in STAGES[:7])
            assert stages['replay']['count'] == stages['commit_age']['count']
            before_replay = counts['completed'] - counts['evicted'] - stages['replay']['count']
            during_replay = stages['replay']['count'] - counts['applied']
            assert before_replay >= 0 and during_replay >= 0
            assert before_replay + during_replay == counts['expired']
            detection_key = f'yolo_stream_{tier}_detections_by_stream_total{{stream_id="{sid}"}}'
            published = int(delta(detection_key))
            outcome_key = (f'yolo_stream_async_results_by_stream_total'
                           f'{{tier="{tier}",outcome="applied",stream_id="{sid}"}}')
            publication_gap = [item[outcome_key] - item[detection_key] for item in (start, end)]
            assert all(value >= 0 and value.is_integer() for value in publication_gap)
            assert counts['applied'] - published == publication_gap[1] - publication_gap[0]
            row['tiers'][tier] = {'counts': counts, 'fps': {k: v / seconds for k, v in counts.items()},
                'published_count': published, 'published_fps': published / seconds,
                'applied_minus_published_endpoints': publication_gap,
                'expired_before_replay': before_replay, 'expired_during_replay': during_replay,
                'stages': stages}
        rows[sid] = row
    scheduler = {}
    for tier in ('high', 'low'):
        for outcome in OUTCOMES:
            assert sum(row['tiers'][tier]['counts'][outcome] for row in rows.values()) == delta(
                f'yolo_stream_async_results_total{{tier="{tier}",outcome="{outcome}"}}')
        scheduler[tier] = {name: int(delta(f'yolo_scheduler_{name}_total{{tier="{tier}"}}'))
                           for name in SCHEDULER}
        scheduler[tier]['sampled_max_queue'] = max(item[f'yolo_scheduler_queue_depth{{tier="{tier}"}}'] for item in parsed)
        scheduler[tier]['sampled_max_in_flight'] = max(item[f'yolo_scheduler_in_flight{{tier="{tier}"}}'] for item in parsed)
    return {'duration_seconds': seconds, 'samples': len(samples), 'streams': rows, 'scheduler': scheduler}


def load_window(row: dict) -> list[dict]:
    raw = (Path(row['case']) / 'live_samples.jsonl').read_bytes()
    assert hashlib.sha256(raw).hexdigest() == row['journal_sha256']
    samples = [json.loads(line) for line in raw.splitlines()]
    full = [sample for sample in samples if len(parse_sample(sample)['streams']) == 4]
    origin = full[0]['metrics']['monotonic_seconds']
    window = row['approximate_post_warmup_diagnostic']
    start = origin + window['from_seconds']
    end = origin + window['to_seconds']
    selected = [sample for sample in full if start - 1e-6 <= sample['metrics']['monotonic_seconds'] <= end + 1e-6]
    assert abs(selected[0]['metrics']['monotonic_seconds'] - start) < 1e-6
    assert abs(selected[-1]['metrics']['monotonic_seconds'] - end) < 1e-6
    ids = set(window['per_stream_fps'])
    assert all(set(parse_sample(sample)['streams']) == ids for sample in selected)
    return selected


def analyze() -> dict:
    references = {f'71_{k}': v for k, v in verify_high16()['cases'].items() if k != 'previous_high8'}
    references.update({f'72_{k}': v for k, v in verify_high12()['cases'].items()})
    cases = {}
    for tag, row in references.items():
        window = row['approximate_post_warmup_diagnostic']
        result = analyze_window(load_window(row), window['duration_seconds'], sorted(window['per_stream_fps']))
        result.update({'case': row['case'], 'journal_sha256': row['journal_sha256'],
                       'original_acceptance_status': row['original_acceptance_status'],
                       'original_failed_gates': row['failed_gates'],
                       'original_formal_rates': row['rates']})
        for sid, stream in result['streams'].items():
            for tier in ('high', 'low'):
                assert abs(stream['tiers'][tier]['published_fps'] - window['per_stream_fps'][sid][tier + '_detections']) < 1e-10
        cases[tag] = result
    return {'scope': 'Existing approximately 51-second post-warmup windows; not formal 60-second acceptance or causal attribution. Stage means include received completed results; execution includes infer. Parent processing stages overlap. No window percentiles or maxima inferred from cumulative maxima.',
            'cases': cases}


if __name__ == '__main__':
    print(json.dumps(analyze(), indent=2))
