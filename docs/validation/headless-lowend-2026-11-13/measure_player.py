"""Measure the real player on an authorized X11 display; never inject system input.

Usage: python3 measure_player.py PLAYER ASSETS OUTPUT_DIRECTORY
Uses the player's 120-frame warmup and 600-frame measurement. /proc sampling
starts after six seconds; process CPU excludes Xorg and is expressed per core.
Screenshots and GPU profilers must be run separately from these measurements.
"""

import json
import os
from pathlib import Path
import subprocess
import sys
import time


def snapshot(pid):
    root = Path('/proc') / str(pid)
    fields = (root / 'stat').read_text().rsplit(')', 1)[1].split()
    ticks = int(fields[11]) + int(fields[12])
    rss = None
    for line in (root / 'status').read_text().splitlines():
        if line.startswith('VmRSS:'):
            rss = int(line.split()[1])
    return time.monotonic(), ticks, rss


def main():
    player, assets, output = map(Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    results = []
    models = ['chocola/date-a', 'chocola/wintermaid', 'coconut/casual']
    xorg = next((int(p.name) for p in Path('/proc').iterdir()
                 if p.name.isdigit() and (p / 'comm').exists()
                 and (p / 'comm').read_text().strip() == 'Xorg'), None)
    profiles = [(1920, 1080, 60)] * 3 + [(1280, 720, 60), (1920, 1080, 30)]
    for run, (width, height, fps) in enumerate(profiles):
        for model in models:
            label = f'{run}-{model.replace("/", "-")}-{width}x{height}-{fps}'
            bundle = assets / 'characters' / model
            command = [str(player.resolve()), '--model', str(bundle / 'model.nkm'),
                       '--session', str(bundle / 'session.nks.json'), '--backend', 'opengl',
                       '--fit', '--mute', '--seed', '1', '--width', str(width),
                       '--height', str(height), '--fps', str(fps), '--measure-frames', '600']
            samples = []
            xorg_samples = []
            start = time.monotonic()
            with (output / (label + '.log')).open('w') as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
                timed_out = False
                while process.poll() is None:
                    if time.monotonic() - start > 180:
                        process.kill()
                        timed_out = True
                        break
                    if time.monotonic() - start >= 6:
                        try:
                            value = snapshot(process.pid)
                            if value[2] is not None:
                                samples.append(value)
                                if xorg is not None:
                                    xorg_samples.append(snapshot(xorg))
                        except (FileNotFoundError, ProcessLookupError):
                            pass
                    time.sleep(0.25)
                status = process.wait()
            record = dict(model=model, run=run, width=width, height=height, fps=fps,
                          exit_status=status, timed_out=timed_out,
                          wall_seconds=time.monotonic() - start, command=command,
                          proc_samples=samples)
            if len(samples) >= 2:
                seconds = samples[-1][0] - samples[0][0]
                cpu = 100 * (samples[-1][1] - samples[0][1]) / os.sysconf('SC_CLK_TCK') / seconds
                record.update(cpu_one_core_percent=cpu, cpu_machine_percent=cpu / os.cpu_count(),
                              rss_peak_kib=max(s[2] for s in samples), steady_seconds=seconds)
            if len(xorg_samples) >= 2:
                first, last = xorg_samples[0], xorg_samples[-1]
                record['xorg_cpu_one_core_percent'] = (
                    100 * (last[1] - first[1]) / os.sysconf('SC_CLK_TCK') / (last[0] - first[0]))
            results.append(record)
            (output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
            print(label, status, record.get('cpu_one_core_percent'), flush=True)


if __name__ == '__main__':
    main()
