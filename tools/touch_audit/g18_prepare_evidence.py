#!/usr/bin/env python3
"""G18: Prepare touch evidence screenshots for three focus models.

For each model x condition, generates:
  - plain.png: full frame screenshot
  - overlay.png: full frame with touch region overlay
  - regions.json: session probe output with touch_regions
  - crops: chest and lower body crops with coordinate mapping metadata

Also generates a diagnostic layout (tall window) for lower body visibility.

Does NOT generate observation conclusions. Only prepares evidence for Gemini
to mark test points.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PLAYER = REPO / "build" / "plasma" / "bin" / "nekomote_player"
PROBE = REPO / "build" / "plasma" / "bin" / "nekomote_playback_probe"
ASSETS = REPO / "assets" / "characters"
EVIDENCE = REPO / "build" / "evidence" / "g28-touch-evidence"
GPU_LOCK = REPO / "build" / "gpu-validation.lock"

MODELS = ["fraise-france-maid-b", "cinnamon-maid-b", "coconut-santa-b"]

CONDITIONS = [
    {"name": "default_idle", "steps": 0, "origin_x": 0, "origin_y": 400, "scale": 1.0,
     "width": 800, "height": 1080},
    {"name": "idle_120", "steps": 120, "origin_x": 0, "origin_y": 400, "scale": 1.0,
     "width": 800, "height": 1080},
    {"name": "moved", "steps": 0, "origin_x": 80, "origin_y": 400, "scale": 1.0,
     "width": 800, "height": 1080},
    {"name": "scaled_half", "steps": 0, "origin_x": 0, "origin_y": 400, "scale": 0.5,
     "width": 800, "height": 1080},
]

DIAGNOSTIC = {"name": "lower_body_diag", "steps": 0, "origin_x": 0, "origin_y": 400,
              "scale": 1.0, "width": 800, "height": 2160}

ZONES = ["bust", "eye", "face", "head", "pant"]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def run_player(model: str, cond: dict, output: Path, overlay: bool) -> bool:
    model_path = ASSETS / model / "model.nkm.json"
    session_path = ASSETS / model / "session.nks.json"
    cmd = [
        "flock", str(GPU_LOCK),
        str(PLAYER),
        "--model", str(model_path),
        "--session", str(session_path),
        "--origin-x", str(cond["origin_x"]),
        "--origin-y", str(cond["origin_y"]),
        "--scale", str(cond["scale"]),
        "--width", str(cond["width"]),
        "--height", str(cond["height"]),
        "--seed", "1",
        "--steps", str(cond["steps"]),
        "--mute", "--transparent", "--no-save",
        "--capture", str(output),
    ]
    if overlay:
        cmd.append("--show-touch-regions")
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    if result.returncode != 0:
        print(f"  FAILED: {result.stderr.strip()}", file=sys.stderr)
        return False
    return output.exists()


def run_probe(model: str, cond: dict, output_dir: Path) -> dict | None:
    model_path = ASSETS / model / "model.nkm.json"
    session_path = ASSETS / model / "session.nks.json"
    input_path = output_dir / "regions.input.json"
    output_path = output_dir / "regions.json"
    input_data = {
        "steps": cond["steps"],
        "random_seed": 1,
        "reaction_seed": 1,
        "origin": [cond["origin_x"], cond["origin_y"]],
        "scale": cond["scale"],
    }
    input_path.write_text(json.dumps(input_data, indent=2))
    cmd = [str(PROBE), str(model_path), str(session_path), str(input_path), str(output_path)]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    if result.returncode != 0:
        print(f"  probe FAILED: {result.stderr.strip()}", file=sys.stderr)
        return None
    return json.loads(output_path.read_text())


def crop_image(src: Path, dst: Path, x0: int, y0: int, x1: int, y1: int) -> bool:
    try:
        from PIL import Image
    except ImportError:
        print("  PIL not available, skipping crop", file=sys.stderr)
        return False
    img = Image.open(src)
    x0 = max(0, x0)
    y0 = max(0, y0)
    x1 = min(img.width, x1)
    y1 = min(img.height, y1)
    if x1 <= x0 or y1 <= y0:
        return False
    crop = img.crop((x0, y0, x1, y1))
    crop.save(str(dst))
    return True


def zone_to_png(center: list, radius: list, width: int, height: int) -> dict:
    cx = width / 2 + center[0]
    cy = height / 2 + center[1]
    return {
        "png_center": [round(cx, 1), round(cy, 1)],
        "png_radius": [round(radius[0], 1), round(radius[1], 1)],
        "png_bbox": [
            int(math.floor(cx - radius[0])),
            int(math.floor(cy - radius[1])),
            int(math.ceil(cx + radius[0])),
            int(math.ceil(cy + radius[1])),
        ],
    }


def prepare_condition(model: str, cond: dict, base_dir: Path) -> dict:
    name = cond["name"]
    cond_dir = base_dir / name
    cond_dir.mkdir(parents=True, exist_ok=True)
    w, h = cond["width"], cond["height"]

    print(f"  {name} ({w}x{h})...")
    plain_path = cond_dir / "plain.png"
    overlay_path = cond_dir / "overlay.png"

    if not run_player(model, cond, plain_path, overlay=False):
        return {"ok": False, "error": "plain capture failed"}
    if not run_player(model, cond, overlay_path, overlay=True):
        return {"ok": False, "error": "overlay capture failed"}

    probe_data = run_probe(model, cond, cond_dir)
    if probe_data is None:
        return {"ok": False, "error": "probe failed"}

    touch_regions = probe_data.get("touch_regions", {})
    mirror = probe_data.get("presentation_mirror", False)

    manifest = {
        "model": model,
        "condition": name,
        "width": w,
        "height": h,
        "origin_x": cond["origin_x"],
        "origin_y": cond["origin_y"],
        "scale": cond["scale"],
        "steps": cond["steps"],
        "seed": 1,
        "mirror": mirror,
        "plain": {"path": str(plain_path), "sha256": sha256_file(plain_path)},
        "overlay": {"path": str(overlay_path), "sha256": sha256_file(overlay_path)},
        "touch_regions": {},
    }

    for zone in ZONES:
        r = touch_regions.get(zone)
        if r is None:
            manifest["touch_regions"][zone] = None
            continue
        png_info = zone_to_png(r["center"], r["radius"], w, h)
        manifest["touch_regions"][zone] = {
            "centered_center": r["center"],
            "centered_radius": r["radius"],
            **png_info,
        }

    crops_dir = cond_dir / "crops"
    crops_dir.mkdir(exist_ok=True)
    manifest["crops"] = {}

    bust = manifest["touch_regions"].get("bust")
    if bust and bust["png_bbox"][3] > 0:
        bx0, by0, bx1, by1 = bust["png_bbox"]
        margin = 50
        for img_name in ["plain.png", "overlay.png"]:
            src = cond_dir / img_name
            dst = crops_dir / f"chest_{img_name}"
            if crop_image(src, dst, bx0 - margin, by0 - margin, bx1 + margin, by1 + margin):
                manifest["crops"].setdefault("chest", {})[img_name] = {
                    "path": str(dst),
                    "sha256": sha256_file(dst),
                    "source_bbox": [bx0 - margin, by0 - margin, bx1 + margin, by1 + margin],
                    "zone_bbox": bust["png_bbox"],
                }

    pant = manifest["touch_regions"].get("pant")
    if pant and pant["png_bbox"][1] < h:
        px0, py0, px1, py1 = pant["png_bbox"]
        margin = 50
        for img_name in ["plain.png", "overlay.png"]:
            src = cond_dir / img_name
            dst = crops_dir / f"lower_{img_name}"
            if crop_image(src, dst, px0 - margin, py0 - margin, px1 + margin, py1 + margin):
                manifest["crops"].setdefault("lower", {})[img_name] = {
                    "path": str(dst),
                    "sha256": sha256_file(dst),
                    "source_bbox": [px0 - margin, py0 - margin, px1 + margin, py1 + margin],
                    "zone_bbox": pant["png_bbox"],
                }

    manifest_path = cond_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2))
    manifest["ok"] = True
    return manifest


def main() -> int:
    if not PLAYER.exists():
        print(f"Player not found: {PLAYER}", file=sys.stderr)
        return 1
    if not PROBE.exists():
        print(f"Probe not found: {PROBE}", file=sys.stderr)
        return 1

    EVIDENCE.mkdir(parents=True, exist_ok=True)
    all_manifests = {"models": {}, "head_commit": None}

    try:
        all_manifests["head_commit"] = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip()
    except Exception:
        pass

    for model in MODELS:
        print(f"\n=== {model} ===")
        model_dir = EVIDENCE / model
        model_dir.mkdir(parents=True, exist_ok=True)
        model_manifest = {"conditions": {}, "diagnostic": {}}

        for cond in CONDITIONS:
            result = prepare_condition(model, cond, model_dir)
            model_manifest["conditions"][cond["name"]] = result

        print(f"  {DIAGNOSTIC['name']} ({DIAGNOSTIC['width']}x{DIAGNOSTIC['height']})...")
        diag_result = prepare_condition(model, DIAGNOSTIC, model_dir)
        model_manifest["diagnostic"] = diag_result

        all_manifests["models"][model] = model_manifest

    manifest_path = EVIDENCE / "manifest.json"
    manifest_path.write_text(json.dumps(all_manifests, indent=2))
    print(f"\nEvidence written to {EVIDENCE}")
    print(f"Manifest: {manifest_path}")

    ok_count = 0
    total = 0
    for model, mm in all_manifests["models"].items():
        for cname, cm in mm["conditions"].items():
            total += 1
            if cm.get("ok"):
                ok_count += 1
        total += 1
        if mm["diagnostic"].get("ok"):
            ok_count += 1
    print(f"Conditions: {ok_count}/{total} OK")
    return 0 if ok_count == total else 1


if __name__ == "__main__":
    sys.exit(main())
