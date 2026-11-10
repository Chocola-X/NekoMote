#!/usr/bin/env python3
"""Compare deterministic anchor_audit outcomes, including failures and switches."""

import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path


def read(path):
    opener = gzip.open if path.suffix == ".gz" else open
    with opener(path, "rt", encoding="utf-8") as stream:
        return [json.loads(line) for line in stream if line.strip()]


def compare(before, after, steps):
    def ordered(rows):
        cases = sorted(
            (row for row in rows if "ticks" in row),
            key=lambda row: (row["model"], row.get("case", row.get("reaction")), row.get("work", 0)),
        )
        return cases + [row for row in rows if "ticks" not in row]

    before, after = ordered(before), ordered(after)
    if not before or len(before) != len(after):
        raise ValueError(f"row count differs: {len(before)} / {len(after)}")
    for index, (a, b) in enumerate(zip(before, after)):
        if a != b:
            raise ValueError(f"row {index} differs: {a!r} / {b!r}")
    cases = [row for row in before if "ticks" in row]
    if not cases or not any("switch_hash" in row for row in before):
        raise ValueError("missing playback cases or controller switch result")
    successful = [row for row in cases if row["error"] == 0]
    if any(row["ticks"] != steps for row in successful):
        raise ValueError("successful case did not complete all steps")
    canonical = json.dumps(before, sort_keys=True, separators=(",", ":")).encode()
    return {
        "all_outcomes_equal": True,
        "canonical_sha256": hashlib.sha256(canonical).hexdigest(),
        "models": sorted({row["model"] for row in cases}),
        "cases": len(cases),
        "successful_cases": len(successful),
        "successful_display_steps": sum(row["ticks"] for row in successful),
        "rejected_cases": len(cases) - len(successful),
        "error_counts": dict(Counter(str(row["error"]) for row in cases)),
        "controller": [row for row in before if "ticks" not in row],
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--steps", required=True, type=int)
    args = parser.parse_args()
    result = compare(read(args.baseline), read(args.candidate), args.steps)
    print(json.dumps(result, ensure_ascii=False, indent=2))
