"""Launch the bounded local soak in its own session with persistent file logs."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import sys


def launch(output_dir: Path, driver_arguments: list[str]) -> dict:
    output_dir = output_dir.resolve()
    if output_dir.exists():
        raise FileExistsError('refusing to overwrite a prior soak')
    if any(arg == '--output-dir' or arg.startswith('--output-dir=') for arg in driver_arguments):
        raise ValueError('output directory must be supplied only to the launcher')
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    log_path = output_dir.with_name(output_dir.name + '.launcher.log')
    manifest_path = output_dir.with_name(output_dir.name + '.launch.json')
    command = [sys.executable, str(Path(__file__).with_name('local_multistream_soak.py')),
               '--output-dir', str(output_dir), *driver_arguments]
    # Reserve both sibling artifacts before launching; the driver creates its own case directory.
    with manifest_path.open('x') as manifest, log_path.open('xb') as log:
        child = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                 close_fds=True, start_new_session=True)
        fields = (Path('/proc') / str(child.pid) / 'stat').read_text().rsplit(') ', 1)[1].split()
        record = {'driver_pid': child.pid, 'driver_start_ticks': int(fields[19]),
                  'session_id': os.getsid(child.pid), 'launched_at_utc': datetime.now(timezone.utc).isoformat(),
                  'output_dir': str(output_dir), 'log_file': str(log_path), 'command': command,
                  'scope': 'launch identity only; verify run_state, actual executable and fresh samples separately'}
        json.dump(record, manifest, indent=2)
        manifest.write('\n')
    return record


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('driver_arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    arguments = args.driver_arguments
    if arguments and arguments[0] == '--':
        arguments = arguments[1:]
    if not arguments:
        parser.error('provide local_multistream_soak.py arguments after --')
    print(json.dumps(launch(args.output_dir, arguments)))


if __name__ == '__main__':
    main()
