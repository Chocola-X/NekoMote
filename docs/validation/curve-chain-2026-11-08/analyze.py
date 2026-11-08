#!/usr/bin/env python3
"""Summarize alternating runs without a machine-independent performance gate."""

import argparse
import json
from pathlib import Path
from statistics import mean, median

METRICS = (
    "advance_us",
    "prepare_us",
    "total_us",
    "geometry_evaluate_us",
    "geometry_build_us",
)
STATISTICS = ("mean", "p50", "p95", "p99")


def summarize(root):
    def read(label):
        return [
            [json.loads(line) for line in (root / f"{label}-{i}.jsonl").read_text().splitlines()]
            for i in range(3)
        ]

    before, after = read("baseline"), read("candidate")
    assert all(len(run) == len(before[0]) for run in before + after)
    rows = []
    for index, initial in enumerate(before[0]):
        row = {key: initial[key] for key in ("model", "moving")}
        for run in before + after:
            assert all(run[index][key] == row[key] for key in row)
            assert run[index]["fingerprint"] == initial["fingerprint"]
            assert run[index]["allocations"] == 0
        for metric in METRICS:
            row[metric] = {}
            for statistic in STATISTICS:
                a = median(run[index][metric][statistic] for run in before)
                b = median(run[index][metric][statistic] for run in after)
                row[metric][statistic] = {
                    "before_us": a,
                    "after_us": b,
                    "change_percent": (b / a - 1) * 100,
                }
        rows.append(row)
    aggregate = {}
    for metric in METRICS:
        aggregate[metric] = {}
        for statistic in STATISTICS:
            changes = [row[metric][statistic]["change_percent"] for row in rows]
            aggregate[metric][statistic] = {
                "mean_change_percent": mean(changes),
                "min_change_percent": min(changes),
                "max_change_percent": max(changes),
            }
    return {"cases": rows, "equal_case_weight": aggregate, "all_fingerprints_equal": True}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    print(json.dumps(summarize(args.data), indent=2))
