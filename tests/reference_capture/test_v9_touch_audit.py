#!/usr/bin/env python3
"""Synthetic branch tests for V9 provenance and capture failure handling."""
import copy
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ledger_tool = load("v9_ledger", ROOT / "tools/touch_audit/generate_v9_ledger.py")
capture_tool = load("v9_capture", ROOT / "tools/touch_audit/v9_visual_capture.py")


class TestV9Evidence(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.region = {"center": [0, 38], "radius": [143, 110]}
        self.g12 = {"models": [{"model": "fixture", "status": "OK",
                                "author_regions": {"bust": self.region}},
                               {"model": "rejected", "status": "CPU_REJECTED"}]}
        self.evidence = []
        for role in sorted(ledger_tool.ROLES):
            path = self.root / f"{role}.{'png' if role in {'clean', 'overlay'} else 'json'}"
            if role in {"clean", "overlay"}:
                Image.new("RGBA", (4, 4), (20, 40, 60, 255)).save(path)
            else:
                path.write_text(json.dumps({"role": role}))
            self.evidence.append({"role": role, "path": path.name,
                                  "sha256": ledger_tool.file_hash(path)})
        default_evidence = [e for e in self.evidence if e["role"] in {"clean", "overlay", "probe"}]
        for e in default_evidence:
            e["condition"] = "default_idle"
        idle_evidence = []
        for role in ("clean", "overlay", "probe"):
            path = self.root / f"{role}_idle_120.{'png' if role in {'clean', 'overlay'} else 'json'}"
            if role in {"clean", "overlay"}:
                Image.new("RGBA", (4, 4), (40, 60, 80, 255)).save(path)
            else:
                path.write_text(json.dumps({"role": role, "pose": "idle_120"}))
            idle_evidence.append({"role": role, "path": path.name,
                                  "sha256": ledger_tool.file_hash(path), "condition": "idle_120"})
        self.idle_evidence = idle_evidence
        conditions = []
        for pose, evidence in (("default_idle", default_evidence), ("idle_120", idle_evidence)):
            outputs = {e["role"]: {"path": e["path"], "sha256": e["sha256"]} for e in evidence}
            manifest = self.root / f"{pose}_regions.result.json"
            manifest.write_text(json.dumps({"ok": True, "returncode": 0, "output": outputs["probe"]}))
            conditions.append({"name": pose, "pass": True, "errors": [], "evidence": manifest.name,
                               "capture": {"plain": {"ok": True, "returncode": 0, "image": outputs["clean"]},
                                           "overlay": {"ok": True, "returncode": 0, "image": outputs["overlay"]}}})
        self.g12["models"][0]["conditions"] = conditions
        self.g12["models"][0]["assets"] = [
            e for e in self.evidence if e["role"] in {"model_asset", "session_asset"}
        ]
        self.g12["models"][0]["sources"] = [
            e for e in self.evidence if e["role"] == "source"
        ]
        self.observation = {"model": "fixture", "status": "MATCH", "observer": "test reviewer",
                            "notes": "Explicit scoped comparison", "conditions": ["default_idle"],
                            "checked_zones": ["bust"], "evidence": self.evidence}

    def build(self, observations=None, candidates=None):
        return ledger_tool.build_ledger(self.g12, {"version": 3, "observations": observations or [],
                                                  "candidates": candidates or []}, self.root)

    def test_no_observation_preserves_denominator_without_pass(self):
        result = self.build()
        self.assertEqual(result["total_models"], 2)
        self.assertEqual(result["summary"]["UNREVIEWED"], 1)
        self.assertEqual(result["summary"]["BLOCKED_INPUT"], 1)
        self.assertEqual(result["summary"]["MATCH"], 0)
        self.assertEqual(result["candidates"], [])

    def test_explicit_observation_is_scoped(self):
        result = self.build([self.observation])
        self.assertEqual(result["models"][0]["visual_alignment"], "MATCH")
        self.assertEqual(result["models"][0]["checked_zones"], ["bust"])
        self.assertEqual(len(result["models"][0]["evidence"]), 7)
        self.assertEqual(result["version"], 3)

    def test_each_declared_pose_has_frozen_images_and_probe(self):
        record = copy.deepcopy(self.observation)
        record["conditions"].append("idle_120")
        record["evidence"].extend(self.idle_evidence)
        result = self.build([record])["models"][0]
        self.assertEqual(result["visual_alignment"], "MATCH")
        self.assertEqual(result["errors"], [])
        self.assertEqual(len(result["evidence"]), 10)

    def test_default_only_evidence_cannot_certify_another_pose(self):
        record = copy.deepcopy(self.observation)
        record["conditions"].append("idle_120")
        result = self.build([record])["models"][0]
        self.assertEqual(result["visual_alignment"], "PENDING_EVIDENCE")
        for role in ("clean", "overlay", "probe"):
            self.assertIn(f"condition requires exactly one {role} evidence: idle_120", result["errors"])

    def test_missing_wrong_or_unknown_condition_cannot_pass(self):
        for value in (None, "idle_120", "unknown"):
            record = copy.deepcopy(self.observation)
            next(e for e in record["evidence"] if e["role"] == "clean")["condition"] = value
            result = self.build([record])["models"][0]
            with self.subTest(condition=value):
                self.assertEqual(result["visual_alignment"], "PENDING_EVIDENCE")
                self.assertIn("evidence condition missing/undeclared: clean", result["errors"])

    def test_unknown_declared_condition_cannot_pass(self):
        record = copy.deepcopy(self.observation)
        record["conditions"] = ["unknown"]
        result = self.build([record])["models"][0]
        self.assertEqual(result["visual_alignment"], "PENDING_EVIDENCE")
        self.assertIn("unknown frozen G13 condition: unknown", result["errors"])

    def test_valid_hash_for_different_pose_cannot_pass(self):
        record = copy.deepcopy(self.observation)
        for role in ("clean", "overlay", "probe"):
            item = next(e for e in record["evidence"] if e["role"] == role)
            other = next(e for e in self.idle_evidence if e["role"] == role)
            item.update(path=other["path"], sha256=other["sha256"])
        result = self.build([record])["models"][0]
        self.assertEqual(result["visual_alignment"], "PENDING_EVIDENCE")
        for role in ("clean", "overlay", "probe"):
            self.assertIn(f"evidence differs from frozen G13 condition output: default_idle: {role}", result["errors"])

    def test_duplicate_condition_evidence_cannot_pass(self):
        record = copy.deepcopy(self.observation)
        record["evidence"].append(copy.deepcopy(next(e for e in self.evidence if e["role"] == "clean")))
        result = self.build([record])["models"][0]
        self.assertEqual(result["visual_alignment"], "PENDING_EVIDENCE")
        self.assertIn("condition requires exactly one clean evidence: default_idle", result["errors"])

    def test_unconditioned_legacy_evidence_cannot_pass(self):
        record = copy.deepcopy(self.observation)
        for item in record["evidence"]:
            item.pop("condition", None)
        result = self.build([record])["models"][0]
        self.assertEqual(result["visual_alignment"], "PENDING_EVIDENCE")
        self.assertIn("condition requires exactly one probe evidence: default_idle", result["errors"])

    def test_version_two_or_missing_version_cannot_pass(self):
        for version in (2, None):
            records = {"observations": [self.observation]}
            if version is not None:
                records["version"] = version
            result = ledger_tool.build_ledger(self.g12, records, self.root)["models"][0]
            with self.subTest(version=version):
                self.assertEqual(result["visual_alignment"], "PENDING_EVIDENCE")
                self.assertIn("observation input version unsupported; rebuild version 3 evidence", result["errors"])

    def test_frozen_probe_failure_or_missing_manifest_cannot_pass(self):
        manifest = self.root / "default_idle_regions.result.json"
        for data in ("not JSON", '{"ok": false, "returncode": 1}', '{"ok": true, "returncode": 0}'):
            manifest.write_text(data)
            result = self.build([self.observation])["models"][0]
            with self.subTest(data=data):
                self.assertEqual(result["visual_alignment"], "PENDING_EVIDENCE")
                self.assertIn("frozen G13 probe output unavailable: default_idle", result["errors"])
        manifest.unlink()
        result = self.build([self.observation])["models"][0]
        self.assertEqual(result["visual_alignment"], "PENDING_EVIDENCE")
        self.assertIn("frozen G13 probe output unavailable: default_idle", result["errors"])

    def test_frozen_condition_failure_cannot_pass(self):
        self.g12["models"][0]["conditions"][0]["pass"] = False
        result = self.build([self.observation])["models"][0]
        self.assertEqual(result["visual_alignment"], "PENDING_EVIDENCE")
        self.assertIn("frozen G13 condition unsuccessful: default_idle", result["errors"])

    def test_hash_mismatch_cannot_pass(self):
        record = copy.deepcopy(self.observation)
        record["evidence"][0]["sha256"] = "0" * 64
        result = self.build([record])["models"][0]
        self.assertEqual(result["visual_alignment"], "PENDING_EVIDENCE")
        self.assertTrue(any("hash mismatch" in error for error in result["errors"]))

    def test_valid_hash_from_wrong_model_cannot_pass(self):
        record = copy.deepcopy(self.observation)
        wrong = self.root / "other-model.json"
        wrong.write_text('{"other-model": true}')
        item = next(e for e in record["evidence"] if e["role"] == "model_asset")
        item.update(path=wrong.name, sha256=ledger_tool.file_hash(wrong))
        result = self.build([record])["models"][0]
        self.assertEqual(result["visual_alignment"], "PENDING_EVIDENCE")
        self.assertTrue(any("frozen G12 model input" in error for error in result["errors"]))

    def test_legacy_g12_without_hashes_cannot_pass(self):
        del self.g12["models"][0]["assets"]
        result = self.build([self.observation])["models"][0]
        self.assertEqual(result["visual_alignment"], "PENDING_EVIDENCE")

    def test_missing_reference_cannot_pass(self):
        record = copy.deepcopy(self.observation)
        record["evidence"] = [e for e in record["evidence"] if e["role"] != "reference"]
        self.assertEqual(self.build([record])["models"][0]["visual_alignment"], "PENDING_EVIDENCE")

    def test_missing_file_cannot_pass(self):
        (self.root / "probe.json").unlink()
        self.assertEqual(self.build([self.observation])["models"][0]["visual_alignment"], "PENDING_EVIDENCE")

    def test_missing_observer_or_scope_cannot_pass(self):
        for field in ("observer", "notes", "conditions", "checked_zones"):
            record = copy.deepcopy(self.observation)
            del record[field]
            with self.subTest(field=field):
                self.assertEqual(self.build([record])["models"][0]["visual_alignment"], "PENDING_EVIDENCE")

    def test_duplicate_unknown_models_rejected(self):
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.build([self.observation, self.observation])
        bad = {**self.observation, "model": "unknown"}
        with self.assertRaisesRegex(ValueError, "not in coverage"):
            self.build([bad])

    def test_duplicate_coverage_rejected(self):
        self.g12["models"].append(self.g12["models"][0])
        with self.assertRaisesRegex(ValueError, "duplicated"):
            self.build()

    def test_malformed_inputs_rejected(self):
        for g12, observations in (([], {}), ({}, []), ({"models": [None]}, {})):
            with self.subTest(g12=g12, observations=observations):
                with self.assertRaises(ValueError):
                    ledger_tool.build_ledger(g12, observations, self.root)

    def candidate(self):
        return {"model": "fixture", "zone": "bust", "current_author_region": self.region,
                "suggested_author_region": {"center": [-25, 38], "radius": [180, 115]},
                "rationale": "Measured edge misses", "conditions": ["default_idle", "idle_120"],
                "measured_points": [{"condition": pose, "author_point": [100, 38],
                                     "output_point": [100, 438], "expected": "hit"}
                                    for pose in ("default_idle", "idle_120")],
                "evidence": self.evidence + self.idle_evidence}

    def test_candidate_syntax_cannot_certify_unexecuted_points(self):
        result = self.build(candidates=[self.candidate()])["candidates"][0]
        self.assertEqual(result["status"], "PENDING_EVIDENCE")
        self.assertEqual(result["errors"], ["candidate point request/response validator unavailable; actual per-point execution is unverified"])

    def test_candidate_bad_current_values_are_pending(self):
        candidate = self.candidate()
        candidate["current_author_region"] = {"center": [0, -10], "radius": [143, 110]}
        result = self.build(candidates=[candidate])["candidates"][0]
        self.assertEqual(result["status"], "PENDING_EVIDENCE")
        self.assertTrue(any("frozen G12" in e for e in result["errors"]))

    def test_candidate_missing_hash_pose_points_and_nonfinite_are_pending(self):
        changes = [({"evidence": []}, "missing evidence role"),
                   ({"conditions": ["default_idle"]}, "candidate requires default and another observed pose"),
                   ({"measured_points": []}, "candidate measured points missing"),
                   ({"suggested_author_region": {"center": [float("nan"), 0], "radius": [1, 1]}}, "candidate region invalid"),
                   ({"suggested_author_region": {"center": [0, 0], "radius": [0, 1]}}, "candidate region invalid")]
        for change, expected_error in changes:
            with self.subTest(change=change):
                result = self.build(candidates=[{**self.candidate(), **change}])["candidates"][0]
                self.assertEqual(result["status"], "PENDING_EVIDENCE")
                self.assertTrue(any(expected_error in error for error in result["errors"]))

    def test_cli_existing_output_refused_before_mutation(self):
        source = self.root / "g12.json"
        source.write_text(json.dumps(self.g12))
        destination = self.root / "output"
        destination.mkdir()
        marker = destination / "preserved"
        marker.write_text("unchanged")
        result = subprocess.run([sys.executable, str(ROOT / "tools/touch_audit/generate_v9_ledger.py"),
                                 "--g12-ledger", str(source), "--output", str(destination)],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("output already exists", result.stderr)
        self.assertEqual(marker.read_text(), "unchanged")


class TestV9Capture(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.assets = self.root / "assets"
        self.model = self.assets / "characters/fixture"
        self.model.mkdir(parents=True)
        for name in ("model.nkm.json", "session.nks.json"):
            (self.model / name).write_text("{}")
        self.output = self.root / "output"
        for directory in ("captures", "logs", "crops"):
            (self.output / directory).mkdir(parents=True)

    def capture(self, runner):
        return capture_tool.capture_model("fixture", self.assets, self.root / "player",
                                          self.output, self.root / "lock", 1, runner)

    def test_missing_assets_never_execute(self):
        (self.model / "session.nks.json").unlink()
        runner = Mock()
        self.assertEqual(self.capture(runner)["status"], "MISSING_ASSET")
        runner.assert_not_called()

    def test_missing_texture_never_execute(self):
        (self.model / "model.nkm.json").write_text(json.dumps(
            {"model": {"textures": [{"path": "textures/missing.png"}]}}
        ))
        runner = Mock()
        self.assertEqual(self.capture(runner)["status"], "MISSING_ASSET")
        runner.assert_not_called()

    def test_failure_does_not_reuse_stale_overlay(self):
        Image.new("RGBA", (800, 1080)).save(self.output / "captures/fixture_overlay.png")
        runner = Mock(return_value=subprocess.CompletedProcess([], 1, "", "busy"))
        result = self.capture(runner)
        self.assertEqual(result["status"], "FAILED")
        self.assertNotIn("crops", result)
        self.assertTrue(all(c["sha256"] is None for c in result["captures"]))

    def test_success_exit_without_png_is_failure(self):
        runner = Mock(return_value=subprocess.CompletedProcess([], 0, "", ""))
        self.assertEqual(self.capture(runner)["status"], "FAILED")

    def test_invalid_png_is_failure(self):
        def runner(command, **kwargs):
            Path(command[-1]).write_text("invalid PNG")
            return subprocess.CompletedProcess(command, 0, "", "")
        self.assertEqual(self.capture(runner)["status"], "FAILED")

    def test_wrong_dimensions_are_failure(self):
        def runner(command, **kwargs):
            Image.new("RGBA", (4, 4)).save(command[-1])
            return subprocess.CompletedProcess(command, 0, "", "")
        self.assertEqual(self.capture(runner)["status"], "FAILED")

    def test_capture_cli_rejects_existing_output(self):
        player = self.root / "player"
        player.write_text("fixture")
        marker = self.output / "preserved"
        marker.write_text("unchanged")
        result = subprocess.run([
            sys.executable, str(ROOT / "tools/touch_audit/v9_visual_capture.py"),
            "--assets", str(self.assets), "--player", str(player), "--output", str(self.output)
        ], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("output already exists", result.stderr)
        self.assertEqual(marker.read_text(), "unchanged")

    def test_capture_cli_rejects_path_traversal_before_output(self):
        player = self.root / "player"
        player.write_text("fixture")
        output = self.root / "new-output"
        result = subprocess.run([
            sys.executable, str(ROOT / "tools/touch_audit/v9_visual_capture.py"),
            "--assets", str(self.assets), "--player", str(player), "--output", str(output),
            "--models", "../escape"
        ], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("model names invalid", result.stderr)
        self.assertFalse(output.exists())

    def test_timeout_saved_not_reported_success(self):
        runner = Mock(side_effect=subprocess.TimeoutExpired("fixture", 1))
        result = self.capture(runner)
        self.assertEqual(result["status"], "FAILED")
        self.assertTrue(all(c["log_sha256"] for c in result["captures"]))

    def test_capture_success_has_provenance_without_visual_pass(self):
        def runner(command, **kwargs):
            Image.new("RGBA", (800, 1080)).save(command[-1])
            return subprocess.CompletedProcess(command, 0, "out", "")
        result = self.capture(runner)
        self.assertEqual(result["status"], "CAPTURED")
        self.assertEqual(result["visual_alignment"], "UNREVIEWED")
        self.assertEqual(len(result["crops"]), 2)
        for capture in result["captures"]:
            self.assertEqual(capture["command"][:2], ["flock", str(self.root / "lock")])
            self.assertIn("--no-save", capture["command"])
            self.assertEqual(capture["sha256"], capture_tool.file_hash(Path(capture["path"])))

    def test_partial_failure_is_failure(self):
        def runner(command, **kwargs):
            if "--show-touch-regions" in command:
                return subprocess.CompletedProcess(command, 1, "", "failure")
            Image.new("RGBA", (800, 1080)).save(command[-1])
            return subprocess.CompletedProcess(command, 0, "", "")
        self.assertEqual(self.capture(runner)["status"], "PARTIAL")

    def test_asset_mutation_cannot_pass(self):
        def runner(command, **kwargs):
            Image.new("RGBA", (800, 1080)).save(command[-1])
            (self.model / "session.nks.json").write_text('{"changed": true}')
            return subprocess.CompletedProcess(command, 0, "", "")
        result = self.capture(runner)
        self.assertEqual(result["status"], "FAILED")
        self.assertIn("asset changed during capture", result["errors"])


if __name__ == "__main__":
    unittest.main()
