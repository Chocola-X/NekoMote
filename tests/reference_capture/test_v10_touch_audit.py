#!/usr/bin/env python3
"""Synthetic and evidence verification tests for V10 touch observations."""

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
V10_LEDGER = ROOT / "build/evidence/v10-touch-audit/final-ledger/v9_touch_ledger.json"
V10_OBSERVATIONS = ROOT / "build/evidence/v10-touch-audit/observations.json"

@unittest.skipUnless(V10_LEDGER.exists(), "V10 ledger not yet built")
class TestV10Ledger(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(V10_LEDGER) as f:
            cls.ledger = json.load(f)

    def test_denominator_preserved(self):
        self.assertEqual(self.ledger["total_models"], 195)
        self.assertEqual(len(self.ledger["models"]), 195)

    def test_summary_counts(self):
        s = self.ledger["summary"]
        self.assertEqual(s["MATCH"], 18)
        self.assertEqual(s["MISMATCH"], 2)
        self.assertEqual(s["UNREVIEWED"], 160)
        self.assertEqual(s["PENDING_EVIDENCE"], 0)
        self.assertEqual(s["BLOCKED_INPUT"], 15)

    def test_fraise_b_observed_and_matched(self):
        by_name = {m["model"]: m for m in self.ledger["models"]}
        fraise_b = by_name["fraise-france-maid-b"]
        self.assertEqual(fraise_b["visual_alignment"], "MATCH")
        self.assertEqual(fraise_b["errors"], [])
        self.assertEqual(len(fraise_b["evidence"]), 7)
        self.assertEqual(fraise_b["conditions"], ["default_idle", "idle_120", "moved", "scaled_half"])
        self.assertEqual(fraise_b["checked_zones"], ["bust", "eye", "face", "head", "pant"])

    def test_candidates_ready_for_review(self):
        candidates = self.ledger.get("candidates", [])
        self.assertEqual(len(candidates), 2)
        for c in candidates:
            self.assertEqual(c["status"], "READY_FOR_REVIEW", f"{c['model']} errors: {c.get('errors')}")
            self.assertEqual(c["errors"], [])
            self.assertIn("default_idle", c["conditions"])
            self.assertIn("idle_120", c["conditions"])
            self.assertTrue(len(c["measured_points"]) >= 4)
            self.assertEqual(len(c["evidence"]), 7)

    def test_candidate_current_regions_match_session(self):
        candidates = {c["model"]: c for c in self.ledger.get("candidates", [])}
        self.assertIn("cinnamon-maid-b", candidates)
        self.assertIn("coconut-santa-b", candidates)
        
        cin = candidates["cinnamon-maid-b"]
        self.assertEqual(cin["current_author_region"], {"center": [0, -28.0], "radius": [132.6, 102.0]})
        self.assertEqual(cin["suggested_author_region"], {"center": [35.0, -28.0], "radius": [165.0, 110.0]})
        
        coco = candidates["coconut-santa-b"]
        self.assertEqual(coco["current_author_region"], {"center": [0, 38.0], "radius": [143.0, 110.0]})
        self.assertEqual(coco["suggested_author_region"], {"center": [-25.0, 38.0], "radius": [180.0, 115.0]})

    def test_unreviewed_models_untainted(self):
        unreviewed = [m for m in self.ledger["models"] if m["visual_alignment"] == "UNREVIEWED"]
        self.assertEqual(len(unreviewed), 160)
        for m in unreviewed:
            self.assertIsNone(m["observer"])
            self.assertEqual(m["checked_zones"], [])
            self.assertEqual(m["conditions"], [])
            self.assertEqual(m["evidence"], [])

    def test_blocked_models_preserved(self):
        blocked = [m for m in self.ledger["models"] if m["visual_alignment"] == "BLOCKED_INPUT"]
        self.assertEqual(len(blocked), 15)


if __name__ == "__main__":
    unittest.main()
