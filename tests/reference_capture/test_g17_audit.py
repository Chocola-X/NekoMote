import unittest
import json
import os
import sys
import tempfile
import subprocess

SCRIPT = os.path.join(os.path.dirname(__file__), "..", "..",
                      "tools", "g17_support_audit", "audit_support_gaps.py")

K_STANDARD = 0x20007fc
K_STENCIL = 0x24007fc
K_PARTIAL = 0x200020c
K_HIT = 0x20003fc
K_ARM = 0x7fc


def make_layer(label, inherit_mask, ltype=0, children=None, frame_list=None):
    return {
        "label": label,
        "type": ltype,
        "inherit_mask": inherit_mask,
        "coordinate": 0,
        "mesh_transform": 0,
        "mesh_combine": 0,
        "mesh_sync_child_mask": 0,
        "transform_order": [0, 3, 2, 1],
        "children": children or [],
        "frame_list": frame_list or [],
    }


def make_model(layers):
    return {"model": {"parts": [{"name": "test", "layers": layers}],
                      "icons": {}}}


class AuditSupportGapsTest(unittest.TestCase):
    def run_audit(self, model_json):
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
            json.dump(model_json, f)
            path = f.name
        try:
            proc = subprocess.run(
                [sys.executable, SCRIPT, path],
                capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            return json.loads(proc.stdout)
        finally:
            os.unlink(path)

    def test_standard_inherit_no_findings(self):
        model = make_model([make_layer("ok", K_STANDARD)])
        result = self.run_audit(model)
        findings = list(result.values())[0]
        self.assertEqual(findings, {})

    def test_stencil_inherit_no_findings(self):
        model = make_model([make_layer("stencil", K_STENCIL, ltype=12)])
        result = self.run_audit(model)
        findings = list(result.values())[0]
        self.assertEqual(findings, {})

    def test_partial_marker_0x200020c_detected(self):
        model = make_model([make_layer("center_bust", K_PARTIAL, ltype=1)])
        result = self.run_audit(model)
        findings = list(result.values())[0]
        self.assertIn("inheritance", findings)
        self.assertEqual(len(findings["inheritance"]), 1)
        self.assertEqual(findings["inheritance"][0]["value"], hex(K_PARTIAL))
        self.assertTrue(findings["inheritance"][0]["is_partial_marker"])

    def test_hit_mask_0x20003fc_detected(self):
        model = make_model([make_layer("hit_bust", K_HIT, ltype=1)])
        result = self.run_audit(model)
        findings = list(result.values())[0]
        self.assertIn("inheritance", findings)
        self.assertEqual(findings["inheritance"][0]["value"], hex(K_HIT))
        self.assertFalse(findings["inheritance"][0]["is_partial_marker"])

    def test_arm_mask_0x7fc_detected(self):
        model = make_model([make_layer("arm", K_ARM, ltype=2)])
        result = self.run_audit(model)
        findings = list(result.values())[0]
        self.assertIn("inheritance", findings)
        self.assertEqual(findings["inheritance"][0]["value"], hex(K_ARM))

    def test_coordinate_nonzero_detected(self):
        layer = make_layer("bad", K_STANDARD)
        layer["coordinate"] = 1
        model = make_model([layer])
        result = self.run_audit(model)
        findings = list(result.values())[0]
        self.assertIn("transform_common", findings)

    def test_mesh_transform_detected(self):
        layer = make_layer("bad", K_STANDARD)
        layer["mesh_transform"] = 2
        model = make_model([layer])
        result = self.run_audit(model)
        findings = list(result.values())[0]
        self.assertIn("transform_common", findings)

    def test_transform_order_detected(self):
        layer = make_layer("bad", K_STANDARD)
        layer["transform_order"] = [0, 1, 2, 3]
        model = make_model([layer])
        result = self.run_audit(model)
        findings = list(result.values())[0]
        self.assertIn("transform_order", findings)

    def test_attr_detected(self):
        model = {"model": {"parts": [],
                            "icons": {"tex/0000": {"attr": 3}}}}
        result = self.run_audit(model)
        findings = list(result.values())[0]
        self.assertIn("attr", findings)
        self.assertEqual(findings["attr"][0]["value"], 3)

    def test_nested_children_scanned(self):
        child = make_layer("inner", K_PARTIAL, ltype=1)
        outer = make_layer("outer", K_STANDARD, children=[child])
        model = make_model([outer])
        result = self.run_audit(model)
        findings = list(result.values())[0]
        self.assertIn("inheritance", findings)
        self.assertEqual(findings["inheritance"][0]["layer"], "test/outer/inner")


if __name__ == "__main__":
    unittest.main()
