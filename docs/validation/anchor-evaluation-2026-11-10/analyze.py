#!/usr/bin/env python3
"""Rebuild runtime benchmark aggregation from committed raw measurements."""
import argparse
import importlib.util
import json
from pathlib import Path

source = Path(__file__).resolve().parents[1] / "curve-chain-2026-11-08" / "analyze.py"
spec = importlib.util.spec_from_file_location("runtime_summary", source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    print(json.dumps(module.summarize(args.data), indent=2))
