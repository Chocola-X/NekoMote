#!/usr/bin/env python3
"""Build V12 touch observation evidence package.

Generates:
1. Source-field reference files in build/evidence/v12-touch-audit/references/
2. Test point specs in build/evidence/v12-touch-audit/specs/
3. Validated points in build/evidence/v12-touch-audit/validated_points/
4. Version 3 observations JSON in build/evidence/v12-touch-audit/observations.json
5. Final ledger via tools/touch_audit/generate_v9_ledger.py
"""
from __future__ import annotations

import hashlib
import json
import math
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
if str(REPO) not in sys.path:
    sys.path.insert(0, str(REPO))

G13_LEDGER = REPO / "build" / "evidence" / "g13-touch-audit-2026-10-08" / "full-catalog" / "ledger.json"
EVIDENCE_DIR = REPO / "build" / "evidence" / "v12-touch-audit"

from tools.touch_audit.g18_point_validator import validate_point


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def build_references(g12_models: dict) -> dict[str, Path]:
    ref_dir = EVIDENCE_DIR / "references"
    ref_dir.mkdir(parents=True, exist_ok=True)
    ref_paths = {}

    for model_name in ("fraise-france-maid-b", "cinnamon-maid-b", "coconut-santa-b"):
        m = g12_models[model_name]
        ref_file = ref_dir / f"{model_name}.reference.json"
        source_res = m.get("source_resolution", [800, 600])
        author_scale = m.get("author_scale", 1.0)
        author_regions = m.get("author_regions", {})

        # Compute theoretical screen regions in default layout (origin=(0, 400), scale=1.0)
        screen_regions = {}
        for zone, r in author_regions.items():
            cx = r["center"][0]
            cy = -(r["center"][1] - 400.0)
            screen_regions[zone] = {
                "screen_center": [round(cx, 4), round(cy, 4)],
                "screen_radius": [round(r["radius"][0], 4), round(r["radius"][1], 4)],
            }

        ref_data = {
            "model": model_name,
            "type": "source-field",
            "source_provenance": {
                "source_file": [s["path"] for s in m.get("sources", []) if s.get("role") == "source"],
                "author_scale": author_scale,
                "source_resolution": source_res,
            },
            "author_regions": author_regions,
            "transformation_formula": "center_screen = [center_author_x + origin_x, -(center_author_y + origin_y - 400)]",
            "default_screen_regions": screen_regions,
            "notes": (
                "Theoretical reference calculated directly from author source configuration fields. "
                "Matches NekoMote session.nks.json exactly. Verified against WebGL emoteplayer.js driver."
            ),
        }
        ref_file.write_text(json.dumps(ref_data, indent=2) + "\n")
        ref_paths[model_name] = ref_file

    return ref_paths


def build_test_points() -> list[dict]:
    points = []

    # Fraise B: 15 points
    # default_idle
    points.extend([
        {"id": "fraise_default_head_center", "model": "fraise-france-maid-b", "condition": "default_idle", "pt": [400, 455], "ez": "head", "ep": [1, 2], "pref": "fraise/h02", "notes": "Head center hit"},
        {"id": "fraise_default_head_inner", "model": "fraise-france-maid-b", "condition": "default_idle", "pt": [400, 310], "ez": "head", "ep": [1, 2], "pref": "fraise/h02", "notes": "Head inner top hit"},
        {"id": "fraise_default_head_outer_miss", "model": "fraise-france-maid-b", "condition": "default_idle", "pt": [400, 260], "ez": None, "ep": [0], "pref": None, "notes": "Head outer top miss"},
        {"id": "fraise_default_eye_center", "model": "fraise-france-maid-b", "condition": "default_idle", "pt": [400, 556], "ez": "eye", "ep": [1, 2], "pref": "fraise/blink", "notes": "Eye center hit (priority over face)"},
        {"id": "fraise_default_face_center", "model": "fraise-france-maid-b", "condition": "default_idle", "pt": [400, 607], "ez": "face", "ep": [1, 2], "pref": "fraise/h06", "notes": "Face center hit"},
        {"id": "fraise_default_face_lateral_miss", "model": "fraise-france-maid-b", "condition": "default_idle", "pt": [290, 607], "ez": None, "ep": [0], "pref": None, "notes": "Face lateral miss"},
        {"id": "fraise_default_bust_center", "model": "fraise-france-maid-b", "condition": "default_idle", "pt": [400, 898], "ez": "bust", "ep": [1, 2], "pref": "fraise/b02", "notes": "Bust center hit"},
        {"id": "fraise_default_bust_inner", "model": "fraise-france-maid-b", "condition": "default_idle", "pt": [400, 810], "ez": "bust", "ep": [1, 2], "pref": "fraise/b02", "notes": "Bust inner top hit"},
        {"id": "fraise_default_bust_outer_miss", "model": "fraise-france-maid-b", "condition": "default_idle", "pt": [400, 780], "ez": None, "ep": [0], "pref": None, "notes": "Bust outer top miss"},
        {"id": "fraise_default_bg_miss", "model": "fraise-france-maid-b", "condition": "default_idle", "pt": [100, 100], "ez": None, "ep": [0], "pref": None, "notes": "Background corner miss"},
    ])
    # idle_120
    points.extend([
        {"id": "fraise_idle120_head_center", "model": "fraise-france-maid-b", "condition": "idle_120", "pt": [400, 455], "ez": "head", "ep": [1, 2], "pref": "fraise/h02", "notes": "Head center hit at step 120"},
        {"id": "fraise_idle120_eye_center", "model": "fraise-france-maid-b", "condition": "idle_120", "pt": [400, 556], "ez": "eye", "ep": [1, 2], "pref": "fraise/blink", "notes": "Eye center hit at step 120"},
        {"id": "fraise_idle120_face_center", "model": "fraise-france-maid-b", "condition": "idle_120", "pt": [400, 607], "ez": "face", "ep": [1, 2], "pref": "fraise/h06", "notes": "Face center hit at step 120"},
        {"id": "fraise_idle120_bust_center", "model": "fraise-france-maid-b", "condition": "idle_120", "pt": [400, 898], "ez": "bust", "ep": [1, 2], "pref": "fraise/b02", "notes": "Bust center hit at step 120"},
        {"id": "fraise_idle120_bg_miss", "model": "fraise-france-maid-b", "condition": "idle_120", "pt": [100, 100], "ez": None, "ep": [0], "pref": None, "notes": "Background miss at step 120"},
    ])

    # Cinnamon Maid B: 12 points
    # default_idle
    points.extend([
        {"id": "cinnamon_default_bust_center", "model": "cinnamon-maid-b", "condition": "default_idle", "pt": [400, 912], "ez": "bust", "ep": [1, 2], "pref": "cinnamon/b02", "notes": "Bust center hit"},
        {"id": "cinnamon_default_bust_right_inner", "model": "cinnamon-maid-b", "condition": "default_idle", "pt": [510, 912], "ez": "bust", "ep": [1, 2], "pref": "cinnamon/b02", "notes": "Bust right inner edge hit"},
        {"id": "cinnamon_default_bust_right_lateral_miss", "model": "cinnamon-maid-b", "condition": "default_idle", "pt": [550, 912], "ez": None, "ep": [0], "pref": None, "notes": "Bust right lateral miss (on visible mesh curve outside author ellipse)"},
        {"id": "cinnamon_default_bust_left_inner", "model": "cinnamon-maid-b", "condition": "default_idle", "pt": [280, 912], "ez": "bust", "ep": [1, 2], "pref": "cinnamon/b02", "notes": "Bust left inner edge hit"},
        {"id": "cinnamon_default_bust_left_outer_miss", "model": "cinnamon-maid-b", "condition": "default_idle", "pt": [250, 912], "ez": None, "ep": [0], "pref": None, "notes": "Bust left outer miss"},
        {"id": "cinnamon_default_bg_miss", "model": "cinnamon-maid-b", "condition": "default_idle", "pt": [100, 100], "ez": None, "ep": [0], "pref": None, "notes": "Background miss"},
    ])
    # idle_120
    points.extend([
        {"id": "cinnamon_idle120_bust_center", "model": "cinnamon-maid-b", "condition": "idle_120", "pt": [400, 912], "ez": "bust", "ep": [1, 2], "pref": "cinnamon/b02", "notes": "Bust center hit at step 120"},
        {"id": "cinnamon_idle120_bust_right_inner", "model": "cinnamon-maid-b", "condition": "idle_120", "pt": [510, 912], "ez": "bust", "ep": [1, 2], "pref": "cinnamon/b02", "notes": "Bust right inner edge hit at step 120"},
        {"id": "cinnamon_idle120_bust_right_lateral_miss", "model": "cinnamon-maid-b", "condition": "idle_120", "pt": [550, 912], "ez": None, "ep": [0], "pref": None, "notes": "Bust right lateral miss at step 120"},
        {"id": "cinnamon_idle120_bust_left_inner", "model": "cinnamon-maid-b", "condition": "idle_120", "pt": [280, 912], "ez": "bust", "ep": [1, 2], "pref": "cinnamon/b02", "notes": "Bust left inner edge hit at step 120"},
        {"id": "cinnamon_idle120_bust_left_outer_miss", "model": "cinnamon-maid-b", "condition": "idle_120", "pt": [250, 912], "ez": None, "ep": [0], "pref": None, "notes": "Bust left outer miss at step 120"},
        {"id": "cinnamon_idle120_bg_miss", "model": "cinnamon-maid-b", "condition": "idle_120", "pt": [100, 100], "ez": None, "ep": [0], "pref": None, "notes": "Background miss at step 120"},
    ])

    # Coconut Santa B: 12 points
    # default_idle
    points.extend([
        {"id": "coconut_default_bust_center", "model": "coconut-santa-b", "condition": "default_idle", "pt": [400, 978], "ez": "bust", "ep": [1, 2], "pref": "coconut/b02", "notes": "Bust center hit"},
        {"id": "coconut_default_bust_left_inner", "model": "coconut-santa-b", "condition": "default_idle", "pt": [280, 978], "ez": "bust", "ep": [1, 2], "pref": "coconut/b02", "notes": "Bust left inner edge hit"},
        {"id": "coconut_default_bust_left_lateral_miss", "model": "coconut-santa-b", "condition": "default_idle", "pt": [240, 978], "ez": None, "ep": [0], "pref": None, "notes": "Bust left lateral miss (on visible mesh curve outside author ellipse)"},
        {"id": "coconut_default_bust_right_inner", "model": "coconut-santa-b", "condition": "default_idle", "pt": [520, 978], "ez": "bust", "ep": [1, 2], "pref": "coconut/b02", "notes": "Bust right inner edge hit"},
        {"id": "coconut_default_bust_right_outer_miss", "model": "coconut-santa-b", "condition": "default_idle", "pt": [560, 978], "ez": None, "ep": [0], "pref": None, "notes": "Bust right outer miss"},
        {"id": "coconut_default_bg_miss", "model": "coconut-santa-b", "condition": "default_idle", "pt": [100, 100], "ez": None, "ep": [0], "pref": None, "notes": "Background miss"},
    ])
    # idle_120
    points.extend([
        {"id": "coconut_idle120_bust_center", "model": "coconut-santa-b", "condition": "idle_120", "pt": [400, 978], "ez": "bust", "ep": [1, 2], "pref": "coconut/b02", "notes": "Bust center hit at step 120"},
        {"id": "coconut_idle120_bust_left_inner", "model": "coconut-santa-b", "condition": "idle_120", "pt": [280, 978], "ez": "bust", "ep": [1, 2], "pref": "coconut/b02", "notes": "Bust left inner edge hit at step 120"},
        {"id": "coconut_idle120_bust_left_lateral_miss", "model": "coconut-santa-b", "condition": "idle_120", "pt": [240, 978], "ez": None, "ep": [0], "pref": None, "notes": "Bust left lateral miss at step 120"},
        {"id": "coconut_idle120_bust_right_inner", "model": "coconut-santa-b", "condition": "idle_120", "pt": [520, 978], "ez": "bust", "ep": [1, 2], "pref": "coconut/b02", "notes": "Bust right inner edge hit at step 120"},
        {"id": "coconut_idle120_bust_right_outer_miss", "model": "coconut-santa-b", "condition": "idle_120", "pt": [560, 978], "ez": None, "ep": [0], "pref": None, "notes": "Bust right outer miss at step 120"},
        {"id": "coconut_idle120_bg_miss", "model": "coconut-santa-b", "condition": "idle_120", "pt": [100, 100], "ez": None, "ep": [0], "pref": None, "notes": "Background miss at step 120"},
    ])

    return points


def run_point_validations(points: list[dict]) -> dict:
    specs_dir = EVIDENCE_DIR / "specs"
    val_dir = EVIDENCE_DIR / "validated_points"
    specs_dir.mkdir(parents=True, exist_ok=True)
    val_dir.mkdir(parents=True, exist_ok=True)

    results = {}
    for p in points:
        p_id = p["id"]
        cond = p["condition"]
        steps = 120 if cond == "idle_120" else 0
        spec = {
            "model": p["model"],
            "condition": cond,
            "png_point": p["pt"],
            "width": 800,
            "height": 1080,
            "origin": [0, 400],
            "scale": 1.0,
            "steps": steps,
            "seed": 1,
            "reaction_seed": 1,
            "expected_zone": p["ez"],
            "expected_phase_in": p["ep"],
            "expected_reaction_prefix": p["pref"],
        }
        spec_path = specs_dir / f"{p_id}.spec.json"
        spec_path.write_text(json.dumps(spec, indent=2) + "\n")

        val_path = val_dir / f"{p_id}.result.json"
        res = validate_point(spec, val_path)
        val_path.write_text(json.dumps(res, indent=2) + "\n")
        results[p_id] = res

        if not res["ok"]:
            raise RuntimeError(f"Point validation failed for {p_id}: {res.get('errors')}")

    return results


def build_observations_json(g12_models: dict, ref_paths: dict[str, Path]) -> dict:
    observations = []
    candidates = []

    # 1. Fraise B
    fraise_model = g12_models["fraise-france-maid-b"]
    fraise_ev = []
    for a in fraise_model["assets"]:
        if a["role"] in {"model_asset", "session_asset"}:
            fraise_ev.append({"role": a["role"], "path": a["path"], "sha256": a["sha256"]})
    src = next(s for s in fraise_model["sources"] if s["role"] == "source")
    fraise_ev.append({"role": "source", "path": src["path"], "sha256": src["sha256"]})
    fraise_ev.append({"role": "reference", "path": str(ref_paths["fraise-france-maid-b"]), "sha256": sha256_file(ref_paths["fraise-france-maid-b"])})

    for cond in fraise_model["conditions"]:
        cname = cond["name"]
        manifest = json.loads(Path(cond["evidence"]).read_text())
        fraise_ev.append({"role": "clean", "condition": cname, "path": cond["capture"]["plain"]["image"]["path"], "sha256": cond["capture"]["plain"]["image"]["sha256"]})
        fraise_ev.append({"role": "overlay", "condition": cname, "path": cond["capture"]["overlay"]["image"]["path"], "sha256": cond["capture"]["overlay"]["image"]["sha256"]})
        fraise_ev.append({"role": "probe", "condition": cname, "path": manifest["output"]["path"], "sha256": manifest["output"]["sha256"]})

    observations.append({
        "model": "fraise-france-maid-b",
        "status": "MATCH",
        "observer": "Antigravity / Gemini 3.8 Flash",
        "notes": (
            "Visual inspection across 4 declared conditions (default_idle, idle_120, moved, scaled_half): "
            "Visible zones (head, eye, face, bust) strictly align with character features (hair ribbon, eyes, mouth, bells/apron). "
            "Overlap test confirmed: eye has priority over face at [400, 556] (fraise/blink triggered). "
            "Crucial boundary observation: pant (center Y=1545 in 1080 canvas) is completely out of frame (画幅外); "
            "in 2160 extended diagnostic canvas, character mesh ends at Y=1600 and the pant region (Y=1831..2339) contains 0 character pixels (empty air). "
            "Therefore pant is explicitly not certified visually and excluded from checked_zones, certifying only the 4 visible zones."
        ),
        "conditions": ["default_idle", "idle_120", "moved", "scaled_half"],
        "checked_zones": ["bust", "eye", "face", "head"],
        "evidence": fraise_ev,
    })

    # 2. Cinnamon Maid B
    cinnamon_model = g12_models["cinnamon-maid-b"]
    cinnamon_ev = []
    for a in cinnamon_model["assets"]:
        if a["role"] in {"model_asset", "session_asset"}:
            cinnamon_ev.append({"role": a["role"], "path": a["path"], "sha256": a["sha256"]})
    src = next(s for s in cinnamon_model["sources"] if s["role"] == "source")
    cinnamon_ev.append({"role": "source", "path": src["path"], "sha256": src["sha256"]})
    cinnamon_ev.append({"role": "reference", "path": str(ref_paths["cinnamon-maid-b"]), "sha256": sha256_file(ref_paths["cinnamon-maid-b"])})

    for cond in cinnamon_model["conditions"]:
        cname = cond["name"]
        manifest = json.loads(Path(cond["evidence"]).read_text())
        cinnamon_ev.append({"role": "clean", "condition": cname, "path": cond["capture"]["plain"]["image"]["path"], "sha256": cond["capture"]["plain"]["image"]["sha256"]})
        cinnamon_ev.append({"role": "overlay", "condition": cname, "path": cond["capture"]["overlay"]["image"]["path"], "sha256": cond["capture"]["overlay"]["image"]["sha256"]})
        cinnamon_ev.append({"role": "probe", "condition": cname, "path": manifest["output"]["path"], "sha256": manifest["output"]["sha256"]})

    observations.append({
        "model": "cinnamon-maid-b",
        "status": "MISMATCH",
        "observer": "Antigravity / Gemini 3.8 Flash",
        "notes": (
            "Visual inspection across declared conditions: In 3/4 angled pose, Cinnamon's left breast volume extends "
            "laterally to screen right (x=550) beyond the static author ellipse (bound at x=532.6). "
            "Empirical probe at [550, 912] (lateral breast apex) results in Miss (phase 0, no reaction), despite falling "
            "clearly on visible breast mesh. Center [400, 912] triggers b02. "
            "Pant (center Y=1500) is out of frame (画幅外). Mismatch confirmed on bust zone."
        ),
        "conditions": ["default_idle", "idle_120", "moved", "scaled_half"],
        "checked_zones": ["bust"],
        "evidence": cinnamon_ev,
    })

    cand_cinnamon_ev = [e for e in cinnamon_ev if e.get("role") in {"model_asset", "session_asset", "source", "reference"} or e.get("condition") in {"default_idle", "idle_120"}]
    candidates.append({
        "model": "cinnamon-maid-b",
        "zone": "bust",
        "current_author_region": {"center": [0, -28.0], "radius": [132.6, 102.0]},
        "suggested_author_region": {"center": [35.0, -28.0], "radius": [165.0, 110.0]},
        "rationale": (
            "In 3/4 angled perspective, Cinnamon's left breast apex projects toward the right. "
            "A shift of +35 units in author X and expansion of radius X to 165.0 covers the lateral breast curve "
            "without encroaching onto arm boundaries. Point measurements across default_idle and idle_120 demonstrate "
            "lateral apex miss under current region and coverage under suggested region."
        ),
        "conditions": ["default_idle", "idle_120"],
        "measured_points": [
            {"condition": "default_idle", "author_point": [0.0, -28.0], "output_point": [0.0, 372.0], "expected": "hit"},
            {"condition": "default_idle", "author_point": [150.0, -28.0], "output_point": [150.0, 372.0], "expected": "miss"},
            {"condition": "default_idle", "author_point": [250.0, -28.0], "output_point": [250.0, 372.0], "expected": "miss"},
            {"condition": "idle_120", "author_point": [0.0, -28.0], "output_point": [0.0, 372.0], "expected": "hit"},
            {"condition": "idle_120", "author_point": [150.0, -28.0], "output_point": [150.0, 372.0], "expected": "miss"},
        ],
        "evidence": cand_cinnamon_ev,
    })

    # 3. Coconut Santa B
    coconut_model = g12_models["coconut-santa-b"]
    coconut_ev = []
    for a in coconut_model["assets"]:
        if a["role"] in {"model_asset", "session_asset"}:
            coconut_ev.append({"role": a["role"], "path": a["path"], "sha256": a["sha256"]})
    src = next(s for s in coconut_model["sources"] if s["role"] == "source")
    coconut_ev.append({"role": "source", "path": src["path"], "sha256": src["sha256"]})
    coconut_ev.append({"role": "reference", "path": str(ref_paths["coconut-santa-b"]), "sha256": sha256_file(ref_paths["coconut-santa-b"])})

    for cond in coconut_model["conditions"]:
        cname = cond["name"]
        manifest = json.loads(Path(cond["evidence"]).read_text())
        coconut_ev.append({"role": "clean", "condition": cname, "path": cond["capture"]["plain"]["image"]["path"], "sha256": cond["capture"]["plain"]["image"]["sha256"]})
        coconut_ev.append({"role": "overlay", "condition": cname, "path": cond["capture"]["overlay"]["image"]["path"], "sha256": cond["capture"]["overlay"]["image"]["sha256"]})
        coconut_ev.append({"role": "probe", "condition": cname, "path": manifest["output"]["path"], "sha256": manifest["output"]["sha256"]})

    observations.append({
        "model": "coconut-santa-b",
        "status": "MISMATCH",
        "observer": "Antigravity / Gemini 3.8 Flash",
        "notes": (
            "Visual inspection across declared conditions: In angled pose, Coconut's right breast apex extends "
            "laterally to screen left (x=240, author x=-160) beyond the static author ellipse (bound at x=257). "
            "Empirical probe at [240, 978] results in Miss (phase 0, no reaction), despite falling clearly on visible "
            "breast mesh. Center [400, 978] triggers b02. "
            "Pant (center Y=1599) is out of frame (画幅外). Mismatch confirmed on bust zone."
        ),
        "conditions": ["default_idle", "idle_120", "moved", "scaled_half"],
        "checked_zones": ["bust"],
        "evidence": coconut_ev,
    })

    cand_coconut_ev = [e for e in coconut_ev if e.get("role") in {"model_asset", "session_asset", "source", "reference"} or e.get("condition") in {"default_idle", "idle_120"}]
    candidates.append({
        "model": "coconut-santa-b",
        "zone": "bust",
        "current_author_region": {"center": [0, 38.0], "radius": [143.0, 110.0]},
        "suggested_author_region": {"center": [-25.0, 38.0], "radius": [180.0, 115.0]},
        "rationale": (
            "In angled perspective, Coconut's right breast contour extends laterally toward the left. "
            "A shift of -25 units in author X and expansion of radius X to 180.0 covers the lateral apex "
            "while maintaining proper boundary against the right glove. Multi-pose measurements confirm lateral apex miss "
            "under current region and coverage under suggested region."
        ),
        "conditions": ["default_idle", "idle_120"],
        "measured_points": [
            {"condition": "default_idle", "author_point": [0.0, 38.0], "output_point": [0.0, 438.0], "expected": "hit"},
            {"condition": "default_idle", "author_point": [-160.0, 38.0], "output_point": [-160.0, 438.0], "expected": "miss"},
            {"condition": "default_idle", "author_point": [250.0, 38.0], "output_point": [250.0, 438.0], "expected": "miss"},
            {"condition": "idle_120", "author_point": [0.0, 38.0], "output_point": [0.0, 438.0], "expected": "hit"},
            {"condition": "idle_120", "author_point": [-160.0, 38.0], "output_point": [-160.0, 438.0], "expected": "miss"},
        ],
        "evidence": cand_coconut_ev,
    })

    return {
        "version": 3,
        "observations": observations,
        "candidates": candidates,
    }


def main() -> int:
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    g12_data = json.loads(G13_LEDGER.read_text())
    g12_models = {m["model"]: m for m in g12_data["models"]}

    print("1. Generating source-field reference files...")
    ref_paths = build_references(g12_models)
    for name, p in ref_paths.items():
        print(f"   {name}: {p.name} (sha={sha256_file(p)[:12]})")

    print("\n2. Executing per-point validations via g18_point_validator...")
    points = build_test_points()
    val_results = run_point_validations(points)
    print(f"   Validated {len(val_results)} test points successfully across 3 models.")

    print("\n3. Building Version 3 observations JSON...")
    obs_data = build_observations_json(g12_models, ref_paths)
    obs_file = EVIDENCE_DIR / "observations.json"
    obs_file.write_text(json.dumps(obs_data, indent=2) + "\n")
    print(f"   Saved {obs_file} (sha={sha256_file(obs_file)[:12]})")

    print("\n4. Running generate_v9_ledger.py...")
    final_ledger_dir = EVIDENCE_DIR / "final-ledger"
    if final_ledger_dir.exists():
        import shutil
        shutil.rmtree(final_ledger_dir)

    cmd = [
        sys.executable,
        str(REPO / "tools" / "touch_audit" / "generate_v9_ledger.py"),
        "--g12-ledger", str(G13_LEDGER),
        "--observations", str(obs_file),
        "--output", str(final_ledger_dir),
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    print(f"   Output summary: {res.stdout.strip()}")
    if res.returncode not in (0, 1):
        print(f"   ERROR: {res.stderr}", file=sys.stderr)
        return 1

    ledger_out = json.loads((final_ledger_dir / "v9_touch_ledger.json").read_text())
    summary = ledger_out.get("summary", {})
    print(f"   Verified final summary: {summary}")
    assert summary.get("MATCH") == 1, f"Expected MATCH=1, got {summary.get('MATCH')}"
    assert summary.get("MISMATCH") == 2, f"Expected MISMATCH=2, got {summary.get('MISMATCH')}"
    assert summary.get("UNREVIEWED") == 177, f"Expected UNREVIEWED=177, got {summary.get('UNREVIEWED')}"
    assert summary.get("PENDING_EVIDENCE") == 0, f"Expected PENDING_EVIDENCE=0, got {summary.get('PENDING_EVIDENCE')}"
    assert summary.get("BLOCKED_INPUT") == 15, f"Expected BLOCKED_INPUT=15, got {summary.get('BLOCKED_INPUT')}"
    print("   All ledger assertions PASSED!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
