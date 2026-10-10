#!/usr/bin/env python3
"""G28-3: Compare Qt/FBO probe captures against nekomote_player reference captures.

Generates player reference screenshots for default and idle120 conditions,
then computes pixel RMSE against the Qt probe clean images.  The player uses
a different render path (GLFW) than the Qt/FBO probe, so its output is a
legitimate reference — not a self-comparison.

No browser/WebGL is available in this environment; player is the reference.
"""
from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
MODELS = ["fraise-france-maid-b", "cinnamon-maid-b", "coconut-santa-b"]
CONDITIONS = [
    {"name": "default", "steps": 0, "origin": [0, 400], "scale": 1.0},
    {"name": "idle120", "steps": 120, "origin": [0, 400], "scale": 1.0},
]
WIDTH = 800
HEIGHT = 1080
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


def pixel_rmse(a_path, b_path):
    """Compute RMSE between two RGBA PNGs."""
    from PIL import Image
    import math
    with Image.open(a_path) as a, Image.open(b_path) as b:
        a = a.convert("RGBA")
        b = b.convert("RGBA")
        if a.size != b.size:
            return None, f"size mismatch: {a.size} vs {b.size}"
        aa = list(a.getdata())
        bb = list(b.getdata())
        if len(aa) != len(bb):
            return None, "pixel count mismatch"
        total = 0
        for (ra, ga, ba, alfa), (rb, gb, bb_, alfb) in zip(aa, bb):
            total += (ra - rb) ** 2 + (ga - gb) ** 2 + (ba - bb_) ** 2 + (alfa - alfb) ** 2
        rmse = math.sqrt(total / (len(aa) * 4))
        return rmse, None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--player", required=True, help="nekomote_player 路径")
    parser.add_argument("--qt-captures", required=True, help="Qt captures 目录")
    parser.add_argument("--output", required=True, help="输出目录")
    args = parser.parse_args()

    player = pathlib.Path(args.player).resolve()
    qt_captures = pathlib.Path(args.qt_captures).resolve()
    output = pathlib.Path(args.output).resolve()
    if output.exists():
        raise SystemExit(f"output already exists: {output}")
    output.mkdir(parents=True)

    lock_path = ROOT / "build/gpu-validation.lock"
    lock_path.parent.mkdir(parents=True, exist_ok=True)

    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip())

    manifest = {
        "task": "G28-3",
        "purpose": "Qt/FBO vs player 参照对比（默认和 idle120）",
        "head_commit": head,
        "dirty": dirty,
        "generator": file_record(__file__, "generator_script"),
        "player": file_record(player, "player_binary"),
        "width": WIDTH,
        "height": HEIGHT,
        "seed": SEED,
        "models": {},
        "all_ok": True,
    }

    with lock_path.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for model in MODELS:
            model_dir = ROOT / "assets/characters" / model
            model_file = model_dir / "model.nkm.json"
            session_file = model_dir / "session.nks.json"
            model_output = output / model
            model_output.mkdir()
            manifest["models"][model] = {"conditions": {}}

            for cond in CONDITIONS:
                cond_name = cond["name"]
                player_png = model_output / f"{cond_name}_player.png"
                cmd = [
                    str(player),
                    "--model", str(model_file),
                    "--session", str(session_file),
                    "--width", str(WIDTH),
                    "--height", str(HEIGHT),
                    "--origin-x", str(cond["origin"][0]),
                    "--origin-y", str(cond["origin"][1]),
                    "--scale", str(cond["scale"]),
                    "--seed", str(SEED),
                    "--steps", str(cond["steps"]),
                    "--transparent", "--mute", "--no-save",
                    "--capture", str(player_png),
                ]
                t0 = time.monotonic()
                result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
                elapsed = time.monotonic() - t0

                qt_png = qt_captures / model / cond_name / "package.png"
                cond_result = {
                    "name": cond_name,
                    "player_argv": cmd,
                    "player_returncode": result.returncode,
                    "player_elapsed_s": round(elapsed, 3),
                    "player_stdout": result.stdout,
                    "player_stderr": result.stderr,
                    "ok": False,
                }

                if player_png.is_file():
                    cond_result["player_png"] = file_record(player_png, "player_reference")
                if qt_png.is_file():
                    cond_result["qt_png"] = file_record(qt_png, "qt_clean")

                if player_png.is_file() and qt_png.is_file():
                    rmse, err = pixel_rmse(player_png, qt_png)
                    if rmse is not None:
                        cond_result["rmse"] = round(rmse, 4)
                        cond_result["ok"] = rmse < 5.0
                    else:
                        cond_result["rmse_error"] = err

                if not cond_result["ok"]:
                    manifest["all_ok"] = False

                manifest["models"][model]["conditions"][cond_name] = cond_result
                status = "OK" if cond_result["ok"] else "FAIL"
                rmse_str = f" RMSE={cond_result.get('rmse', 'N/A')}" if "rmse" in cond_result else ""
                print(f"  {model}/{cond_name}: {status}{rmse_str} ({elapsed:.1f}s)", file=sys.stderr)

    manifest_path = output / "comparison-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False, allow_nan=False) + "\n")
    print(f"wrote {manifest_path}")
    return 0 if manifest["all_ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
