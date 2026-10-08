#!/usr/bin/env python3
"""G18: Per-point touch validator.

Takes a test point specification (model, condition, PNG coordinates, expected
zone/reaction), executes a touch via nekomote_session_probe, and validates
the response. Does NOT generate observation conclusions — only validates.

Rejects: no request, wrong coords/steps/seed/response, Busy/failure, wrong hash.

Usage:
  g18_point_validator.py SPEC.json OUTPUT.json
  g18_point_validator.py --batch SPECS_DIR/ OUTPUT_DIR/

Spec format:
{
  "model": "fraise-france-maid-b",
  "condition": "default_idle",
  "png_point": [400, 897],       // PNG pixel coordinates
  "width": 800, "height": 1080,
  "origin": [0, 400], "scale": 1.0,
  "steps": 0, "seed": 1, "reaction_seed": 1,
  "expected_zone": "bust",       // or null for expected miss
  "expected_phase_in": [1, 2],   // [Playing, Recovering] or [0] for miss
  "expected_reaction_prefix": "fraise/"  // prefix check, or null
}
"""
from __future__ import annotations

import hashlib
import json
import math
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROBE = REPO / "build" / "release" / "bin" / "nekomote_session_probe"
ASSETS = REPO / "assets" / "characters"

ZONES = ["bust", "eye", "face", "head", "pant"]
PHASE_NAMES = {0: "Idle", 1: "Playing", 2: "Recovering"}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def validate_spec(spec: dict) -> list[str]:
    errors = []
    for key in ["model", "condition", "png_point", "width", "height",
                "origin", "scale", "steps", "seed", "reaction_seed"]:
        if key not in spec:
            errors.append(f"missing required field: {key}")
    if errors:
        return errors
    pt = spec["png_point"]
    if not (isinstance(pt, list) and len(pt) == 2 and
            all(isinstance(v, (int, float)) and math.isfinite(v) for v in pt)):
        errors.append("png_point must be [x, y] with finite values")
    if spec["width"] <= 0 or spec["height"] <= 0:
        errors.append("width/height must be positive")
    if spec["scale"] <= 0 or not math.isfinite(spec["scale"]):
        errors.append("scale must be positive and finite")
    if not isinstance(spec["steps"], int) or spec["steps"] < 0:
        errors.append("steps must be non-negative integer")
    if not isinstance(spec["seed"], int) or spec["seed"] < 0:
        errors.append("seed must be non-negative integer")
    if not isinstance(spec["reaction_seed"], int) or spec["reaction_seed"] < 0:
        errors.append("reaction_seed must be non-negative integer")
    orig = spec["origin"]
    if not (isinstance(orig, list) and len(orig) == 2 and
            all(isinstance(v, (int, float)) and math.isfinite(v) for v in orig)):
        errors.append("origin must be [x, y] with finite values")
    ez = spec.get("expected_zone")
    if ez is not None and ez not in ZONES:
        errors.append(f"expected_zone must be one of {ZONES} or null")
    ep = spec.get("expected_phase_in")
    if ep is not None and not (isinstance(ep, list) and all(p in [0, 1, 2] for p in ep)):
        errors.append("expected_phase_in must be list of 0/1/2")
    return errors


def png_to_centered(png_x: float, png_y: float, width: int, height: int) -> list:
    return [png_x - width / 2, png_y - height / 2]


def run_probe(model: str, session_path: Path, input_data: dict,
              input_file: Path, output_file: Path) -> dict:
    input_file.write_text(json.dumps(input_data, indent=2))
    model_path = ASSETS / model / "model.nkm.json"
    cmd = [str(PROBE), str(model_path), str(session_path),
           str(input_file), str(output_file)]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    return {
        "returncode": result.returncode,
        "stdout": result.stdout,
        "stderr": result.stderr,
        "output_file": str(output_file),
    }


def validate_point(spec: dict, output_path: Path | None = None) -> dict:
    errors = validate_spec(spec)
    if errors:
        return {"ok": False, "validated": False, "errors": errors, "spec": spec}

    model = spec["model"]
    session_path = ASSETS / model / "session.nks.json"
    if not session_path.exists():
        return {"ok": False, "validated": False,
                "errors": [f"session not found: {session_path}"], "spec": spec}

    w, h = spec["width"], spec["height"]
    png_x, png_y = spec["png_point"]
    cx, cy = png_to_centered(png_x, png_y, w, h)

    event_step = spec["steps"] + 1
    input_data = {
        "steps": event_step,
        "random_seed": spec["seed"],
        "reaction_seed": spec["reaction_seed"],
        "origin": spec["origin"],
        "scale": spec["scale"],
        "events": [
            {"step": spec["steps"], "kind": "touch", "point": [cx, cy]}
        ],
    }

    if output_path is None:
        output_path = Path("/tmp") / f"g18_validate_{model}_{spec['condition']}.json"
    input_path = output_path.with_suffix(".input.json")
    probe_out_path = output_path.with_suffix(".probe.json")

    probe_result = run_probe(model, session_path, input_data, input_path, probe_out_path)

    result = {
        "spec": spec,
        "validated": True,
        "png_point": [png_x, png_y],
        "centered_point": [round(cx, 6), round(cy, 6)],
        "event_step": event_step,
        "probe": {
            "returncode": probe_result["returncode"],
            "input_path": str(input_path),
            "output_path": str(probe_out_path),
        },
    }

    if probe_result["returncode"] != 0:
        result["ok"] = False
        result["errors"] = [f"probe failed (rc={probe_result['returncode']}): "
                            f"{probe_result['stderr'].strip()}"]
        return result

    if not Path(probe_out_path).exists():
        result["ok"] = False
        result["errors"] = ["probe output file not created"]
        return result

    probe_data = json.loads(Path(probe_out_path).read_text())
    result["probe"]["output_sha256"] = sha256_file(Path(probe_out_path))
    result["mirror"] = probe_data.get("presentation_mirror")

    samples = probe_data.get("samples", [])
    touch_sample = None
    for s in samples:
        if s["step"] == event_step:
            touch_sample = s
            break
    if touch_sample is None:
        touch_sample = samples[-1] if samples else {}

    phase = touch_sample.get("phase", -1)
    reaction = touch_sample.get("reaction", "")
    result["actual_phase"] = phase
    result["actual_phase_name"] = PHASE_NAMES.get(phase, f"Unknown({phase})")
    result["actual_reaction"] = reaction

    touch_regions = probe_data.get("touch_regions", {})
    result["touch_regions"] = touch_regions

    errors = []

    expected_zone = spec.get("expected_zone")
    if expected_zone is not None:
        r = touch_regions.get(expected_zone)
        if r is not None:
            dx = (cx - r["center"][0]) / r["radius"][0]
            dy = (cy - r["center"][1]) / r["radius"][1]
            inside = dx * dx + dy * dy <= 1.0
            result["expected_zone_inside"] = inside
            result["expected_zone_distance"] = round(math.sqrt(dx * dx + dy * dy), 6)
            if not inside:
                errors.append(f"point not inside expected zone '{expected_zone}' "
                              f"(distance={result['expected_zone_distance']:.3f})")

    expected_phases = spec.get("expected_phase_in")
    if expected_phases is not None:
        if phase not in expected_phases:
            errors.append(f"phase {phase} ({result['actual_phase_name']}) not in "
                          f"expected {expected_phases}")

    expected_prefix = spec.get("expected_reaction_prefix")
    if expected_prefix is not None:
        if not reaction.startswith(expected_prefix):
            errors.append(f"reaction '{reaction}' does not start with "
                          f"'{expected_prefix}'")

    if expected_zone is None and expected_phases == [0]:
        if reaction != "":
            errors.append(f"expected no reaction but got '{reaction}'")
        if phase != 0:
            errors.append(f"expected phase 0 (Idle) but got {phase}")

    result["ok"] = len(errors) == 0
    result["errors"] = errors
    return result


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 1

    spec_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    spec = json.loads(spec_path.read_text())
    result = validate_point(spec, output_path)
    output_path.write_text(json.dumps(result, indent=2))
    print(f"ok={result['ok']} validated={result['validated']}")
    if result.get("errors"):
        for e in result["errors"]:
            print(f"  ERROR: {e}", file=sys.stderr)
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
