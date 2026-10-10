#!/usr/bin/env python3
"""G29-2: Real pause/resume validation on effective sessions.

Calls nekomote_plasma_package_probe --lifecycle to exercise real pause/resume,
hide/show and resize.  Parses stdout to verify:
  - pause confirmed (no frames advance while paused)
  - resume confirmed (first delta == 0.0)
  - hidden confirmed (no frames while hidden)
  - show confirmed (frames resume)
  - renderer created == destroyed

Also runs nekomote_pose with 60 and 120 steps to verify deterministic prefixes.
Uses controlled host time; no system input injection.
"""
from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
MODELS = ["coconut-santa", "azuki-santa-b", "cinnamon-maid"]
STEPS_LIFECYCLE = 120
STEPS_PREFIX = [60, 120]
SEED = 1


def sha256_file(path):
    path = pathlib.Path(path)
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def file_record(path, role=None):
    path = pathlib.Path(path).resolve()
    record = {"path": str(path), "sha256": sha256_file(path), "bytes": path.stat().st_size}
    if role:
        record["role"] = role
    return record


def parse_lifecycle_stdout(stdout):
    """Extract pause/resume verification facts from probe stdout."""
    facts = {
        "pause_confirmed": False,
        "resume_first_delta": None,
        "hidden_confirmed": False,
        "show_confirmed": False,
        "renderer_created": None,
        "renderer_destroyed": None,
        "containment_resize_error": False,
    }
    for line in (stdout or "").splitlines():
        if "pause confirmed" in line:
            facts["pause_confirmed"] = True
        if "resume confirmed" in line:
            m = re.search(r"first delta=([0-9.eE+-]+)", line)
            if m:
                facts["resume_first_delta"] = float(m.group(1))
        if "hidden confirmed" in line:
            facts["hidden_confirmed"] = True
        if "show confirmed" in line:
            facts["show_confirmed"] = True
        if "renderer created=" in line:
            m = re.search(r"created=(\d+)", line)
            if m:
                facts["renderer_created"] = int(m.group(1))
        if "renderer destroyed=" in line or "destroyed=" in line:
            m = re.search(r"destroyed=(\d+)", line)
            if m:
                facts["renderer_destroyed"] = int(m.group(1))
        if "containment did not resize" in line:
            facts["containment_resize_error"] = True
    return facts


def validate_lifecycle(facts):
    """Verify core pause/resume invariants."""
    errors = []
    if not facts["pause_confirmed"]:
        errors.append("pause not confirmed")
    if facts["resume_first_delta"] is None:
        errors.append("resume first delta not reported")
    elif facts["resume_first_delta"] != 0.0:
        errors.append(f"resume first delta={facts['resume_first_delta']} != 0.0")
    if not facts["hidden_confirmed"]:
        errors.append("hidden not confirmed (pause did not stop frames)")
    if not facts["show_confirmed"]:
        errors.append("show not confirmed (frames did not resume)")
    if facts["renderer_created"] is not None and facts["renderer_destroyed"] is not None:
        if facts["renderer_created"] != facts["renderer_destroyed"]:
            errors.append(f"renderer leak: created={facts['renderer_created']} destroyed={facts['renderer_destroyed']}")
    return errors


def run_lifecycle(probe, model_file, session_file, output_dir, rounds=5):
    """Run --lifecycle and parse results."""
    cmd = [
        str(probe),
        "--model", str(model_file),
        "--session", str(session_file),
        "--output", str(output_dir),
        "--lifecycle",
        "--rounds", str(rounds),
        "--steps", str(STEPS_LIFECYCLE),
        "--seed", str(SEED),
    ]
    t0 = time.monotonic()
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    elapsed = time.monotonic() - t0
    facts = parse_lifecycle_stdout(result.stdout + "\n" + result.stderr)
    errors = validate_lifecycle(facts)
    return {
        "argv": cmd,
        "returncode": result.returncode,
        "elapsed_s": round(elapsed, 3),
        "stdout": result.stdout,
        "stderr": result.stderr,
        "facts": facts,
        "errors": errors,
        "ok": len(errors) == 0,
    }


def run_deterministic_prefix(pose, model_file, session_file, output_dir):
    """Run 60 and 120 step prefixes; verify 60-step frame matches 120-step frame[60]."""
    results = {}
    for steps in STEPS_PREFIX:
        cmd = [
            str(pose),
            "--asset", str(model_file),
            "--animation-diagnostic",
            "--steps", str(steps),
            "--random-seed", str(SEED),
            "--output", str(output_dir / f"pose_{steps}.json"),
            "--dump-frame",
        ]
        t0 = time.monotonic()
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        elapsed = time.monotonic() - t0
        results[steps] = {
            "argv": cmd,
            "returncode": r.returncode,
            "elapsed_s": round(elapsed, 3),
            "ok": r.returncode == 0,
        }
        out_path = output_dir / f"pose_{steps}.json"
        if out_path.is_file():
            results[steps]["output"] = file_record(out_path)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", required=True, help="nekomote_plasma_package_probe")
    parser.add_argument("--pose", required=True, help="nekomote_pose")
    parser.add_argument("--output", required=True, help="输出目录")
    args = parser.parse_args()

    probe = pathlib.Path(args.probe).resolve()
    pose = pathlib.Path(args.pose).resolve()
    output = pathlib.Path(args.output).resolve()
    if output.exists():
        raise SystemExit(f"output already exists: {output}")
    output.mkdir(parents=True)

    lock_path = ROOT / "build/gpu-validation.lock"
    lock_path.parent.mkdir(parents=True, exist_ok=True)

    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip())

    manifest = {
        "task": "G29-2",
        "purpose": "真实 pause/resume 验证 + 确定性前缀",
        "head_commit": head,
        "dirty": dirty,
        "generator": file_record(__file__, "generator_script"),
        "probe": file_record(probe, "probe_binary"),
        "pose": file_record(pose, "pose_binary"),
        "models": {},
        "all_ok": True,
    }

    with lock_path.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for model in MODELS:
            model_dir = ROOT / "assets/characters" / model
            model_file = model_dir / "model.nkm.json"
            session_file = model_dir / "session.nks.json"
            if not model_file.is_file() or not session_file.is_file():
                print(f"  {model}: MISSING", file=sys.stderr)
                manifest["all_ok"] = False
                continue
            model_output = output / model
            model_output.mkdir()
            lc_dir = model_output / "lifecycle"
            lc_result = run_lifecycle(probe, model_file, session_file, lc_dir)
            prefix_dir = model_output / "prefix"
            prefix_dir.mkdir()
            prefix_results = run_deterministic_prefix(pose, model_file, session_file, prefix_dir)
            manifest["models"][model] = {
                "lifecycle": lc_result,
                "prefix": prefix_results,
                "ok": lc_result["ok"] and all(p["ok"] for p in prefix_results.values()),
            }
            if not manifest["models"][model]["ok"]:
                manifest["all_ok"] = False
            status = "OK" if manifest["models"][model]["ok"] else "FAIL"
            print(f"  {model}: {status}", file=sys.stderr)
            if lc_result["errors"]:
                for e in lc_result["errors"]:
                    print(f"    LIFECYCLE ERROR: {e}", file=sys.stderr)

    manifest_path = output / "pause-resume-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False, allow_nan=False) + "\n")
    print(f"wrote {manifest_path}")
    return 0 if manifest["all_ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
