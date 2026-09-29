"""Read-only Linux resource evidence for the local multistream soak fixture."""
from __future__ import annotations

import os
import re
import time
from pathlib import Path


def optional_text(path: Path) -> str | None:
    try:
        return path.read_text().strip()
    except (FileNotFoundError, PermissionError):
        return None


def process_stat(path: Path) -> dict[str, int | str]:
    fields = path.read_text().rsplit(') ', 1)[1].split()
    return {'state': fields[0], 'cpu_ticks': int(fields[11]) + int(fields[12]),
            'start_ticks': int(fields[19]), 'last_cpu': int(fields[36]),
            'minor_faults': int(fields[7]), 'major_faults': int(fields[9])}


def cpu_cgroup_limits(pid: int) -> dict[str, dict[str, str | None]]:
    result = {}
    for line in (Path('/proc') / str(pid) / 'cgroup').read_text().splitlines():
        _, controllers, group = line.split(':', 2)
        if 'cpu' not in controllers.split(','):
            continue
        for mount in (Path('/sys/fs/cgroup/cpu,cpuacct'), Path('/sys/fs/cgroup/cpu')):
            if not (mount / 'cpu.cfs_quota_us').exists():
                continue
            current = mount / group.lstrip('/')
            while True:
                result[str(current)] = {name: optional_text(current / name) for name in
                    ('cpu.cfs_quota_us', 'cpu.cfs_period_us', 'cpu.stat')}
                if current == mount:
                    break
                current = current.parent
            return result
    # Explicitly absent on an unsupported/unavailable hierarchy; never infer unlimited CPU.
    return result


def snapshot(pid: int, start_ticks: int, binary: Path) -> dict:
    started = time.monotonic()
    root = Path('/proc') / str(pid)
    process = process_stat(root / 'stat')
    if process['start_ticks'] != start_ticks or process['state'] == 'Z':
        raise RuntimeError('owned service identity changed')
    if (root / 'exe').resolve() != binary.resolve():
        raise RuntimeError('owned service executable changed')
    threads = {}
    for directory in (root / 'task').iterdir():
        try:
            row = process_stat(directory / 'stat')
            row['allowed_cpus'] = next(line.split(':', 1)[1].strip()
                for line in (directory / 'status').read_text().splitlines()
                if line.startswith('Cpus_allowed_list:'))
            threads[directory.name] = row
        except FileNotFoundError:
            continue
    nodes = {}
    for line in (root / 'numa_maps').read_text().splitlines():
        for node, count in re.findall(r'(?:^| )N(\d+)=(\d+)', line):
            nodes[node] = nodes.get(node, 0) + int(count)
    cpu = [int(value) for value in Path('/proc/stat').read_text().splitlines()[0].split()[1:9]]
    frequencies = [int(value) for path in Path('/sys/devices/system/cpu').glob(
        'cpu[0-9]*/cpufreq/scaling_cur_freq') if (value := optional_text(path)) is not None]
    return {
        'schema_version': 1, 'monotonic_seconds': started, 'service_pid': pid,
        'clock_ticks_per_second': os.sysconf('SC_CLK_TCK'), 'host_logical_cpus': os.cpu_count(),
        'service': process, 'driver': process_stat(Path('/proc/self/stat')), 'threads': threads,
        'service_cpu_affinity': sorted(os.sched_getaffinity(pid)),
        'numa_mapping_pages_by_node': nodes, 'host_cpu_ticks_first_eight': cpu,
        'cpu_cgroups': cpu_cgroup_limits(pid), 'host_cpu_pressure': optional_text(Path('/proc/pressure/cpu')),
        'scaling_frequency_khz': {'count': len(frequencies),
            'min': min(frequencies) if frequencies else None, 'max': max(frequencies) if frequencies else None,
            'mean': sum(frequencies) / len(frequencies) if frequencies else None},
        'observation_seconds': time.monotonic() - started,
    }


def interval(first: dict, last: dict) -> dict:
    elapsed = last['monotonic_seconds'] - first['monotonic_seconds']
    if elapsed <= 0 or first['service_pid'] != last['service_pid']:
        raise ValueError('invalid resource interval')
    if first['service']['start_ticks'] != last['service']['start_ticks']:
        raise ValueError('owned service restarted')
    hz, cpus = first['clock_ticks_per_second'], first['host_logical_cpus']
    if (hz, cpus) != (last['clock_ticks_per_second'], last['host_logical_cpus']):
        raise ValueError('CPU accounting scope changed')
    ticks = [b - a for a, b in zip(first['host_cpu_ticks_first_eight'], last['host_cpu_ticks_first_eight'])]
    if len(ticks) != 8 or any(value < 0 for value in ticks) or sum(ticks) <= 0:
        raise ValueError('invalid host CPU counters')
    result = {'elapsed_seconds': elapsed, 'host_busy_percent':
              100 * (sum(ticks) - ticks[3] - ticks[4]) / sum(ticks)}
    for name in ('service', 'driver'):
        if first[name]['start_ticks'] != last[name]['start_ticks']:
            raise ValueError('observed process restarted')
        delta = last[name]['cpu_ticks'] - first[name]['cpu_ticks']
        if delta < 0:
            raise ValueError('process CPU counter regressed')
        result[name + '_cpu_percent_of_host'] = 100 * delta / hz / elapsed / cpus
    # This is an observation of other work, not proof of interference or NUMA cost.
    result['host_busy_minus_service_and_driver_percent'] = (result['host_busy_percent'] -
        result['service_cpu_percent_of_host'] - result['driver_cpu_percent_of_host'])
    common = first['threads'].keys() & last['threads'].keys()
    result['active_threads_with_different_endpoint_cpu'] = sum(
        b['last_cpu'] != a['last_cpu'] for tid in common
        if (a := first['threads'][tid])['start_ticks'] == (b := last['threads'][tid])['start_ticks']
        and b['cpu_ticks'] > a['cpu_ticks'])
    return result
