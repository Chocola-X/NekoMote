#!/usr/bin/env python3
"""V9 逐模型触摸区域视觉核对合成契约测试.

验证 V9 视觉账本、候选数据推荐、图片资产与几何不变量。
"""

import json
import os
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
LEDGER = ROOT / "build" / "evidence" / "v9-touch-review" / "v9_touch_ledger.json"
CANDIDATES = ROOT / "build" / "evidence" / "v9-touch-review" / "candidate_touch_adjustments.json"
CROPS_DIR = ROOT / "build" / "evidence" / "v9-touch-review" / "crops"
CAPTURES_DIR = ROOT / "build" / "evidence" / "v9-touch-review" / "captures"
FRAISE_G12 = ROOT / "build" / "evidence" / "g12-touch-audit" / "captures" / "fraise-france-maid-b"

ZONE_ORDER = ["bust", "eye", "face", "head", "pant"]
EXPECTED_TOTAL = 195
EXPECTED_OK = 180
EXPECTED_CPU_REJECTED = 15
EXPECTED_AUTHORED = 59
EXPECTED_ESTIMATED = 136
EXPECTED_MIRROR = 6


@unittest.skipUnless(LEDGER.exists(), "v9 ledger not generated yet")
class TestV9LedgerStructure(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(LEDGER, encoding="utf-8") as f:
            cls.ledger = json.load(f)

    def test_total_models(self):
        self.assertEqual(len(self.ledger["models"]), EXPECTED_TOTAL)
        self.assertEqual(self.ledger["total_models"], EXPECTED_TOTAL)

    def test_summary_consistency(self):
        s = self.ledger["summary"]
        self.assertEqual(s["total"], EXPECTED_TOTAL)
        self.assertEqual(s["ok"], EXPECTED_OK)
        self.assertEqual(s["cpu_rejected"], EXPECTED_CPU_REJECTED)
        self.assertEqual(s["authored_markers"], EXPECTED_AUTHORED)
        self.assertEqual(s["estimated_markers"], EXPECTED_ESTIMATED)
        self.assertEqual(s["mirrored_models"], EXPECTED_MIRROR)
        self.assertEqual(s["visual_match"], 178)
        self.assertEqual(s["visual_match_3q_limitation"], 2)
        self.assertEqual(s["blocked"], EXPECTED_CPU_REJECTED)

    def test_each_model_has_required_fields(self):
        for m in self.ledger["models"]:
            for field in [
                "model",
                "character",
                "status",
                "mirror",
                "marker_source",
                "active_zones",
                "visual_alignment",
                "priority_verified",
                "coordinate_chain_verified",
                "notes",
            ]:
                self.assertIn(field, m, f"{m.get('model')}: missing field {field}")

    def test_ok_models_have_all_active_zones(self):
        for m in self.ledger["models"]:
            if m["status"] == "OK":
                self.assertEqual(
                    m["active_zones"],
                    ZONE_ORDER,
                    f"{m['model']}: OK model must have all 5 active zones",
                )
                self.assertTrue(m["priority_verified"])
                self.assertTrue(m["coordinate_chain_verified"])

    def test_cpu_rejected_models_blocked(self):
        for m in self.ledger["models"]:
            if m["status"] == "CPU_REJECTED":
                self.assertEqual(m["visual_alignment"], "BLOCKED_CPU_REJECTED")
                self.assertFalse(m["priority_verified"])
                self.assertFalse(m["coordinate_chain_verified"])

    def test_mirrored_models_are_fraise(self):
        for m in self.ledger["models"]:
            if m["mirror"] is True:
                self.assertEqual(m["character"], "fraise")
                self.assertTrue(m["model"].endswith("-b"))


@unittest.skipUnless(CANDIDATES.exists(), "candidate adjustments not generated yet")
class TestV9Candidates(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(CANDIDATES, encoding="utf-8") as f:
            cls.candidates = json.load(f)["candidates"]

    def test_candidate_count(self):
        self.assertEqual(len(self.candidates), 2)

    def test_candidate_fields(self):
        for c in self.candidates:
            for field in [
                "model",
                "zone",
                "current_author_region",
                "suggested_author_region",
                "rationale",
                "source_hash",
                "applicable_pose",
                "status",
            ]:
                self.assertIn(field, c, f"Candidate {c.get('model')}: missing {field}")
            self.assertEqual(c["status"], "CANDIDATE_FOR_CODEX_REVIEW")

    def test_candidate_models(self):
        models = {c["model"] for c in self.candidates}
        self.assertEqual(models, {"cinnamon-maid-b", "coconut-santa-b"})


@unittest.skipUnless(CAPTURES_DIR.exists() and CROPS_DIR.exists(), "captures not available")
class TestV9Captures(unittest.TestCase):
    def test_fraise_g12_captures_exist(self):
        if FRAISE_G12.exists():
            for name in ["default_idle.png", "idle_120.png", "moved.png", "scaled_half.png"]:
                p = FRAISE_G12 / name
                self.assertTrue(p.exists(), f"Missing G12 capture: {p}")

    def test_representative_crops_exist(self):
        expected_models = [
            "chocola-date-a", "chocola-casual",
            "vanilla-date-a", "vanilla-casual",
            "azuki-casual", "azuki-santa",
            "maple-casual", "maple-maid",
            "cinnamon-casual", "cinnamon-maid-b",
            "coconut-casual", "coconut-santa-b",
            "milk-summer", "milk-winter",
            "fraise-france-maid-b", "fraise-maid-a"
        ]
        for m in expected_models:
            head_crop = CROPS_DIR / f"{m}_head_crop.png"
            bust_crop = CROPS_DIR / f"{m}_bust_crop.png"
            self.assertTrue(head_crop.exists(), f"Missing crop: {head_crop}")
            self.assertTrue(bust_crop.exists(), f"Missing crop: {bust_crop}")


class TestV9CoordinateMath(unittest.TestCase):
    def test_mirror_coordinate_roundtrip(self):
        # Center-origin pixel coordinate X_pix
        # Origin X_origin, Scale S
        # Math: X_author = (X_pix - X_origin) / S; if mirror: X_author = -X_author
        # Inverse: X_pix = X_origin + (direction * S * X_author) where direction = -1 if mirror else 1
        for origin_x in [0.0, 80.0, -120.0]:
            for scale in [0.5, 1.0, 1.5]:
                for author_x in [-50.0, 0.0, 100.0]:
                    for mirror in [False, True]:
                        direction = -1.0 if mirror else 1.0
                        pix_x = origin_x + direction * scale * author_x
                        # Forward touch mapping:
                        mapped_author_x = (pix_x - origin_x) / scale
                        if mirror:
                            mapped_author_x = -mapped_author_x
                        self.assertAlmostEqual(
                            mapped_author_x,
                            author_x,
                            places=5,
                            msg=f"Coordinate roundtrip failed for mirror={mirror}, origin={origin_x}, scale={scale}"
                        )


if __name__ == "__main__":
    unittest.main()
