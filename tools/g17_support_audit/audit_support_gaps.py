#!/usr/bin/env python3
"""Schema 6 static geometry candidates, not proof of a runtime trigger.

Scan authored frames, including currently unselected motions. Findings are
static_potential until an evaluated node/path establishes a runtime failure.
This is a targeted semantic audit, not the complete native asset validator.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
K_STANDARD_INHERIT = 0x20007fc
K_STENCIL_INHERIT = 0x24007fc
K_PARTIAL_INHERIT = 0x200020c
K_NEUTRAL_COLOR = 0x808080ff
CONTENT_FIELDS = ("angle", "bm", "color", "coord", "flip", "icon", "mask", "mesh",
                  "motion", "opa", "ox", "oy", "src", "zx", "zy")


def file_record(path):
    path = Path(path).resolve()
    return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "bytes": path.stat().st_size}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def field_value(obj, key):
    require(isinstance(obj, dict) and key in obj, f"missing Field {key}")
    f = obj[key]
    require(isinstance(f, dict) and type(f.get("state")) is int and f["state"] in (0, 1, 2),
            f"invalid Field state: {key}")
    if f["state"] != 2:
        return False, None
    require("value" in f and f["value"] is not None, f"Field Value missing: {key}")
    return True, f["value"]


def integer(value, path):
    require(type(value) is int, f"expected integer: {path}")
    return value


def number(value, path):
    require(type(value) in (int, float) and math.isfinite(value), f"expected finite number: {path}")
    return value


def parameter_driven(part):
    """Same ownership condition as geometry compile's Resource::parameter_driven."""
    if not part["layers"]:
        return False
    for layer in part["layers"]:
        present, binding = field_value(layer, "parameterize")
        if not present:
            present, binding = field_value(part, "parameterize")
        parameters = layer["parameter"] or part["parameter"]
        if not present or type(binding) is not int or not 0 <= binding < len(parameters):
            return False
        if not parameters[binding]["enabled"]:
            return False
    return True


def audit_model(model_path):
    document = json.loads(Path(model_path).read_text())
    require(type(document.get("schema")) is int and document["schema"] == 6,
            "unsupported asset schema; rebuild schema 6 assets")
    model = document.get("model")
    require(isinstance(model, dict) and isinstance(model.get("parts"), list) and
            isinstance(model.get("icons"), dict), "invalid model parts/icons")
    findings = []
    notes = []
    counts = {"parts": len(model["parts"]), "layers": 0, "frames": 0, "active_content_frames": 0}
    motions = {}
    for part in model["parts"]:
        require(isinstance(part, dict) and isinstance(part.get("group"), str) and
                isinstance(part.get("name"), str) and isinstance(part.get("layers"), list) and
                isinstance(part.get("parameter"), list), "invalid part")
        field_value(part, "parameterize")
        key = part["group"] + "/" + part["name"]
        require(key not in motions, "duplicate motion resource")
        motions[key] = part

    def add(category, path, label, field, value, condition, **extra):
        findings.append({"category": category, "path": path, "label": label,
                         "field": field, "value": value, "condition": condition,
                         "classification": "static_potential", **extra})

    def scan(layer, path):
        require(isinstance(layer, dict) and isinstance(layer.get("label"), str) and
                isinstance(layer.get("children"), list) and isinstance(layer.get("frame_list"), list) and
                isinstance(layer.get("parameter"), list), f"invalid layer: {path}")
        field_value(layer, "parameterize")
        counts["layers"] += 1
        label = layer["label"]
        ltype = integer(layer["type"], path + "/type")
        mask = integer(layer["inherit_mask"], path + "/inherit_mask")
        details = {"layer_type": ltype, "leaf": not layer["children"],
                   "export_self": layer["export_self"]}
        if mask != K_STANDARD_INHERIT and not (ltype == 12 and mask == K_STENCIL_INHERIT):
            add("inheritance", path + "/inherit_mask", label, "inherit_mask", hex(mask),
                "nonstandard inheritance; selection/render/locator path not evaluated", **details)
        allowed = {"coordinate": {0}, "mesh_transform": {0, 1}, "mesh_combine": {0, 1},
                   "mesh_sync_child_mask": {0, 1, 8}}
        for field, values in allowed.items():
            value = integer(layer[field], path + "/" + field)
            if value not in values:
                add("transform_common", path + "/" + field, label, field, value,
                    "outside supported values " + str(sorted(values)), **details)
        order = layer["transform_order"]
        require(isinstance(order, list) and all(type(v) is int for v in order), "invalid transform_order")
        if order and order != [0, 3, 2, 1]:
            add("transform_order", path + "/transform_order", label, "transform_order", order,
                "nonempty order differs from [0,3,2,1]", **details)
        if ltype not in (0, 1, 2, 3, 12):
            add("layer_type", path + "/type", label, "type", ltype, "unverified layer type", **details)
        if ltype == 12 and layer["stencil_type"] != 5:
            add("stencil", path + "/stencil_type", label, "stencil_type", layer["stencil_type"],
                "only stencil type 5 supported", **details)
        division = integer(layer["mesh_division"], path + "/mesh_division")
        if not 0 <= division <= 256:
            add("mesh_division", path + "/mesh_division", label, "mesh_division", division,
                "outside geometry compiler capacity", **details)
        previous = -math.inf
        for index, frame in enumerate(layer["frame_list"]):
            fp = f"{path}/frame_list/{index}"
            require(isinstance(frame, dict), "invalid frame object")
            ftype = integer(frame["type"], fp + "/type")
            t = number(frame["time"], fp + "/time")
            counts["frames"] += 1
            if ftype not in (0, 1, 2, 3) or t < previous:
                add("frame_type_time", fp, label, "type/time", [ftype, t], "invalid frame type or unsorted time", **details)
            previous = t
            present, content = field_value(frame, "content")
            if not present:
                continue
            require(isinstance(content, dict), "invalid FrameContent")
            values = {key: field_value(content, key) for key in CONTENT_FIELDS}
            if ftype == 0:
                continue  # Native pose() returns before any content reject checks.
            counts["active_content_frames"] += 1
            if ftype == 1:
                add("frame_type", fp + "/type", label, "type", ftype, "pose rejects frame type 1", **details)
            for axis in ("zx", "zy", "ox", "oy"):
                has, value = values[axis]
                if has:
                    number(value, fp + "/" + axis)
                    add("frame_field", fp + "/content/value/" + axis, label, axis, value,
                        "pose marks axis/offset Value unsupported", **details)
            for field, supported in (("bm", {0, 19}), ("flip", {0}), ("color", {K_NEUTRAL_COLOR})):
                has, value = values[field]
                if has and integer(value, fp + "/" + field) not in supported:
                    add("blend_mode" if field == "bm" else "frame_field",
                        fp + "/content/value/" + field, label, field, value,
                        "outside supported values " + str(sorted(supported)), **details)
            has, mesh = values["mesh"]
            if has:
                require(isinstance(mesh, dict), "invalid mesh")
                has_cc, cc = field_value(mesh, "cc")
                if has_cc:
                    add("mesh_cc", fp + "/content/value/mesh/value/cc", label, "mesh.cc", cc,
                        "geometry pose compile rejects cc Value; importer/asset validation do not",
                        stage="geometry_compile", **details)
            has, motion = values["motion"]
            if has:
                require(isinstance(motion, dict), "invalid motion")
                has_offset, offset = field_value(motion, "time_offset")
                if has_offset and number(offset, fp + "/motion/time_offset") != 0:
                    has_src, src = values["src"]
                    has_icon, icon = values["icon"]
                    target = str(src) + "/" + str(icon) if has_src and has_icon else None
                    driven = parameter_driven(motions[target]) if target in motions else False
                    if driven:
                        notes.append({"path": fp, "field": "motion.time_offset", "value": offset,
                                      "resource": target, "parameter_driven": True,
                                      "classification": "supported_parameter_clock"})
                    else:
                        add("frame_field", fp + "/content/value/motion/value/time_offset", label,
                            "motion.time_offset", offset,
                            "nonzero offset on non-parameter-driven/unresolved resource",
                            resource=target, parameter_driven=False, **details)
        for index, child in enumerate(layer["children"]):
            scan(child, f"{path}/children/{index}")

    for index, part in enumerate(model["parts"]):
        for layer_index, layer in enumerate(part["layers"]):
            scan(layer, f"/model/parts/{index}/layers/{layer_index}")
    for resource, icon in model["icons"].items():
        require(isinstance(icon, dict), "invalid icon")
        attr = integer(icon["attr"], "/model/icons/" + resource + "/attr")
        if attr not in (0, 1, 2):
            add("attr", "/model/icons/" + resource.replace("~", "~0").replace("/", "~1") + "/attr",
                resource, "attr", attr, "resource attr outside {0,1,2}")
    return {"asset": file_record(model_path), "counts": counts,
            "findings": findings, "supported_observations": notes,
            "runtime_trigger_certified": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("models", nargs="+")
    args = parser.parse_args()
    results = {"format_version": 2, "scope": "static_candidates_only", "tool": file_record(__file__),
               "runtime_source": file_record(ROOT / "src/runtime/geometry_scene.cpp"), "models": []}
    failed = False
    for path in args.models:
        try:
            result = audit_model(path)
            result["status"] = "SCANNED"
        except (OSError, ValueError, KeyError, TypeError) as error:
            result = {"path": str(path), "status": "INVALID_ASSET", "error": str(error)}
            failed = True
        results["models"].append(result)
    print(json.dumps(results, indent=2, ensure_ascii=False, allow_nan=False))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
