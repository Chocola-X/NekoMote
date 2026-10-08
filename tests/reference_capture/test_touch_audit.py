#!/usr/bin/env python3
"""G12 触摸审计账本与工具的合成契约测试.

验证账本 JSON 结构、不变量和工具负例处理。
不运行 session_probe；账本验证在账本文件存在时执行，否则跳过。
"""

import json
import os
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TOOL = ROOT / "tools" / "touch_audit" / "run_touch_audit.py"
LEDGER = ROOT / "build" / "evidence" / "g12-touch-audit" / "ledger.json"

ZONE_ORDER = ["bust", "eye", "face", "head", "pant"]
EXPECTED_CONDITIONS = ["default_idle", "idle_120", "moved", "scaled_half"]
EXPECTED_TOTAL = 195


@unittest.skipUnless(LEDGER.exists(), "ledger not generated yet")
class TestLedgerStructure(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(LEDGER) as f:
            cls.ledger = json.load(f)

    def test_total_models(self):
        self.assertEqual(len(self.ledger["models"]), EXPECTED_TOTAL)

    def test_freeze_fields(self):
        for field in [
            "generated_at",
            "head_commit",
            "dirty",
            "tool_sha256",
            "probe_sha256",
            "catalog_sha256",
            "summary",
        ]:
            self.assertIn(field, self.ledger, f"missing freeze field: {field}")

    def test_status_counts(self):
        sc = self.ledger["summary"]["status_counts"]
        self.assertEqual(sum(sc.values()), EXPECTED_TOTAL)
        self.assertIn("OK", sc)
        self.assertGreater(sc["OK"], 0)

    def test_condition_names(self):
        self.assertEqual(
            self.ledger["conditions"], EXPECTED_CONDITIONS
        )

    def test_each_model_has_required_fields(self):
        for m in self.ledger["models"]:
            for field in ["model", "status", "conditions", "errors"]:
                self.assertIn(field, m, f"{m.get('model')}: missing {field}")

    def test_ok_models_have_conditions(self):
        for m in self.ledger["models"]:
            if m["status"] == "OK":
                self.assertEqual(
                    len(m["conditions"]),
                    len(EXPECTED_CONDITIONS),
                    f"{m['model']}: wrong condition count",
                )

    def test_cpu_rejected_have_no_conditions(self):
        for m in self.ledger["models"]:
            if m["status"] == "CPU_REJECTED":
                self.assertEqual(
                    len(m["conditions"]),
                    0,
                    f"{m['model']}: CPU_REJECTED should have no conditions",
                )


@unittest.skipUnless(LEDGER.exists(), "ledger not generated yet")
class TestLedgerInvariants(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(LEDGER) as f:
            cls.ledger = json.load(f)

    def test_outside_point_never_triggers(self):
        for m in self.ledger["models"]:
            if m["status"] != "OK":
                continue
            for c in m["conditions"]:
                ot = c.get("outside_test")
                if ot is None:
                    continue
                self.assertFalse(
                    ot["reaction"],
                    f"{m['model']}/{c['name']}: outside point triggered "
                    f"reaction {ot['reaction']}",
                )

    def test_ok_model_center_hits(self):
        for m in self.ledger["models"]:
            if m["status"] != "OK":
                continue
            for c in m["conditions"]:
                for z in ZONE_ORDER:
                    zt = c.get("zone_tests", {}).get(z, {})
                    if zt.get("skipped"):
                        continue
                    ct = zt.get("center_test", {})
                    self.assertTrue(
                        ct.get("pass"),
                        f"{m['model']}/{c['name']}/{z}: center did not hit "
                        f"(reaction={ct.get('reaction')})",
                    )

    def test_mirror_and_non_mirror_exist(self):
        mirrors = [m for m in self.ledger["models"] if m.get("mirror") is True]
        non_mirrors = [
            m for m in self.ledger["models"] if m.get("mirror") is False
        ]
        self.assertGreater(len(mirrors), 0, "no mirror models found")
        self.assertGreater(len(non_mirrors), 0, "no non-mirror models found")

    def test_projected_regions_match_author_transform(self):
        for m in self.ledger["models"]:
            if m["status"] != "OK":
                continue
            author = m.get("author_regions", {})
            for c in m["conditions"]:
                origin = c["params"]["origin"]
                scale = c["params"]["scale"]
                direction = -1 if m.get("mirror") else 1
                for z in ZONE_ORDER:
                    ar = author.get(z)
                    pr = c.get("projected_regions", {}).get(z)
                    if ar is None or pr is None:
                        continue
                    if ar["radius"][0] == 0:
                        continue
                    inferred_author_scale = pr["radius"][0] / (
                        scale * ar["radius"][0]
                    )
                    total_scale = scale * inferred_author_scale
                    expected_cx = origin[0] + direction * total_scale * ar["center"][0]
                    expected_cy = origin[1] + total_scale * ar["center"][1]
                    self.assertAlmostEqual(
                        pr["center"][0],
                        expected_cx,
                        places=0,
                        msg=f"{m['model']}/{c['name']}/{z}: center.x mismatch",
                    )
                    self.assertAlmostEqual(
                        pr["center"][1],
                        expected_cy,
                        places=0,
                        msg=f"{m['model']}/{c['name']}/{z}: center.y mismatch",
                    )
                    self.assertAlmostEqual(
                        pr["radius"][1],
                        total_scale * ar["radius"][1],
                        places=0,
                        msg=f"{m['model']}/{c['name']}/{z}: radius.y mismatch",
                    )


class TestToolNegatives(unittest.TestCase):
    """合成负例：验证工具对缺失/无效输入的处理."""

    def test_missing_model_dir(self):
        sys.path.insert(0, str(ROOT / "tools" / "touch_audit"))
        import run_touch_audit

        assets = ROOT / "assets"
        output = ROOT / "build" / "evidence" / "g12-touch-audit" / "_test"
        output.mkdir(parents=True, exist_ok=True)
        entry = run_touch_audit.audit_model(
            "dummy_probe",
            None,
            assets,
            "__nonexistent_model__",
            output,
            False,
        )
        self.assertEqual(entry["status"], "MISSING_MODEL")
        self.assertTrue(entry["errors"])

    def test_find_zone_of_reaction(self):
        sys.path.insert(0, str(ROOT / "tools" / "touch_audit"))
        import run_touch_audit

        zr = {"bust": ["a/b01"], "eye": ["a/blink"]}
        self.assertEqual(
            run_touch_audit.find_zone_of_reaction("a/b01", zr), "bust"
        )
        self.assertEqual(
            run_touch_audit.find_zone_of_reaction("a/blink", zr), "eye"
        )
        self.assertIsNone(
            run_touch_audit.find_zone_of_reaction("unknown", zr)
        )
        self.assertIsNone(
            run_touch_audit.find_zone_of_reaction("", zr)
        )

    def test_inside_point_within_ellipse(self):
        sys.path.insert(0, str(ROOT / "tools" / "touch_audit"))
        import run_touch_audit

        center = [100.0, 200.0]
        radius = [50.0, 80.0]
        pt = run_touch_audit.inside_point(center, radius)
        dx = (pt[0] - center[0]) / radius[0]
        dy = (pt[1] - center[1]) / radius[1]
        self.assertLess(dx * dx + dy * dy, 1.0)

    def test_outside_point_beyond_ellipse(self):
        sys.path.insert(0, str(ROOT / "tools" / "touch_audit"))
        import run_touch_audit

        center = [100.0, 200.0]
        radius = [50.0, 80.0]
        pt = run_touch_audit.outside_point_for_zone(center, radius)
        dx = (pt[0] - center[0]) / radius[0]
        dy = (pt[1] - center[1]) / radius[1]
        self.assertGreater(dx * dx + dy * dy, 1.0)

    def test_zone_order_is_priority(self):
        sys.path.insert(0, str(ROOT / "tools" / "touch_audit"))
        import run_touch_audit

        self.assertEqual(
            run_touch_audit.ZONE_ORDER,
            ["bust", "eye", "face", "head", "pant"],
        )

    def test_conditions_cover_required_cases(self):
        sys.path.insert(0, str(ROOT / "tools" / "touch_audit"))
        import run_touch_audit

        names = [c["name"] for c in run_touch_audit.CONDITIONS]
        self.assertIn("default_idle", names)
        self.assertIn("idle_120", names)
        self.assertIn("moved", names)
        self.assertIn("scaled_half", names)
        for c in run_touch_audit.CONDITIONS:
            self.assertGreaterEqual(c["steps"], 0)
            self.assertGreater(c["scale"], 0)


if __name__ == "__main__":
    unittest.main()
