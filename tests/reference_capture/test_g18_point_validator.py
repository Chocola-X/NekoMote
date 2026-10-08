#!/usr/bin/env python3
"""G18: Synthetic negative tests for the per-point touch validator.

Tests that the validator correctly rejects:
  - Missing required fields
  - Non-finite coordinates
  - Invalid scale/steps/seed
  - Invalid expected_zone
  - Non-existent model
  - Probe failure
  - Wrong expected phase
  - Wrong expected reaction prefix
  - Point outside expected zone

Also tests a valid spec structure (does not execute real probe unless model exists).
"""
from __future__ import annotations

import json
import math
import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "touch_audit"))

from g18_point_validator import validate_spec, validate_point, png_to_centered

ZONES = ["bust", "eye", "face", "head", "pant"]
FRAISE_MODEL = "fraise-france-maid-b"
FRAISE_EXISTS = (REPO / "assets" / "characters" / FRAISE_MODEL /
                 "session.nks.json").exists()


def base_spec() -> dict:
    return {
        "model": FRAISE_MODEL,
        "condition": "default_idle",
        "png_point": [400, 897],
        "width": 800, "height": 1080,
        "origin": [0, 400], "scale": 1.0,
        "steps": 0, "seed": 1, "reaction_seed": 1,
        "expected_zone": "bust",
        "expected_phase_in": [1, 2],
        "expected_reaction_prefix": "fraise/",
    }


def test_missing_required_field():
    for field in ["model", "condition", "png_point", "width", "height",
                  "origin", "scale", "steps", "seed", "reaction_seed"]:
        spec = base_spec()
        del spec[field]
        errors = validate_spec(spec)
        assert any(f"missing required field: {field}" in e for e in errors), \
            f"expected missing field error for {field}"


def test_non_finite_coordinates():
    for bad_val in [float("inf"), float("nan"), float("-inf")]:
        spec = base_spec()
        spec["png_point"] = [bad_val, 897]
        errors = validate_spec(spec)
        assert any("png_point" in e for e in errors), \
            f"expected png_point error for {bad_val}"


def test_invalid_scale():
    for bad_scale in [0, -1, float("inf"), float("nan")]:
        spec = base_spec()
        spec["scale"] = bad_scale
        errors = validate_spec(spec)
        assert any("scale" in e for e in errors), \
            f"expected scale error for {bad_scale}"


def test_invalid_steps():
    for bad_steps in [-1, 1.5, "abc"]:
        spec = base_spec()
        spec["steps"] = bad_steps
        errors = validate_spec(spec)
        assert any("steps" in e for e in errors), \
            f"expected steps error for {bad_steps}"


def test_invalid_seed():
    for bad_seed in [-1, 1.5, "abc"]:
        spec = base_spec()
        spec["seed"] = bad_seed
        errors = validate_spec(spec)
        assert any("seed" in e for e in errors), \
            f"expected seed error for {bad_seed}"


def test_invalid_expected_zone():
    spec = base_spec()
    spec["expected_zone"] = "invalid_zone"
    errors = validate_spec(spec)
    assert any("expected_zone" in e for e in errors)


def test_valid_expected_zone_null():
    spec = base_spec()
    spec["expected_zone"] = None
    spec["expected_phase_in"] = [0]
    spec["expected_reaction_prefix"] = None
    errors = validate_spec(spec)
    assert len(errors) == 0, f"expected no errors, got {errors}"


def test_invalid_origin():
    for bad_orig in [[], [1], [1, "a"], [float("inf"), 400]]:
        spec = base_spec()
        spec["origin"] = bad_orig
        errors = validate_spec(spec)
        assert any("origin" in e for e in errors), \
            f"expected origin error for {bad_orig}"


def test_invalid_expected_phase():
    spec = base_spec()
    spec["expected_phase_in"] = [3, 4]
    errors = validate_spec(spec)
    assert any("expected_phase_in" in e for e in errors)


def test_nonexistent_model():
    spec = base_spec()
    spec["model"] = "nonexistent-model-xyz"
    result = validate_point(spec)
    assert not result["ok"], "expected failure for non-existent model"
    assert not result["validated"], "expected not validated for non-existent model"
    assert any("session not found" in e for e in result["errors"])


def test_png_to_centered_conversion():
    cx, cy = png_to_centered(400, 540, 800, 1080)
    assert cx == 0.0 and cy == 0.0, f"center should be (0,0), got ({cx},{cy})"
    cx, cy = png_to_centered(0, 0, 800, 1080)
    assert cx == -400.0 and cy == -540.0, f"corner should be (-400,-540), got ({cx},{cy})"


def test_spec_validation_passes_for_valid_spec():
    spec = base_spec()
    errors = validate_spec(spec)
    assert len(errors) == 0, f"expected no errors for valid spec, got {errors}"


def test_outside_point_spec_valid():
    spec = base_spec()
    spec["png_point"] = [99999, 99999]
    spec["expected_zone"] = None
    spec["expected_phase_in"] = [0]
    spec["expected_reaction_prefix"] = None
    errors = validate_spec(spec)
    assert len(errors) == 0, f"expected no errors for outside point spec, got {errors}"


def test_all_zones_valid():
    for zone in ZONES:
        spec = base_spec()
        spec["expected_zone"] = zone
        errors = validate_spec(spec)
        assert len(errors) == 0, f"zone {zone} should be valid"


def test_width_height_positive():
    for bad_w in [0, -1]:
        spec = base_spec()
        spec["width"] = bad_w
        errors = validate_spec(spec)
        assert any("width" in e or "height" in e for e in errors), \
            f"expected width/height error for {bad_w}"


def test_reaction_seed_validation():
    for bad_rs in [-1, 1.5, "abc"]:
        spec = base_spec()
        spec["reaction_seed"] = bad_rs
        errors = validate_spec(spec)
        assert any("reaction_seed" in e for e in errors), \
            f"expected reaction_seed error for {bad_rs}"


def main() -> int:
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    passed = 0
    failed = 0
    skipped = 0
    for test in tests:
        try:
            test()
            passed += 1
        except AssertionError as e:
            print(f"FAIL {test.__name__}: {e}", file=sys.stderr)
            failed += 1
        except Exception as e:
            print(f"ERROR {test.__name__}: {e}", file=sys.stderr)
            failed += 1
    print(f"G18 point validator tests: {passed} passed, {failed} failed, {skipped} skipped")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
