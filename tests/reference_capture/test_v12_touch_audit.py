#!/usr/bin/env python3
"""Unit tests for V12 touch observation and evidence audit."""
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_DIR = ROOT / "build" / "evidence" / "v12-touch-audit"
OBSERVATIONS_FILE = EVIDENCE_DIR / "observations.json"
LEDGER_FILE = EVIDENCE_DIR / "final-ledger" / "v9_touch_ledger.json"
VALIDATED_DIR = EVIDENCE_DIR / "validated_points"
SPECS_DIR = EVIDENCE_DIR / "specs"


class TestV12TouchAudit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not OBSERVATIONS_FILE.exists() or not LEDGER_FILE.exists():
            raise unittest.SkipTest("V12 evidence package not yet generated")
        cls.observations = json.loads(OBSERVATIONS_FILE.read_text())
        cls.ledger = json.loads(LEDGER_FILE.read_text())

    def test_version_three_schema(self):
        self.assertEqual(self.observations.get("version"), 3)
        self.assertIn("observations", self.observations)
        self.assertIn("candidates", self.observations)
        self.assertEqual(len(self.observations["observations"]), 3)
        self.assertEqual(len(self.observations["candidates"]), 2)

    def test_exact_195_denominator_and_no_extrapolation(self):
        summary = self.ledger.get("summary", {})
        self.assertEqual(self.ledger.get("total_models"), 195)
        self.assertEqual(summary.get("MATCH"), 1)
        self.assertEqual(summary.get("MISMATCH"), 2)
        self.assertEqual(summary.get("UNREVIEWED"), 177)
        self.assertEqual(summary.get("PENDING_EVIDENCE"), 0)
        self.assertEqual(summary.get("BLOCKED_INPUT"), 15)

    def test_fraise_visible_zones_and_pant_exclusion(self):
        fraise_obs = next(o for o in self.observations["observations"] if o["model"] == "fraise-france-maid-b")
        self.assertEqual(fraise_obs["status"], "MATCH")
        self.assertEqual(sorted(fraise_obs["checked_zones"]), ["bust", "eye", "face", "head"])
        self.assertNotIn("pant", fraise_obs["checked_zones"])
        self.assertIn("pant", fraise_obs["notes"].lower())
        self.assertTrue("画幅外" in fraise_obs["notes"] or "out of frame" in fraise_obs["notes"].lower())

    def test_cinnamon_mismatch_and_candidate(self):
        cinnamon_obs = next(o for o in self.observations["observations"] if o["model"] == "cinnamon-maid-b")
        self.assertEqual(cinnamon_obs["status"], "MISMATCH")
        self.assertEqual(cinnamon_obs["checked_zones"], ["bust"])

        candidate = next(c for c in self.observations["candidates"] if c["model"] == "cinnamon-maid-b")
        self.assertEqual(candidate["zone"], "bust")
        self.assertEqual(candidate["current_author_region"], {"center": [0, -28.0], "radius": [132.6, 102.0]})
        self.assertEqual(candidate["suggested_author_region"], {"center": [35.0, -28.0], "radius": [165.0, 110.0]})
        self.assertEqual(candidate["conditions"], ["default_idle", "idle_120"])
        self.assertGreaterEqual(len(candidate["measured_points"]), 5)

    def test_coconut_mismatch_and_candidate(self):
        coconut_obs = next(o for o in self.observations["observations"] if o["model"] == "coconut-santa-b")
        self.assertEqual(coconut_obs["status"], "MISMATCH")
        self.assertEqual(coconut_obs["checked_zones"], ["bust"])

        candidate = next(c for c in self.observations["candidates"] if c["model"] == "coconut-santa-b")
        self.assertEqual(candidate["zone"], "bust")
        self.assertEqual(candidate["current_author_region"], {"center": [0, 38.0], "radius": [143.0, 110.0]})
        self.assertEqual(candidate["suggested_author_region"], {"center": [-25.0, 38.0], "radius": [180.0, 115.0]})
        self.assertEqual(candidate["conditions"], ["default_idle", "idle_120"])
        self.assertGreaterEqual(len(candidate["measured_points"]), 5)

    def test_validated_points_all_pass(self):
        result_files = list(VALIDATED_DIR.glob("*.result.json"))
        self.assertEqual(len(result_files), 39)
        for rf in result_files:
            data = json.loads(rf.read_text())
            self.assertTrue(data.get("ok"), f"Point validation failed in {rf.name}: {data.get('errors')}")
            self.assertTrue(data.get("validated"))
            self.assertIn("probe", data)
            self.assertEqual(data["probe"].get("returncode"), 0)
            self.assertIn("output_sha256", data["probe"])

    def test_source_field_references_exist(self):
        for name in ("fraise-france-maid-b", "cinnamon-maid-b", "coconut-santa-b"):
            ref_file = EVIDENCE_DIR / "references" / f"{name}.reference.json"
            self.assertTrue(ref_file.is_file(), f"Missing reference file: {ref_file}")
            ref_data = json.loads(ref_file.read_text())
            self.assertEqual(ref_data.get("type"), "source-field")
            self.assertEqual(ref_data.get("model"), name)
            self.assertIn("default_screen_regions", ref_data)


if __name__ == "__main__":
    unittest.main()
