"""Run the 60-second local fixture without a reader on the driver's stdout pipe."""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--service-binary', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    case = root / 'diag64_closed_stdout_60s'
    assert not case.exists(), 'never overwrite a previous run'
    driver = root / 'diagnostic_driver_soak_filefirst_20260922.py'
    command = [sys.executable, str(driver), '--repo', str(root.parents[2]),
               '--service-binary', str(args.service_binary), '--work-dir', str(args.work_dir),
               '--output-dir', str(case), '--seconds', '60', '--warmup-seconds', '10',
               '--sample-interval-seconds', '5', '--high-threads', '16', '--low-threads', '16',
               '--high-requests', '1', '--low-requests', '2', '--opencv-threads', '4',
               '--cpu-pinning', 'false', '--low-detect-fps', '5', '--mode', 'throughput']
    with (root / 'closed_stdout_smoke_stderr_20260922.log').open('x') as errors:
        child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=errors)
        assert child.stdout is not None
        child.stdout.close()
        # The driver already bounds collection and cleans up its own children.
        returncode = child.wait(timeout=240)
    state = json.loads((case / 'run_state.json').read_text())
    manifest = json.loads((case / 'diagnostic.json').read_text())
    report = json.loads((case / 'acceptance/acceptance.json').read_text())
    assert returncode == 0 and state['state'] == 'finished' and state['run_error'] is None
    assert state['service_returncode'] == 0
    assert state['collector_returncode'] == (0 if report['overall_status'] == 'PASS' else 2)
    retired = manifest['retirement_evidence']
    assert retired['active'] == retired['registered'] == 0
    assert all(tier['queue_depth'] == tier['in_flight'] == 0 for tier in retired['scheduler'].values())
    evidence = {'case': case.name, 'driver_stdout_reader_closed_before_completion': True,
                'driver_returncode': returncode, 'driver_sha256': hashlib.sha256(driver.read_bytes()).hexdigest(),
                'state': state['state'], 'original_acceptance_status': report['overall_status'],
                'retirement_evidence': retired}
    with (root / 'closed_stdout_smoke_summary_20260922.json').open('x') as output:
        output.write(json.dumps(evidence, indent=2) + '\n')
    print(json.dumps(evidence))


if __name__ == '__main__':
    main()
