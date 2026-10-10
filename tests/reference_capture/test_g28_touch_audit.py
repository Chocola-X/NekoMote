"""G28 synthetic negative cases: step diagnostics, capture validation, freeze checks, V9 merge.

All inputs are synthetic; no real ledger or probe output is reused.
"""
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def _load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools/touch_audit" / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


qt_captures = _load("generate_qt_condition_captures")
freeze = _load("freeze_g24_evidence")
v9_ledger = _load("generate_v9_ledger")


class ParseCaptureDiagnostics(unittest.TestCase):
    def test_valid_line_parsed(self):
        stdout = "KPackage config loaded\ncapture: fixed steps=120 executed=120 seed=1 rendered_frames=3\ncapture: screenshot saved\n"
        parsed, errors = qt_captures.parse_capture_diagnostics(stdout)
        self.assertEqual(errors, [])
        self.assertEqual(parsed["steps"], 120)
        self.assertEqual(parsed["executed"], 120)
        self.assertEqual(parsed["seed"], 1)
        self.assertEqual(parsed["rendered_frames"], 3)

    def test_zero_steps_parsed(self):
        stdout = "capture: fixed steps=0 executed=0 seed=1 rendered_frames=3\n"
        parsed, errors = qt_captures.parse_capture_diagnostics(stdout)
        self.assertEqual(errors, [])
        self.assertEqual(parsed["executed"], 0)

    def test_missing_line_rejected(self):
        parsed, errors = qt_captures.parse_capture_diagnostics("no capture line here")
        self.assertIsNone(parsed)
        self.assertTrue(errors)

    def test_empty_stdout_rejected(self):
        parsed, errors = qt_captures.parse_capture_diagnostics("")
        self.assertIsNone(parsed)
        self.assertTrue(errors)


class ValidateCapture(unittest.TestCase):
    def test_executed_mismatch_rejected(self):
        parsed = {"steps": 120, "executed": 60, "seed": 1, "rendered_frames": 3}
        errors = qt_captures.validate_capture(parsed, 120, 1)
        self.assertTrue(any("executed mismatch" in e for e in errors))

    def test_steps_mismatch_rejected(self):
        parsed = {"steps": 60, "executed": 120, "seed": 1, "rendered_frames": 3}
        errors = qt_captures.validate_capture(parsed, 120, 1)
        self.assertTrue(any("steps mismatch" in e for e in errors))

    def test_seed_mismatch_rejected(self):
        parsed = {"steps": 120, "executed": 120, "seed": 2, "rendered_frames": 3}
        errors = qt_captures.validate_capture(parsed, 120, 1)
        self.assertTrue(any("seed mismatch" in e for e in errors))

    def test_zero_rendered_frames_rejected(self):
        parsed = {"steps": 0, "executed": 0, "seed": 1, "rendered_frames": 0}
        errors = qt_captures.validate_capture(parsed, 0, 1)
        self.assertTrue(any("rendered_frames" in e for e in errors))

    def test_valid_capture_passes(self):
        parsed = {"steps": 120, "executed": 120, "seed": 1, "rendered_frames": 3}
        errors = qt_captures.validate_capture(parsed, 120, 1)
        self.assertEqual(errors, [])

    def test_none_parsed_rejected(self):
        errors = qt_captures.validate_capture(None, 0, 1)
        self.assertTrue(errors)


class CheckStepDiagnostics(unittest.TestCase):
    def test_stale_log_without_diagnostics_rejected(self):
        cap = {"params": {"steps": 120}, "diagnostics": None}
        errors = freeze.check_step_diagnostics(cap, "idle120")
        self.assertTrue(any("stale" in e for e in errors))

    def test_executed_mismatch_rejected(self):
        cap = {"params": {"steps": 120}, "diagnostics": {"executed": 60, "steps": 120, "seed": 1, "rendered_frames": 3}}
        errors = freeze.check_step_diagnostics(cap, "idle120")
        self.assertTrue(any("executed" in e for e in errors))

    def test_valid_diagnostics_pass(self):
        cap = {"params": {"steps": 120}, "diagnostics": {"executed": 120, "steps": 120, "seed": 1, "rendered_frames": 3}}
        errors = freeze.check_step_diagnostics(cap, "idle120")
        self.assertEqual(errors, [])


class CheckCaptureOk(unittest.TestCase):
    def test_failed_capture_rejected(self):
        cap = {"ok": False, "returncode": 1, "validation_errors": []}
        errors = freeze.check_capture_ok(cap, "default")
        self.assertTrue(any("ok=False" in e for e in errors))

    def test_nonzero_returncode_rejected(self):
        cap = {"ok": True, "returncode": 1, "validation_errors": []}
        errors = freeze.check_capture_ok(cap, "default")
        self.assertTrue(any("returncode" in e for e in errors))

    def test_validation_errors_rejected(self):
        cap = {"ok": True, "returncode": 0, "validation_errors": ["executed mismatch"]}
        errors = freeze.check_capture_ok(cap, "default")
        self.assertTrue(any("executed mismatch" in e for e in errors))

    def test_valid_capture_passes(self):
        cap = {"ok": True, "returncode": 0, "validation_errors": []}
        errors = freeze.check_capture_ok(cap, "default")
        self.assertEqual(errors, [])


class CheckPngValid(unittest.TestCase):
    def test_wrong_size_rejected(self):
        cap = {"image_info": {"size": [400, 540], "alpha_bbox": [0, 0, 400, 540]}}
        errors = freeze.check_png_valid(cap, "default")
        self.assertTrue(any("size" in e for e in errors))

    def test_empty_alpha_rejected(self):
        cap = {"image_info": {"size": [800, 1080], "alpha_bbox": None}}
        errors = freeze.check_png_valid(cap, "default")
        self.assertTrue(any("alpha" in e for e in errors))

    def test_valid_png_passes(self):
        cap = {"image_info": {"size": [800, 1080], "alpha_bbox": [10, 20, 790, 1060]}}
        errors = freeze.check_png_valid(cap, "default")
        self.assertEqual(errors, [])


class CheckLowerBodyInFrame(unittest.TestCase):
    def _source_manifest(self, pant_center):
        return {"models": {"test-model": {"conditions": {
            "lower_body_in_frame": {"projected_regions": {
                "pant": {"center": pant_center, "radius": [100, 100]}}}}}}}

    def test_pant_outside_bbox_rejected(self):
        cap = {"image_info": {"alpha_bbox": [0, 0, 400, 540]}}
        sm = self._source_manifest([0, 400])
        errors = freeze.check_lower_body_in_frame(cap, sm, "test-model")
        self.assertTrue(errors)

    def test_pant_inside_bbox_passes(self):
        cap = {"image_info": {"alpha_bbox": [0, 0, 800, 1080]}}
        sm = self._source_manifest([0, 0])
        errors = freeze.check_lower_body_in_frame(cap, sm, "test-model")
        self.assertEqual(errors, [])

    def test_no_pant_region_passes(self):
        cap = {"image_info": {"alpha_bbox": [0, 0, 100, 100]}}
        sm = {"models": {"test-model": {"conditions": {
            "lower_body_in_frame": {"projected_regions": {"pant": None}}}}}}
        errors = freeze.check_lower_body_in_frame(cap, sm, "test-model")
        self.assertEqual(errors, [])


class MergeG28Conditions(unittest.TestCase):
    def test_new_condition_merged(self):
        g12 = {"models": [{"model": "m1", "status": "OK", "conditions": [
            {"name": "default_idle", "pass": True, "errors": [], "capture": {}, "evidence": None}]}]}
        g28 = {"models": {"m1": {"captures": {
            "lower_body_in_frame": {"png": {"path": "/x.png", "sha256": "abc", "bytes": 1}}}}}}
        result = v9_ledger.merge_g28_conditions(g12, g28)
        names = [c["name"] for c in result["models"][0]["conditions"]]
        self.assertIn("lower_body_in_frame", names)
        self.assertIn("default_idle", names)

    def test_existing_condition_not_duplicated(self):
        g12 = {"models": [{"model": "m1", "status": "OK", "conditions": [
            {"name": "default_idle", "pass": True, "errors": [], "capture": {}, "evidence": None}]}]}
        g28 = {"models": {"m1": {"captures": {
            "default_idle": {"png": {"path": "/x.png", "sha256": "abc", "bytes": 1}}}}}}
        result = v9_ledger.merge_g28_conditions(g12, g28)
        names = [c["name"] for c in result["models"][0]["conditions"]]
        self.assertEqual(names.count("default_idle"), 1)

    def test_overlay_merged_into_capture(self):
        g12 = {"models": [{"model": "m1", "status": "OK", "conditions": []}]}
        g28 = {"models": {"m1": {
            "captures": {"lower_body_in_frame": {"png": {"path": "/c.png", "sha256": "cc", "bytes": 1}}},
            "overlays": {"lower_body_in_frame": {"overlay_png": {"path": "/o.png", "sha256": "oo", "bytes": 1}}}}}}
        result = v9_ledger.merge_g28_conditions(g12, g28)
        cond = next(c for c in result["models"][0]["conditions"] if c["name"] == "lower_body_in_frame")
        self.assertIn("plain", cond["capture"])
        self.assertIn("overlay", cond["capture"])

    def test_no_g28_models_no_change(self):
        g12 = {"models": [{"model": "m1", "status": "OK", "conditions": [
            {"name": "default_idle", "pass": True, "errors": [], "capture": {}, "evidence": None}]}]}
        result = v9_ledger.merge_g28_conditions(g12, {"models": {}})
        self.assertEqual(len(result["models"][0]["conditions"]), 1)


class V9LedgerG28Conditions(unittest.TestCase):
    """Verify generate_v9_ledger accepts G28-merged conditions without weakening validation."""

    def _make_g12(self):
        return {"models": [{"model": "m1", "status": "OK",
                            "conditions": [{"name": "default_idle", "pass": True, "errors": [],
                                            "capture": {"plain": {"ok": True, "returncode": 0, "image": {"path": "c.png", "sha256": "c", "bytes": 1}},
                                                        "overlay": {"ok": True, "returncode": 0, "image": {"path": "o.png", "sha256": "o", "bytes": 1}}},
                                            "evidence": "probe.json"}],
                            "assets": [{"role": "model_asset", "path": "m.json", "sha256": "m", "bytes": 1},
                                       {"role": "session_asset", "path": "s.json", "sha256": "s", "bytes": 1}],
                            "sources": [{"role": "source", "path": "src.json", "sha256": "src", "bytes": 1}]}]}

    def test_merged_condition_still_requires_evidence(self):
        g12 = self._make_g12()
        g28 = {"models": {"m1": {"captures": {
            "lower_body_in_frame": {"png": {"path": "c2.png", "sha256": "c2", "bytes": 1}}},
            "overlays": {"lower_body_in_frame": {"overlay_png": {"path": "o2.png", "sha256": "o2", "bytes": 1}}}}}}
        merged = v9_ledger.merge_g28_conditions(g12, g28)
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            for f in ("c.png", "o.png", "c2.png", "o2.png", "m.json", "s.json", "src.json", "probe.json"):
                (base / f).write_text("x")
            probe_result = {"ok": True, "returncode": 0, "output": {"path": "probe.json", "sha256": v9_ledger.file_hash(base / "probe.json")}}
            (base / "probe_result.json").write_text(json.dumps(probe_result))
            merged["models"][0]["conditions"][0]["evidence"] = "probe_result.json"
            records = {"version": 3, "observations": [{
                "model": "m1", "status": "MATCH", "observer": "test", "notes": "n",
                "conditions": ["default_idle", "lower_body_in_frame"],
                "checked_zones": ["bust"],
                "evidence": [
                    {"role": "model_asset", "path": "m.json", "sha256": v9_ledger.file_hash(base / "m.json")},
                    {"role": "session_asset", "path": "s.json", "sha256": v9_ledger.file_hash(base / "s.json")},
                    {"role": "source", "path": "src.json", "sha256": v9_ledger.file_hash(base / "src.json")},
                    {"role": "clean", "path": "c.png", "sha256": v9_ledger.file_hash(base / "c.png"), "condition": "default_idle"},
                    {"role": "overlay", "path": "o.png", "sha256": v9_ledger.file_hash(base / "o.png"), "condition": "default_idle"},
                    {"role": "probe", "path": "probe.json", "sha256": v9_ledger.file_hash(base / "probe.json"), "condition": "default_idle"},
                    {"role": "reference", "path": "src.json", "sha256": v9_ledger.file_hash(base / "src.json")},
                ]}], "candidates": []}
            ledger = v9_ledger.build_ledger(merged, records, base)
            row = ledger["models"][0]
            self.assertNotEqual(row["visual_alignment"], "MATCH",
                                "merged lower_body_in_frame condition lacks evidence; must not pass")


class InconsistentPngNegative(unittest.TestCase):
    """Clean and overlay PNG hash mismatch must be detectable."""

    def test_different_hash_detected(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            (base / "clean.png").write_text("clean")
            (base / "overlay.png").write_text("overlay")
            clean_hash = v9_ledger.file_hash(base / "clean.png")
            overlay_hash = v9_ledger.file_hash(base / "overlay.png")
            self.assertNotEqual(clean_hash, overlay_hash)

    def test_same_hash_consistent(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            (base / "clean.png").write_text("same")
            (base / "overlay.png").write_text("same")
            clean_hash = v9_ledger.file_hash(base / "clean.png")
            overlay_hash = v9_ledger.file_hash(base / "overlay.png")
            self.assertEqual(clean_hash, overlay_hash)


if __name__ == "__main__":
    unittest.main()
