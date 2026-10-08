#!/usr/bin/env python3
"""G17: reproduce the 15 CPU-rejected models with the current Release probe.

Runs nekomote_session_probe for each FAIL model using the frozen G13
default_idle input, records returncode / stderr / stdout / input hashes,
and compares against the G13 frozen evidence to confirm the binary and
results are identical.
"""

import json
import os
import subprocess
import sys
import hashlib
import tempfile

FAIL_MODELS = [
    "azuki-maid-b",
    "azuki-maid-b-custard",
    "azuki-santa-b",
    "azuki-wintermaid-b",
    "cinnamon-maid",
    "coconut-dress",
    "coconut-dress-a",
    "coconut-hawaii-a",
    "coconut-maid",
    "coconut-maid-a",
    "coconut-maid-a-4yo",
    "coconut-santa",
    "coconut-swimsuit-a",
    "coconut-wintermaid",
    "coconut-yukata",
]

PROBE = "build/release/bin/nekomote_session_probe"
ASSETS = "assets/characters"
G13_ROOT = "build/evidence/g13-touch-audit-2026-10-08/full-catalog"
INPUT_JSON = {"steps": 0, "random_seed": 1, "reaction_seed": 1,
              "origin": [0, 400], "scale": 1.0}


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def main():
    if not os.path.exists(PROBE):
        print(f"probe not found: {PROBE}", file=sys.stderr)
        return 2
    probe_hash = sha256_file(PROBE)
    g13_ledger = os.path.join(G13_ROOT, "ledger.json")
    with open(g13_ledger) as f:
        ledger = json.load(f)
    g13_probe_hash = ledger["probe"]["sha256"]

    results = {
        "probe_path": os.path.abspath(PROBE),
        "probe_sha256": probe_hash,
        "g13_probe_sha256": g13_probe_hash,
        "probe_matches_g13": probe_hash == g13_probe_hash,
        "head_commit": subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"], text=True).strip(),
        "models": [],
    }

    for name in FAIL_MODELS:
        model_path = os.path.join(ASSETS, name, "model.nkm.json")
        session_path = os.path.join(ASSETS, name, "session.nks.json")
        if not os.path.exists(model_path):
            results["models"].append({"model": name, "error": "model not found"})
            continue
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as inf:
            json.dump(INPUT_JSON, inf)
            input_path = inf.name
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as outf:
            output_path = outf.name
        proc = subprocess.run(
            [PROBE, model_path, session_path, input_path, output_path],
            capture_output=True, text=True)
        entry = {
            "model": name,
            "returncode": proc.returncode,
            "stderr": proc.stderr.strip(),
            "stdout_sha256": sha256_bytes(proc.stdout.encode()),
            "stdout_bytes": len(proc.stdout),
            "input_sha256": sha256_file(input_path),
        }
        g13_ev = os.path.join(G13_ROOT, name, "default_idle", "regions.result.json")
        if os.path.exists(g13_ev):
            with open(g13_ev) as f:
                g13 = json.load(f)
            entry["g13_returncode"] = g13["returncode"]
            entry["g13_stderr_sha256"] = g13["stderr"]["sha256"]
            entry["stderr_matches_g13"] = (
                sha256_bytes(proc.stderr.encode()) == g13["stderr"]["sha256"])
            entry["g13_stderr"] = None
            with open(os.path.join(G13_ROOT, name, "default_idle",
                                   "regions.stderr.log")) as f:
                entry["g13_stderr"] = f.read().strip()
            entry["stderr_text_matches"] = entry["stderr"] == entry["g13_stderr"]
        os.unlink(input_path)
        os.unlink(output_path)
        results["models"].append(entry)

    print(json.dumps(results, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
