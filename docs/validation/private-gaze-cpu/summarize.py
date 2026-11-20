"""Report only complete, balanced N=1 experiments; use on cpu.csv."""
import csv
from pathlib import Path
from statistics import median
import sys

with Path(sys.argv[1]).open() as f:
    rows = list(csv.DictReader(f))
by_key = {(int(row['pair']), row['mode'], row['phase']): row for row in rows}
assert len(rows) == len(by_key) == 16
print('| Phase | Process | Local median | Desktop median | Paired Δ median [min, max] (pp) |')
print('|---|---|---:|---:|---:|')
for phase in ('idle', 'motion'):
    for field, label in [('wp_cpu_pct', 'wallpaper (= total)'), ('comp_cpu_pct', 'niri')]:
        series = [[float(by_key[i, mode, phase][field]) for i in range(1, 5)] for mode in ('local', 'desktop')]
        local, desk = series
        delta = [b-a for a,b in zip(local, desk)]
        print(f'| {phase} | {label} | {median(local):.2f}% | {median(desk):.2f}% | {median(delta):+.2f} [{min(delta):+.2f}, {max(delta):+.2f}] |')
