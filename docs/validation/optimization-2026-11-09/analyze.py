#!/usr/bin/env python3
"""Recompute equal-case, median-of-three performance changes."""
import json
from pathlib import Path
from statistics import mean, median

root = Path(__file__).resolve().parent
result = {}
for prefix, metrics in (
    ('gl', ('prepare_us', 'cpu_us', 'complete_us')),
    ('gles', ('prepare_us', 'cpu_us', 'complete_us')),
    ('vk', ('prepare_us', 'cpu_us', 'complete_us')),
    ('runtime', ('advance_us', 'prepare_us', 'total_us', 'geometry_evaluate_us', 'geometry_build_us')),
    ('validation', ('advance_us', 'prepare_us', 'total_us', 'geometry_evaluate_us', 'geometry_build_us')),
):
    before, after = [
        [[json.loads(line) for line in (root / f'{prefix}-{label}-{i}.jsonl').read_text().splitlines()]
         for i in range(3)] for label in ('before', 'after')
    ]
    assert all(len(run) == len(before[0]) for run in before + after)
    for i, row in enumerate(before[0]):
        for run in before + after:
            assert all(run[i][key] == row[key] for key in ('model', 'moving'))
            if prefix in ('runtime', 'validation'):
                assert run[i]['fingerprint'] == row['fingerprint']
                assert run[i]['allocations'] == 0
    result[prefix] = {
        metric: {stat: mean(
            (median(run[i][metric][stat] for run in after) /
             median(run[i][metric][stat] for run in before) - 1) * 100
            for i in range(len(before[0])))
            for stat in ('mean', 'p50', 'p95', 'p99')}
        for metric in metrics
    }
print(json.dumps(result, indent=2))
