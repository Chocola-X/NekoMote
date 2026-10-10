#!/usr/bin/env python3
"""G28/G24-2: 用 Plasma probe (Qt/FBO) 生成触摸条件 clean 图。

为 Fraise B/Cinnamon maid-b/Coconut santa-b 生成 default/idle120/moved/scaled_half
及下身进入画幅条件图。每条件保存 probe stdout/stderr、PNG、FBO 尺寸、
alpha bbox、来源 hash。GPU 串行（flock）。

每条件显式校验 probe stdout 中的 executed 步数、seed、rendered_frames，
拒绝步数不匹配或帧未完成的捕获。RPATH 由 CMake install 设置，不需要
LD_LIBRARY_PATH。
"""
from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]

MODELS = ["fraise-france-maid-b", "cinnamon-maid-b", "coconut-santa-b"]
CONDITIONS = [
    {"name": "default", "steps": 0, "origin": [0, 400], "scale": 1.0},
    {"name": "idle120", "steps": 120, "origin": [0, 400], "scale": 1.0},
    {"name": "moved", "steps": 0, "origin": [80, 400], "scale": 1.0},
    {"name": "scaled_half", "steps": 0, "origin": [0, 400], "scale": 0.5},
    {"name": "lower_body_in_frame", "steps": 0, "origin": [0, -200], "scale": 1.0},
]
WIDTH = 800
HEIGHT = 1080
SEED = 1

CAPTURE_RE = re.compile(
    r"capture:\s+fixed\s+steps=(\d+)\s+executed=(\d+)\s+seed=(\d+)\s+rendered_frames=(\d+)"
)


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


def alpha_bbox(png_path):
    try:
        from PIL import Image
        with Image.open(png_path) as img:
            img.load()
            if img.mode != "RGBA":
                img = img.convert("RGBA")
            alpha = img.getchannel("A")
            bbox = alpha.getbbox()
            return {"size": list(img.size), "alpha_bbox": list(bbox) if bbox else None}
    except Exception as e:
        return {"error": str(e)}


def parse_capture_diagnostics(stdout):
    """Extract executed steps, seed, rendered_frames from probe stdout.

    Returns (parsed_dict, errors). The probe emits a line like:
      capture: fixed steps=120 executed=120 seed=1 rendered_frames=3
    """
    errors = []
    match = CAPTURE_RE.search(stdout or "")
    if not match:
        return None, ["capture diagnostics line not found in stdout"]
    steps, executed, seed, rendered = (int(v) for v in match.groups())
    parsed = {"steps": steps, "executed": executed, "seed": seed,
              "rendered_frames": rendered}
    return parsed, errors


def validate_capture(parsed, expected_steps, expected_seed):
    """Verify probe executed the requested steps and seed."""
    errors = []
    if parsed is None:
        errors.append("no capture diagnostics; cannot verify execution")
        return errors
    if parsed["steps"] != expected_steps:
        errors.append(f"steps mismatch: probe={parsed['steps']} expected={expected_steps}")
    if parsed["executed"] != expected_steps:
        errors.append(f"executed mismatch: probe={parsed['executed']} expected={expected_steps}")
    if parsed["seed"] != expected_seed:
        errors.append(f"seed mismatch: probe={parsed['seed']} expected={expected_seed}")
    if parsed["rendered_frames"] < 1:
        errors.append("rendered_frames < 1; no frame was produced")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", required=True, help="nekomote_plasma_package_probe 路径")
    parser.add_argument("--output", required=True, help="输出目录")
    parser.add_argument("--models", nargs="+", default=MODELS)
    args = parser.parse_args()

    probe = pathlib.Path(args.probe).resolve()
    output = pathlib.Path(args.output).resolve()
    if output.exists():
        raise SystemExit(f"output already exists: {output}")
    output.mkdir(parents=True)

    lock_path = ROOT / "build/gpu-validation.lock"
    lock_path.parent.mkdir(parents=True, exist_ok=True)

    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip())

    manifest = {
        "task": "G24-2",
        "purpose": "Qt/FBO 条件 clean 图",
        "head_commit": head,
        "dirty": dirty,
        "generator": file_record(__file__, "generator_script"),
        "probe": file_record(probe, "probe_binary"),
        "width": WIDTH,
        "height": HEIGHT,
        "seed": SEED,
        "conditions": CONDITIONS,
        "models": {},
        "all_ok": True,
    }

    with lock_path.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for model in args.models:
            model_dir = ROOT / "assets/characters" / model
            model_file = model_dir / "model.nkm.json"
            session_file = model_dir / "session.nks.json"
            model_output = output / model
            model_output.mkdir()
            manifest["models"][model] = {
                "model_asset": file_record(model_file, "model_asset"),
                "session_asset": file_record(session_file, "session_asset"),
                "conditions": {},
            }

            for cond in CONDITIONS:
                cond_name = cond["name"]
                cond_dir = model_output / cond_name
                cmd = [
                    str(probe),
                    "--model", str(model_file),
                    "--session", str(session_file),
                    "--output", str(cond_dir),
                    "--width", str(WIDTH),
                    "--height", str(HEIGHT),
                    "--origin-x", str(cond["origin"][0]),
                    "--origin-y", str(cond["origin"][1]),
                    "--scale", str(cond["scale"]),
                    "--steps", str(cond["steps"]),
                    "--seed", str(SEED),
                ]
                t0 = time.monotonic()
                result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
                elapsed = time.monotonic() - t0

                png_path = cond_dir / "package.png"
                cond_result = {
                    "name": cond_name,
                    "params": cond,
                    "argv": cmd,
                    "returncode": result.returncode,
                    "elapsed_s": round(elapsed, 3),
                    "ok": result.returncode == 0 and png_path.is_file(),
                    "stdout": result.stdout,
                    "stderr": result.stderr,
                    "diagnostics": None,
                    "validation_errors": [],
                }

                parsed, parse_errors = parse_capture_diagnostics(result.stdout)
                cond_result["diagnostics"] = parsed
                cond_result["validation_errors"].extend(parse_errors)
                if parsed is not None:
                    cond_result["validation_errors"].extend(
                        validate_capture(parsed, cond["steps"], SEED))
                if cond_result["validation_errors"]:
                    cond_result["ok"] = False

                if png_path.is_file():
                    cond_result["png"] = file_record(png_path, "clean_png")
                    cond_result["image_info"] = alpha_bbox(png_path)
                else:
                    cond_result["ok"] = False
                if not cond_result["ok"]:
                    manifest["all_ok"] = False

                manifest["models"][model]["conditions"][cond_name] = cond_result
                status = "OK" if cond_result["ok"] else "FAIL"
                print(f"  {model}/{cond_name}: {status} ({elapsed:.1f}s)", file=sys.stderr)
                if cond_result["validation_errors"]:
                    for ve in cond_result["validation_errors"]:
                        print(f"    VALIDATION ERROR: {ve}", file=sys.stderr)

    manifest_path = output / "capture-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False, allow_nan=False) + "\n")
    print(f"wrote {manifest_path}")
    return 0 if manifest["all_ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
