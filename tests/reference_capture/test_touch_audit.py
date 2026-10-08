"""Synthetic input/error/provenance contracts; never reuse a real ledger as a fixture."""
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/touch_audit/run_touch_audit.py"
spec = importlib.util.spec_from_file_location("touch_audit", TOOL)
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def regions():
    return {z: {"center": [i * 100.0, 0.0], "radius": [10.0, 10.0]}
            for i, z in enumerate(tool.ZONE_ORDER)}


def probe_data(steps=0, projected=None):
    return {"presentation_mirror": False, "touch_regions": projected or regions(),
            "samples": [{"step": i, "phase": 0, "reaction": ""} for i in range(steps + 1)]}


class Contracts(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.assets = self.root / "assets"
        self.output = self.root / "out"
        self.output.mkdir()
        self.model_dir = self.assets / "characters/demo"
        self.model_dir.mkdir(parents=True)
        self.author = regions()
        self.reactions = {z: [z + "_reaction"] for z in tool.ZONE_ORDER}
        self.definition = {"interaction": {"regions": self.author}, "reactions": [
            {"label": label, "zones": [z]} for z, labels in self.reactions.items() for label in labels]}
        self.model = {"model": {"mirror": False, "author_scale": {"state": 2, "value": 1.0}, "textures": []}}
        tool.write_json(self.model_dir / "model.nkm.json", self.model)
        tool.write_json(self.model_dir / "session.nks.json", {"session": self.definition})
        self.reference = self.root / "reference"
        (self.reference / "config").mkdir(parents=True)
        tool.write_json(self.reference / "config/model-catalog.json", {"demo": {"config": "demo.js"}, "missing": {"config": "demo.js"}})

    def audit(self, capture=False):
        return tool.audit_model("probe", "player", self.assets, "demo", self.output, capture,
                                lock_path=self.root / "gpu.lock")

    def fake_probe(self, probe, model, session, request, output):
        cond = {"origin": request["origin"], "scale": request["scale"]}
        rs = tool.projected_regions(self.author, self.reactions, False, 1, cond)
        data = probe_data(request["steps"], rs)
        if request.get("events"):
            point = request["events"][0]["point"]
            zone = tool.expected_zone(point, rs)
            if zone:
                data["samples"][-1].update(reaction=self.reactions[zone][0], phase=1)
        return {"ok": True, "data": data}

    def check_touch(self, result, point=None):
        with patch.object(tool, "run_probe", return_value=result):
            return tool.touch_check("probe", Path("model"), Path("session"), tool.CONDITIONS[0],
                                    point or [0, 0], regions(), self.reactions, self.output, "test")

    def test_expected_priority_uses_geometry_not_label(self):
        r = regions()
        r["face"] = r["head"] = {"center": [200, 0], "radius": [10, 10]}
        self.assertEqual(tool.expected_zone([200, 0], r), "face")
        self.assertEqual(tool.expected_zone([300, 0], regions()), "head")

    def test_inside_point_is_strictly_inside(self):
        p = tool.inside_point([10, 20], [5, 9])
        self.assertLess(sum(((p[i] - [10, 20][i]) / [5, 9][i]) ** 2 for i in (0, 1)), 1)

    def test_projection_uses_declared_author_scale(self):
        r = tool.projected_regions(self.author, self.reactions, True, 2, tool.CONDITIONS[2])
        self.assertEqual(r["eye"], {"center": [-120, 400], "radius": [20, 20]})

    def test_projection_suppresses_zones_without_reactions(self):
        self.reactions["eye"] = []
        self.assertIsNone(tool.projected_regions(self.author, self.reactions, False, 1, tool.CONDITIONS[0])["eye"])

    def test_wrong_zone_reaction_fails(self):
        data = probe_data()
        data["samples"][0].update(reaction="pant_reaction", phase=1)
        self.assertFalse(self.check_touch({"ok": True, "data": data})["pass"])

    def test_correct_zone_reaction_passes(self):
        data = probe_data()
        data["samples"][0].update(reaction="bust_reaction", phase=1)
        self.assertTrue(self.check_touch({"ok": True, "data": data})["pass"])

    def test_outside_process_failure_is_not_miss(self):
        result = self.check_touch({"ok": False}, tool.OUTSIDE_POINT)
        self.assertFalse(result["pass"])
        self.assertIsNone(result["reaction"])

    def test_outside_idle_empty_is_miss(self):
        self.assertTrue(self.check_touch({"ok": True, "data": probe_data()}, tool.OUTSIDE_POINT)["pass"])

    def test_outside_busy_empty_fails(self):
        data = probe_data()
        data["samples"][0]["phase"] = 1
        self.assertFalse(self.check_touch({"ok": True, "data": data}, tool.OUTSIDE_POINT)["pass"])

    def test_idle_event_after_exact_120_updates(self):
        with patch.object(tool, "run_probe", return_value={"ok": False}) as probe:
            tool.touch_check("probe", Path("model"), Path("session"), tool.CONDITIONS[1],
                             [0, 0], regions(), self.reactions, self.output, "test")
        request = probe.call_args.args[3]
        self.assertEqual(request["steps"], 121)
        self.assertEqual(request["events"][0]["step"], 121)

    def test_missing_model(self):
        (self.model_dir / "model.nkm.json").unlink()
        self.assertEqual(self.audit()["status"], "MISSING_MODEL")

    def test_missing_session(self):
        (self.model_dir / "session.nks.json").unlink()
        self.assertEqual(self.audit()["status"], "MISSING_SESSION")

    def test_invalid_session_json(self):
        (self.model_dir / "session.nks.json").write_text("broken")
        self.assertEqual(self.audit()["status"], "INVALID_ASSET")

    def test_missing_texture(self):
        self.model["model"]["textures"] = [{"path": "absent.png"}]
        tool.write_json(self.model_dir / "model.nkm.json", self.model)
        self.assertEqual(self.audit()["status"], "INVALID_ASSET")

    def test_escaped_texture_path_rejected(self):
        self.model["model"]["textures"] = [{"path": "../../../secret"}]
        tool.write_json(self.model_dir / "model.nkm.json", self.model)
        self.assertEqual(self.audit()["status"], "INVALID_ASSET")

    def test_region_process_failure_is_fail(self):
        failure = self.output / "stderr.log"
        failure.write_text("launch failed")
        with patch.object(tool, "run_probe", return_value={"ok": False, "stderr": {"path": str(failure)}}):
            self.assertEqual(self.audit()["status"], "FAIL")

    def test_only_diagnosed_geometry_rejection_is_cpu_rejected(self):
        failure = self.output / "stderr.log"
        failure.write_text("nekomote_session_probe: session=6 playback=3 geometry=5")
        with patch.object(tool, "run_probe", return_value={"ok": False, "stderr": {"path": str(failure)}}):
            self.assertEqual(self.audit()["status"], "CPU_REJECTED")

    def test_projection_mismatch_fails(self):
        data = probe_data()
        with patch.object(tool, "run_probe", return_value={"ok": True, "data": data}):
            row = self.audit()
        self.assertEqual(row["status"], "FAIL")
        self.assertIn("projection mismatch", row["errors"][0])

    def test_complete_synthetic_audit_passes(self):
        with patch.object(tool, "run_probe", side_effect=self.fake_probe):
            row = self.audit()
        self.assertEqual(row["status"], "OK")
        self.assertEqual(len(row["conditions"]), 4)
        self.assertEqual({p["role"] for p in row["assets"]}, {"model_asset", "session_asset"})

    def test_capture_failure_is_not_ok(self):
        with patch.object(tool, "run_probe", side_effect=self.fake_probe), patch.object(
                tool, "capture_pair", return_value={"plain": {"ok": True}, "overlay": {"ok": False}}):
            self.assertEqual(self.audit(True)["status"], "FAIL")

    def test_outside_reaction_failure_is_not_ok(self):
        def outside_fails(*args):
            result = self.fake_probe(*args)
            if args[3].get("events", [{}])[0].get("point") == tool.OUTSIDE_POINT:
                result["data"]["samples"][-1].update(reaction="bust_reaction", phase=1)
            return result
        with patch.object(tool, "run_probe", side_effect=outside_fails):
            self.assertEqual(self.audit()["status"], "FAIL")

    def test_no_active_zones_explicitly_measured(self):
        self.reactions = {z: [] for z in tool.ZONE_ORDER}
        self.definition["reactions"] = []
        tool.write_json(self.model_dir / "session.nks.json", {"session": self.definition})
        with patch.object(tool, "run_probe", side_effect=self.fake_probe):
            row = self.audit()
        self.assertEqual(row["status"], "NO_ACTIVE_ZONES")
        self.assertTrue(all(c["outside_test"]["pass"] for c in row["conditions"]))

    def test_invalid_samples_reject_truncation(self):
        with self.assertRaisesRegex(ValueError, "incomplete sample"):
            tool.validate_probe(probe_data(), 1)

    def test_invalid_sample_step_rejected(self):
        data = probe_data()
        data["samples"][0]["step"] = 4
        with self.assertRaisesRegex(ValueError, "invalid sample"):
            tool.validate_probe(data, 0)

    def test_missing_zone_rejected(self):
        data = probe_data()
        del data["touch_regions"]["eye"]
        with self.assertRaisesRegex(ValueError, "incomplete projected"):
            tool.validate_probe(data, 0)

    def test_nonfinite_region_rejected(self):
        data = probe_data()
        data["touch_regions"]["eye"]["center"][0] = float("nan")
        with self.assertRaisesRegex(ValueError, "invalid projected"):
            tool.validate_probe(data, 0)

    def test_zero_radius_rejected(self):
        data = probe_data()
        data["touch_regions"]["eye"]["radius"][0] = 0
        with self.assertRaisesRegex(ValueError, "invalid projected"):
            tool.validate_probe(data, 0)

    def test_run_process_retains_timeout_and_logs(self):
        with patch.object(tool.subprocess, "run", side_effect=subprocess.TimeoutExpired(["fake"], 1, stderr=b"partial")):
            result = tool.run_process(["fake"], self.output / "process", 1)
        self.assertFalse(result["ok"])
        self.assertTrue(result["timeout"])
        self.assertEqual(Path(result["stderr"]["path"]).read_bytes(), b"partial")
        self.assertTrue((self.output / "process.command.json").exists())

    def test_run_process_retains_nonzero_returncode(self):
        with patch.object(tool.subprocess, "run", return_value=subprocess.CompletedProcess(["fake"], 3, b"", b"failed")):
            result = tool.run_process(["fake"], self.output / "process")
        self.assertEqual(result["returncode"], 3)
        self.assertFalse(result["ok"])

    def test_run_probe_rejects_stale_output(self):
        output = self.output / "probe.json"
        output.write_text("stale")
        with self.assertRaisesRegex(ValueError, "already exists"):
            tool.run_probe("dummy", Path("m"), Path("s"), {"steps": 0}, output)

    def cli(self, output, *extra):
        return subprocess.run([sys.executable, str(TOOL), "--reference-root", str(self.reference),
                               "--assets", str(self.assets), "--probe", sys.executable,
                               "--output", str(output), *extra], capture_output=True, text=True)

    def test_cli_existing_output_reaches_rejection(self):
        p = self.cli(self.output)
        self.assertEqual(p.returncode, 2)
        self.assertIn("output/report already exists", p.stderr)

    def test_cli_missing_player_reaches_rejection(self):
        p = self.cli(self.root / "new", "--capture")
        self.assertEqual(p.returncode, 2)
        self.assertIn("probe/player binary missing", p.stderr)
        self.assertFalse((self.root / "new").exists())

    def test_cli_unknown_model_rejected(self):
        p = self.cli(self.root / "new", "--model", "unknown")
        self.assertEqual(p.returncode, 2)
        self.assertIn("not in catalog", p.stderr)

    def test_cli_empty_catalog_rejected(self):
        tool.write_json(self.reference / "config/model-catalog.json", {})
        p = self.cli(self.root / "new")
        self.assertEqual(p.returncode, 2)
        self.assertIn("catalog is empty", p.stderr)

    def test_full_catalog_includes_missing_session_and_model(self):
        (self.model_dir / "session.nks.json").unlink()
        p = self.cli(self.root / "new")
        self.assertEqual(p.returncode, 1)
        ledger = json.loads((self.root / "new/ledger.json").read_text())
        self.assertEqual(ledger["catalog_total"], 2)
        self.assertEqual({m["status"] for m in ledger["models"]}, {"MISSING_MODEL", "MISSING_SESSION"})
        self.assertTrue(ledger["complete"])
        self.assertFalse(ledger["all_requested_checks_passed"])
        self.assertEqual(ledger["freeze_errors"], [])


if __name__ == "__main__":
    unittest.main()
