#!/usr/bin/env python3
"""Summarize paired Release runs; no machine-independent performance gate."""

import json
from pathlib import Path
from statistics import mean, median

ROOT = Path(__file__).resolve().parent


def read(pattern):
    runs = []
    for round_number in range(3):
        path = ROOT / pattern.format(round_number)
        runs.append([json.loads(line) for line in path.read_text().splitlines()])
    assert len({len(run) for run in runs}) == 1
    return runs


def compare(before, after, metrics):
    assert len(before[0]) == len(after[0])
    rows = []
    for index, initial in enumerate(before[0]):
        row = {key: initial[key] for key in ("model", "moving")}
        for run in before + after:
            assert all(run[index][key] == row[key] for key in row)
        for metric in metrics:
            row[metric] = {}
            for statistic in ("mean", "p50", "p95", "p99"):
                a = median(run[index][metric][statistic] for run in before)
                b = median(run[index][metric][statistic] for run in after)
                row[metric][statistic] = {
                    "before_us": a,
                    "after_us": b,
                    "change_percent": (b / a - 1) * 100,
                }
        rows.append(row)
    aggregate = {}
    for metric in metrics:
        aggregate[metric] = {}
        for statistic in ("mean", "p50", "p95", "p99"):
            changes = [row[metric][statistic]["change_percent"] for row in rows]
            aggregate[metric][statistic] = {
                "mean_change_percent": mean(changes),
                "min_change_percent": min(changes),
                "max_change_percent": max(changes),
            }
    return {"cases": rows, "equal_case_weight": aggregate}


baseline, final = read("baseline-final-{}.jsonl"), read("final-final-{}.jsonl")
for old, new in zip(baseline, final):
    for a, b in zip(old, new):
        assert a["fingerprint"] == b["fingerprint"]
        assert a["allocations"] == b["allocations"] == 0
result = {
    "runtime": compare(
        baseline,
        final,
        ("advance_us", "prepare_us", "total_us", "geometry_evaluate_us", "geometry_build_us"),
    )
}
for api in ("desktop", "gles", "vulkan"):
    before = read("ebo-{}-" + api + "-upload.jsonl")
    after = read("ebo-{}-" + api + "-reuse.jsonl")
    result[api] = compare(before, after, ("prepare_us", "cpu_us", "complete_us"))
    if api != "vulkan":
        result[api]["upload_counters"] = [
            {
                "model": a["model"],
                "moving": a["moving"],
                "before_index_calls": a["index_calls"],
                "after_index_calls": b["index_calls"],
                "before_index_bytes": a["index_bytes"],
                "after_index_bytes": b["index_bytes"],
                "vertex_bytes_unchanged": a["vertex_bytes"] == b["vertex_bytes"],
            }
            for a, b in zip(before[0], after[0])
        ]

print(json.dumps(result, indent=2, ensure_ascii=False))
