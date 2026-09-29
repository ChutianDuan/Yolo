"""Fixed high8/pool1, low pool4; compare low16 against a fresh low8 control."""
import json
from pathlib import Path

from verify_high12_20260928 import REPO, verify_case, AFFINITY, summarize
from analyze_stream_losses_20260928 import load_window, analyze_window

CASES = {'low16_pool4': ('diag74_node0_low16_pool4', 16),
         'low8_control': ('diag74_node0_low8_control', 8)}


def verify() -> dict:
    rows, reference = {}, None
    for tag, (name, low_threads) in CASES.items():
        case = REPO / 'yolo_onnx_cpp/test_outputs/multistream_local_20260928' / name
        row = verify_case(case, AFFINITY['node0'])
        manifest = json.loads((case / 'diagnostic.json').read_text())
        state = json.loads((case / 'run_state.json').read_text())
        report = json.loads((case / 'acceptance/acceptance.json').read_text())
        assert manifest['configured_high_threads'] == 8
        assert manifest['configured_low_threads'] == low_threads
        assert manifest['configured_high_requests'] == 1
        assert manifest['configured_low_requests'] == 4
        yaml = (case / 'diagnostic-config.yaml').read_text()
        line = f'low_model_threads: {low_threads}\n'
        assert yaml.count(line) == 1
        invariant = {'yaml': yaml.replace(line, 'low_model_threads: VARIABLE\n'),
            'binary': state['service_binary_sha256'], 'code': state['code_sha256'],
            'inputs': manifest['inputs'], 'thresholds': report['thresholds'],
            'models': {item['name']: item['sha256'] for item in report['provenance']['artifacts']
                       if item['kind'] == 'model'}}
        assert len(invariant['models']) == 2
        assert manifest['distinct_source_contents'] == 4 and not manifest['source_content_reused']
        source_fps = [item['delivered_fps'] for item in manifest['source_connections']]
        assert len(source_fps) == 4 and all(29.5 <= fps <= 30.5 for fps in source_fps)
        if reference is None:
            reference = invariant
        assert reference == invariant, tag
        row.update({'configured_low_threads': low_threads, 'runtime_records': manifest['runtime_records'],
            'collection_started_at': state['collection_started_at'], 'stopped_at': state['stopped_at'],
            'source_delivered_fps': source_fps,
            'approximate_post_warmup_diagnostic': summarize(case, row['durable_samples'])['observed_total']})
        window = row['approximate_post_warmup_diagnostic']
        row['per_stream_loss_analysis'] = analyze_window(load_window(row), window['duration_seconds'], sorted(window['per_stream_fps']))
        rows[tag] = row
    a, b = (rows[tag]['runtime_records'] for tag in CASES)
    assert a[0] == b[0], 'high runtime changed'
    assert {k: v for k, v in a[1].items() if k != 'properties'} == {k: v for k, v in b[1].items() if k != 'properties'}
    assert a[1]['properties']['INFERENCE_NUM_THREADS'] == '16'
    assert b[1]['properties']['INFERENCE_NUM_THREADS'] == '8'
    differences = {key: [a[1]['properties'].get(key), b[1]['properties'].get(key)]
                   for key in a[1]['properties'].keys() | b[1]['properties'].keys()
                   if a[1]['properties'].get(key) != b[1]['properties'].get(key)}
    return {'scope': 'node0 local four-stream low16/pool4 vs fresh low8/pool4; only configured low threads differ; native properties retained',
            'only_configured_low_threads_differs': True, 'low_runtime_property_differences': differences, 'cases': rows}


if __name__ == '__main__':
    print(json.dumps(verify(), indent=2))
