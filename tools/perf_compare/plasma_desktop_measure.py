#!/usr/bin/env python3
"""Read real-session process counters and NVIDIA NVML; never change desktop state.

CPU percentages use one logical CPU = 100%. JSONL stores absolute counters;
interval statistics can be recomputed without depending on this script's summary.
NVIDIA process utilization is deliberately not inferred from device utilization.
"""

import argparse
import ctypes as ct
import datetime
import json
import os
from pathlib import Path
import statistics
import time


MIB = 1024 * 1024


class Memory(ct.Structure):
    _fields_ = [('version', ct.c_uint), ('total', ct.c_ulonglong),
                ('reserved', ct.c_ulonglong), ('free', ct.c_ulonglong),
                ('used', ct.c_ulonglong)]


class Utilization(ct.Structure):
    _fields_ = [('gpu', ct.c_uint), ('memory', ct.c_uint)]


class ProcessInfo(ct.Structure):
    _fields_ = [('pid', ct.c_uint), ('used_memory', ct.c_ulonglong),
                ('gpu_instance', ct.c_uint), ('compute_instance', ct.c_uint)]


class Nvidia:
    def __init__(self):
        self.lib = ct.CDLL('libnvidia-ml.so.1')
        self.check(self.lib.nvmlInit_v2())
        self.device = ct.c_void_p()
        self.check(self.lib.nvmlDeviceGetHandleByIndex_v2(0, ct.byref(self.device)))

    @staticmethod
    def check(result):
        if result:
            raise RuntimeError(f'NVML error {result}')

    def read(self):
        memory, utilization = Memory(), Utilization()
        memory.version = ct.sizeof(Memory) | (2 << 24)
        power, clock, mem_clock, state = (ct.c_uint() for _ in range(4))
        values = {}
        calls = [('memory', 'nvmlDeviceGetMemoryInfo_v2', (ct.byref(memory),)),
                 ('utilization', 'nvmlDeviceGetUtilizationRates', (ct.byref(utilization),)),
                 ('power', 'nvmlDeviceGetPowerUsage', (ct.byref(power),)),
                 ('clock', 'nvmlDeviceGetClockInfo', (0, ct.byref(clock))),
                 ('mem_clock', 'nvmlDeviceGetClockInfo', (2, ct.byref(mem_clock))),
                 ('state', 'nvmlDeviceGetPerformanceState', (ct.byref(state),))]
        for name, function, arguments in calls:
            result = getattr(self.lib, function)(self.device, *arguments)
            if result:
                values[name + '_error'] = result
        if 'memory_error' not in values:
            values['device_memory_mib'] = memory.used / MIB
            values['device_reserved_mib'] = memory.reserved / MIB
        if 'utilization_error' not in values:
            values['device_gpu_percent'] = utilization.gpu
            values['device_memory_controller_percent'] = utilization.memory
        for name, value, key, scale in [
                ('power', power, 'device_power_w', 1000),
                ('clock', clock, 'graphics_clock_mhz', 1),
                ('mem_clock', mem_clock, 'memory_clock_mhz', 1),
                ('state', state, 'pstate', 1)]:
            if name + '_error' not in values:
                values[key] = value.value / scale
        count = ct.c_uint(256)
        processes = (ProcessInfo * count.value)()
        result = self.lib.nvmlDeviceGetGraphicsRunningProcesses_v2(
            self.device, ct.byref(count), processes)
        if result == 0:
            values['graphics_processes'] = {
                str(p.pid): p.used_memory / MIB for p in processes[:count.value]
                if p.used_memory != 2**64 - 1}
        else:
            values['graphics_processes_error'] = result
        return values


def process(pid, include_pss):
    root = Path('/proc') / str(pid)
    raw = (root / 'stat').read_text()
    stat = raw[raw.rfind(')') + 2:].split()
    result = {'pid': pid, 'start_ticks': int(stat[19]),
              'cpu_ticks': int(stat[11]) + int(stat[12]),
              'rss_mib': int(stat[21]) * os.sysconf('SC_PAGE_SIZE') / MIB,
              'threads': int(stat[17]), 'minor_faults': int(stat[7]),
              'major_faults': int(stat[9])}
    if include_pss:
        try:
            smaps = (root / 'smaps_rollup').read_text()
        except PermissionError:
            result['smaps_error'] = 'permission denied'
            return result
        for line in smaps.splitlines():
            key, _, value = line.partition(':')
            if key in ('Pss', 'Private_Clean', 'Private_Dirty', 'Swap'):
                result[key.lower() + '_mib'] = int(value.split()[0]) / 1024
    return result


def quantile(values, fraction):
    ordered = sorted(values)
    index = (len(ordered) - 1) * fraction
    lo, hi = int(index), min(int(index) + 1, len(ordered) - 1)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (index - lo)


def describe(values):
    return {'mean': statistics.mean(values), 'p50': quantile(values, .5),
            'p95': quantile(values, .95), 'p99': quantile(values, .99),
            'min': min(values), 'max': max(values), 'first': values[0], 'last': values[-1]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', action='append', required=True, metavar='NAME=PID')
    parser.add_argument('--seconds', type=int, default=120)
    parser.add_argument('--warmup', type=int, default=0)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.seconds < 1 or args.warmup < 0:
        parser.error('--seconds must be positive and --warmup nonnegative')
    pids = dict((name, int(pid)) for name, pid in (v.split('=', 1) for v in args.pid))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    gpu = Nvidia()
    ticks = os.sysconf('SC_CLK_TCK')
    if args.warmup:
        time.sleep(args.warmup)
    rows = []
    start = time.monotonic()
    cpu_start = time.process_time()
    with args.output.open('w') as output:
        output.write(json.dumps({'schema': 2, 'pids': pids, 'clock_ticks': ticks,
                                 'cpu_count': os.cpu_count(), 'interval_seconds': 1,
                                 'warmup_seconds': args.warmup,
                                 'smaps_interval_seconds': 5}) + '\n')
        for index in range(args.seconds + 1):
            delay = start + index - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            row = {'monotonic': time.monotonic(),
                   'time': datetime.datetime.now().astimezone().isoformat(),
                   'processes': {k: process(v, index % 5 == 0) for k, v in pids.items()},
                   'gpu': gpu.read()}
            if rows:
                previous = rows[-1]
                dt = row['monotonic'] - previous['monotonic']
                for name, data in row['processes'].items():
                    old = previous['processes'][name]
                    if data['start_ticks'] != old['start_ticks']:
                        raise RuntimeError(f'{name} restarted during measurement')
                    data['cpu_percent'] = (data['cpu_ticks'] - old['cpu_ticks']) / ticks / dt * 100
            rows.append(row)
            output.write(json.dumps(row) + '\n')
            output.flush()
    summary = {'seconds': rows[-1]['monotonic'] - rows[0]['monotonic'],
               'sampler_cpu_seconds': time.process_time() - cpu_start, 'processes': {}, 'gpu': {}}
    for name, pid in pids.items():
        data = [r['processes'][name] for r in rows]
        summary['processes'][name] = {
            key: describe([d[key] for d in data if key in d])
            for key in ('cpu_percent', 'rss_mib', 'pss_mib', 'private_clean_mib',
                        'private_dirty_mib', 'threads', 'swap_mib') if any(key in d for d in data)}
        summary['processes'][name]['smaps_error'] = next(
            (d['smaps_error'] for d in data if 'smaps_error' in d), None)
        summary['processes'][name]['cpu_percent_weighted'] = (
            (data[-1]['cpu_ticks'] - data[0]['cpu_ticks']) / ticks / summary['seconds'] * 100)
        vram = [r['gpu']['graphics_processes'][str(pid)] for r in rows
                if str(pid) in r['gpu'].get('graphics_processes', {})]
        if vram:
            summary['processes'][name]['vram_mib'] = describe(vram)
        summary['processes'][name]['major_faults_delta'] = data[-1]['major_faults'] - data[0]['major_faults']
    for key in rows[0]['gpu']:
        if key != 'graphics_processes' and not key.endswith('_error') and all(
                isinstance(r['gpu'].get(key), (int, float)) for r in rows):
            summary['gpu'][key] = describe([r['gpu'][key] for r in rows])
    args.output.with_suffix('.summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    gpu.lib.nvmlShutdown()
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
