"""Schema 6 frame fixtures and retained process/error evidence, without a GPU."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/g17_support_audit/audit_support_gaps.py"
REPRODUCER = SCRIPT.with_name("reproduce_fails.py")
spec = importlib.util.spec_from_file_location("g17_audit", SCRIPT)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def field(value=None, state=2):
    return {"state": state, **({"value": value} if state == 2 else {})}


def content(**kwargs):
    result = {key: field(state=0) for key in audit.CONTENT_FIELDS}
    result.update({key: field(value) for key, value in kwargs.items()})
    return {"time": 0, "type": 2, "content": field(result)}


def layer(label="test", mask=audit.K_STANDARD_INHERIT, ltype=0, frames=None, children=None):
    return {"label": label, "type": ltype, "inherit_mask": mask, "coordinate": 0,
            "mesh_transform": 0, "mesh_combine": 0, "mesh_sync_child_mask": 0,
            "mesh_division": 0, "export_self": 1, "stencil_type": 5 if ltype == 12 else 0,
            "transform_order": [0, 3, 2, 1], "children": children or [],
            "frame_list": frames or [], "parameterize": field(state=0), "parameter": []}


def part(name="test", layers=None):
    return {"name": name, "group": "all_parts", "layers": layers or [],
            "parameterize": field(state=0), "parameter": []}


def model(layers=None):
    return {"schema": 6, "model": {"parts": [part(layers=layers)], "icons": {}}}


class AuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / "model.nkm.json"

    def run_audit(self, value):
        self.path.write_text(json.dumps(value))
        return audit.audit_model(self.path)

    def fields(self, value):
        return [f["field"] for f in self.run_audit(value)["findings"]]

    def test_standard_no_findings(self):
        self.assertEqual(self.fields(model([layer()])), [])

    def test_stencil_mask_supported(self):
        self.assertEqual(self.fields(model([layer(mask=audit.K_STENCIL_INHERIT, ltype=12)])), [])

    def test_nonstandard_inheritance_candidate(self):
        for mask in (0x7fc, 0x200020c, 0x20003fc):
            result = self.run_audit(model([layer(mask=mask)]))
            self.assertEqual(result["findings"][0]["classification"], "static_potential")
            self.assertFalse(result["runtime_trigger_certified"])

    def test_nested_children_pointer(self):
        result = self.run_audit(model([layer(children=[layer(mask=0x7fc)])]))
        self.assertEqual(result["findings"][0]["path"], "/model/parts/0/layers/0/children/0/inherit_mask")

    def test_coordinate(self):
        l = layer()
        l["coordinate"] = 1
        self.assertIn("coordinate", self.fields(model([l])))

    def test_mesh_transform(self):
        l = layer()
        l["mesh_transform"] = 2
        self.assertIn("mesh_transform", self.fields(model([l])))

    def test_transform_order(self):
        l = layer()
        l["transform_order"] = [0, 1, 2, 3]
        self.assertIn("transform_order", self.fields(model([l])))

    def test_attr(self):
        m = model()
        m["model"]["icons"] = {"tex/0000": {"attr": 3}}
        self.assertEqual(self.fields(m), ["attr"])

    def test_real_shape_zx_zy_detected(self):
        result = self.run_audit(model([layer(ltype=1, frames=[content(zx=20, zy=11)])]))
        self.assertEqual([f["field"] for f in result["findings"]], ["zx", "zy"])
        self.assertTrue(all(f["leaf"] for f in result["findings"]))
        self.assertEqual(result["counts"]["active_content_frames"], 1)

    def test_bm19_and_zero_supported(self):
        for bm in (0, 19):
            self.assertNotIn("bm", self.fields(model([layer(frames=[content(bm=bm)])])))

    def test_unverified_bm_detected(self):
        self.assertEqual(self.fields(model([layer(frames=[content(bm=999)])])), ["bm"])

    def test_flip_color_offsets(self):
        self.assertEqual(self.fields(model([layer(frames=[content(flip=1, color=0, ox=2, oy=3)])])),
                         ["ox", "oy", "flip", "color"])

    def test_null_field_is_not_value(self):
        f = content()
        f["content"]["value"]["zx"] = field(state=1)
        self.assertEqual(self.fields(model([layer(frames=[f])])), [])

    def test_frame_type_zero_does_not_evaluate_content(self):
        f = content(bm=999, zx=20)
        f["type"] = 0
        self.assertEqual(self.fields(model([layer(frames=[f])])), [])

    def test_frame_type_one_detected(self):
        f = content()
        f["type"] = 1
        self.assertIn("type", self.fields(model([layer(frames=[f])])))

    def test_free_clock_offset_candidate(self):
        f = content(motion={"time_offset": field(4), "mask": field(0)}, src="all_parts", icon="target")
        m = model([layer(frames=[f])])
        m["model"]["parts"].append(part("target", [layer()]))
        result = self.run_audit(m)
        self.assertEqual(result["findings"][0]["field"], "motion.time_offset")
        self.assertFalse(result["findings"][0]["parameter_driven"])

    def test_parameter_clock_offset_not_rejected(self):
        f = content(motion={"time_offset": field(20), "mask": field(0)}, src="all_parts", icon="target")
        m = model([layer(frames=[f])])
        target = part("target", [layer()])
        target["parameterize"] = field(0)
        target["parameter"] = [{"enabled": True}]
        m["model"]["parts"].append(target)
        result = self.run_audit(m)
        self.assertEqual(result["findings"], [])
        self.assertEqual(result["supported_observations"][0]["classification"], "supported_parameter_clock")

    def test_disabled_parameter_does_not_own_clock(self):
        target = part("target", [layer()])
        target["parameterize"] = field(0)
        target["parameter"] = [{"enabled": False}]
        self.assertFalse(audit.parameter_driven(target))

    def test_layer_binding_overrides_part_binding(self):
        target = part("target", [layer()])
        target["parameterize"] = field(0)
        target["parameter"] = [{"enabled": True}]
        target["layers"][0]["parameterize"] = field(1)
        self.assertFalse(audit.parameter_driven(target))

    def test_mesh_cc_is_geometry_compile_candidate(self):
        result = self.run_audit(model([layer(frames=[content(mesh={"cc": field([0] * 32), "bp": field(state=0)})])]))
        self.assertEqual(result["findings"][0]["field"], "mesh.cc")
        self.assertEqual(result["findings"][0]["stage"], "geometry_compile")

    def test_invalid_schema_explicitly_rejected(self):
        for schema in (5, "6", True, None):
            m = model()
            m["schema"] = schema
            with self.assertRaisesRegex(ValueError, "unsupported asset schema"):
                self.run_audit(m)

    def test_invalid_field_state_rejected(self):
        f = content()
        f["content"]["state"] = 3
        with self.assertRaisesRegex(ValueError, "invalid Field state"):
            self.run_audit(model([layer(frames=[f])]))

    def test_missing_field_value_rejected(self):
        f = content()
        f["content"] = {"state": 2}
        with self.assertRaisesRegex(ValueError, "Field Value missing"):
            self.run_audit(model([layer(frames=[f])]))

    def test_missing_content_member_rejected(self):
        f = content()
        del f["content"]["value"]["zx"]
        with self.assertRaisesRegex(ValueError, "missing Field zx"):
            self.run_audit(model([layer(frames=[f])]))

    def test_cli_invalid_asset_exits_nonzero(self):
        self.path.write_text(json.dumps({"schema": 5, "model": {}}))
        p = subprocess.run([sys.executable, SCRIPT, self.path], capture_output=True, text=True)
        self.assertEqual(p.returncode, 1)
        self.assertEqual(json.loads(p.stdout)["models"][0]["status"], "INVALID_ASSET")


class ReproductionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / "assets"
        directory = self.assets / "characters/demo"
        directory.mkdir(parents=True)
        self.model = directory / "model.nkm.json"
        self.session = directory / "session.nks.json"
        self.model.write_text(json.dumps(model([layer(frames=[content(zx=20, zy=11)])])))
        self.session.write_text("{}")
        self.probe = self.root / "probe"
        self.probe.write_text("#!/usr/bin/env python3\nimport sys\nsys.stderr.write('fixture fail\\n')\nraise SystemExit(1)\n")
        self.probe.chmod(0o755)
        self.catalog = self.root / "catalog.json"
        self.catalog.write_text(json.dumps({"demo": {}, "untested": {}}))
        self.stderr = self.root / "old.stderr.log"
        self.stderr.write_text("fixture fail\n")
        self.prior = self.root / "prior.json"
        self.prior.write_text(json.dumps({"returncode": 1, "stderr": audit.file_record(self.stderr)}))
        records = []
        for role, path in (("model_asset", self.model), ("session_asset", self.session)):
            records.append({"role": role, **audit.file_record(path)})
        self.baseline = {"complete": True, "scope": "full_catalog", "catalog_total": 2,
                         "catalog": audit.file_record(self.catalog), "probe": audit.file_record(self.probe),
                         "models": [{"model": "demo", "status": "FAIL", "assets": records,
                                     "conditions": [{"evidence": str(self.prior)}]},
                                    {"model": "untested", "status": "OK"}]}
        self.ledger = self.root / "ledger.json"
        self.output = self.root / "output"

    def run_reproduction(self, *extra):
        self.ledger.write_text(json.dumps(self.baseline))
        return subprocess.run([sys.executable, str(REPRODUCER), "--probe", str(self.probe),
                               "--assets", str(self.assets), "--ledger", str(self.ledger),
                               "--output", str(self.output), *extra], capture_output=True, text=True)

    def test_fail_reproduced_stays_fail_and_other_row_not_rerun(self):
        p = self.run_reproduction()
        self.assertEqual(p.returncode, 1, p.stderr)
        result = json.loads((self.output / "ledger.json").read_text())
        self.assertEqual(result["summary"]["status_counts"], {"FAIL": 1, "NOT_RERUN": 1})
        self.assertEqual(result["summary"]["expected_failures_reproduced"], 1)
        self.assertEqual(result["summary"]["findings"], {"zx": 1, "zy": 1})
        self.assertEqual(result["freeze_errors"], [])
        for path in ("request.json", "process.json", "stdout.log", "stderr.log", "static-audit.json"):
            self.assertTrue((self.output / "demo" / path).exists())
        self.assertTrue((self.output / "baseline-ledger.json").exists())

    def test_existing_output_rejected(self):
        self.output.mkdir()
        p = self.run_reproduction()
        self.assertEqual(p.returncode, 2)
        self.assertIn("output already exists", p.stderr)

    def test_changed_probe_rejected(self):
        self.probe.write_text(self.probe.read_text() + "#changed\n")
        p = self.run_reproduction()
        self.assertEqual(p.returncode, 2)
        self.assertIn("probe hash differs", p.stderr)

    def test_duplicate_coverage_rejected(self):
        self.baseline["models"][1]["model"] = "demo"
        p = self.run_reproduction()
        self.assertEqual(p.returncode, 2)
        self.assertIn("catalog coverage invalid", p.stderr)

    def test_changed_asset_not_executed_as_valid_evidence(self):
        self.session.write_text('{"changed":true}')
        p = self.run_reproduction()
        self.assertEqual(p.returncode, 1)
        result = json.loads((self.output / "ledger.json").read_text())
        self.assertEqual(result["models"][0]["status"], "INVALID_PROVENANCE_OR_ASSET")
        self.assertFalse((self.output / "demo/process.json").exists())

    def test_timeout_retained_as_failure(self):
        self.probe.write_text("#!/usr/bin/env python3\nimport time\ntime.sleep(5)\n")
        self.baseline["probe"] = audit.file_record(self.probe)
        p = self.run_reproduction("--timeout", "0.05")
        self.assertEqual(p.returncode, 1)
        result = json.loads((self.output / "ledger.json").read_text())
        self.assertTrue(result["models"][0]["process"]["timeout"])
        self.assertFalse(result["models"][0]["expected_failure_reproduced"])


if __name__ == "__main__":
    unittest.main()
