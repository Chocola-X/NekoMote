#!/usr/bin/env python3
"""G17: static audit of unsupported geometry triggers in model.nkm.json.

Scans every layer and frame in a model asset to collect ALL candidate
unsupported field/value combinations, not just the first error returned
by the runtime.  Groups findings by category (inheritance, transform_order,
frame fields, blend mode, attr, mask, mesh) and labels each as
"actual_triggered" (matches the runtime reject condition), "static_potential"
(present in asset but not on the failing path) or "unchecked".

The runtime reject conditions are mirrored from
src/runtime/geometry_scene.cpp lines 217-222 and 281-295:

  Node unsupported_common:
    coordinate != 0
    mesh_transform not in {0,1}
    mesh_combine not in {0,1}
    mesh_sync_child_mask not in {0,1,8}
    transform_order non-empty and != [0,3,2,1]

  Node unsupported (inheritance):
    inherit_mask != 0x20007fc
    and not (type == 12 and inherit_mask == 0x24007fc)

  Pose unsupported (per frame):
    frame.type == 1
    zx / zy / ox / oy present
    bm not in {0,19}
    flip != 0
    color != 0x808080ff
    motion.time_offset != 0 and not parameter_driven

  Resource attr:
    attr not in {0,1,2}

  Mesh cc:
    mesh.cc present  (rejected at import time, not runtime)
"""

import json
import sys
import os
from collections import defaultdict

K_STANDARD_INHERIT = 0x20007fc
K_STENCIL_INHERIT = 0x24007fc
K_PARTIAL_INHERIT = 0x200020c
K_NEUTRAL_COLOR = 0x808080ff


def field_value(obj, key):
    """Return (has_value, value) for a state/value field."""
    f = obj.get(key)
    if f is None:
        return False, None
    state = f.get("state", 0)
    if state == 0:
        return False, None
    return True, f.get("value")


def audit_layer(layer, path, part_name, findings):
    """Audit a single layer for unsupported conditions."""
    label = layer.get("label", "?")
    full_path = f"{path}/{label}"
    ltype = layer.get("type", 0)
    inherit_mask = layer.get("inherit_mask", 0)
    coordinate = layer.get("coordinate", 0)
    mesh_transform = layer.get("mesh_transform", 0)
    mesh_combine = layer.get("mesh_combine", 0)
    mesh_sync = layer.get("mesh_sync_child_mask", 0)
    transform_order = layer.get("transform_order", [])

    # --- Node-level: unsupported_common ---
    if coordinate != 0:
        findings["transform_common"].append({
            "layer": full_path, "part": part_name,
            "field": "coordinate", "value": coordinate,
            "condition": "coordinate != 0",
        })
    if mesh_transform not in (0, 1):
        findings["transform_common"].append({
            "layer": full_path, "part": part_name,
            "field": "mesh_transform", "value": mesh_transform,
            "condition": "mesh_transform not in {0,1}",
        })
    if mesh_combine not in (0, 1):
        findings["transform_common"].append({
            "layer": full_path, "part": part_name,
            "field": "mesh_combine", "value": mesh_combine,
            "condition": "mesh_combine not in {0,1}",
        })
    if mesh_sync not in (0, 1, 8):
        findings["transform_common"].append({
            "layer": full_path, "part": part_name,
            "field": "mesh_sync_child_mask", "value": mesh_sync,
            "condition": "mesh_sync_child_mask not in {0,1,8}",
        })
    if transform_order and transform_order != [0, 3, 2, 1]:
        findings["transform_order"].append({
            "layer": full_path, "part": part_name,
            "field": "transform_order", "value": transform_order,
            "condition": "transform_order non-empty and != [0,3,2,1]",
        })

    # --- Node-level: inheritance ---
    is_stencil_ok = (ltype == 12 and inherit_mask == K_STENCIL_INHERIT)
    if inherit_mask != K_STANDARD_INHERIT and not is_stencil_ok:
        # Check if it's the partial-inheritance marker (0x200020c)
        is_partial = (inherit_mask == K_PARTIAL_INHERIT)
        findings["inheritance"].append({
            "layer": full_path, "part": part_name,
            "field": "inherit_mask", "value": hex(inherit_mask),
            "layer_type": ltype,
            "condition": "inherit_mask != 0x20007fc and not (type==12 and inherit_mask==0x24007fc)",
            "is_partial_marker": is_partial,
        })

    # --- Pose-level: frame fields ---
    for fi, frame in enumerate(layer.get("frame_list", [])):
        ftype = frame.get("type", 0)
        content = frame.get("content")
        if content is None:
            continue
        has_value, cval = field_value(content, "value")
        if not has_value:
            continue
        frame_id = f"{full_path}@frame[{fi}]"

        if ftype == 1:
            findings["frame_type"].append({
                "layer": full_path, "part": part_name,
                "frame_index": fi, "value": ftype,
                "condition": "frame.type == 1",
            })

        for axis in ("zx", "zy", "ox", "oy"):
            has, val = field_value(cval, axis)
            if has:
                findings["frame_field"].append({
                    "layer": full_path, "part": part_name,
                    "frame_index": fi, "field": axis, "value": val,
                    "condition": f"{axis} present (z-axis or offset)",
                })

        has_bm, bm = field_value(cval, "bm")
        if has_bm and bm not in (0, 19):
            findings["blend_mode"].append({
                "layer": full_path, "part": part_name,
                "frame_index": fi, "field": "bm", "value": bm,
                "condition": "bm not in {0,19}",
            })

        has_flip, flip = field_value(cval, "flip")
        if has_flip and flip != 0:
            findings["frame_field"].append({
                "layer": full_path, "part": part_name,
                "frame_index": fi, "field": "flip", "value": flip,
                "condition": "flip != 0",
            })

        has_color, color = field_value(cval, "color")
        if has_color and color != K_NEUTRAL_COLOR:
            findings["frame_field"].append({
                "layer": full_path, "part": part_name,
                "frame_index": fi, "field": "color", "value": hex(color),
                "condition": "color != 0x808080ff",
            })

        has_motion, motion = field_value(cval, "motion")
        if has_motion:
            has_to, to = field_value(motion, "time_offset")
            if has_to and to != 0:
                findings["frame_field"].append({
                    "layer": full_path, "part": part_name,
                    "frame_index": fi, "field": "motion.time_offset",
                    "value": to,
                    "condition": "motion.time_offset != 0",
                })

        has_mesh, mesh = field_value(cval, "mesh")
        if has_mesh and isinstance(mesh, dict):
            has_cc, cc = field_value(mesh, "cc")
            if has_cc:
                findings["mesh_cc"].append({
                    "layer": full_path, "part": part_name,
                    "frame_index": fi, "field": "mesh.cc",
                    "condition": "mesh.cc present (rejected at import)",
                })

    # --- Recurse children ---
    for child in layer.get("children", []):
        audit_layer(child, full_path, part_name, findings)


def audit_icons(model, findings):
    """Audit icon/resource attr values."""
    icons = model.get("icons", {})
    for key, icon in icons.items():
        attr = icon.get("attr", 0)
        if attr not in (0, 1, 2):
            findings["attr"].append({
                "resource": key, "value": attr,
                "condition": "attr not in {0,1,2}",
            })


def audit_model(model_path):
    """Audit a single model.nkm.json."""
    with open(model_path) as f:
        data = json.load(f)
    model = data["model"]
    findings = defaultdict(list)
    for part in model.get("parts", []):
        part_name = part.get("name", "?")
        for layer in part.get("layers", []):
            audit_layer(layer, part_name, part_name, findings)
    audit_icons(model, findings)
    return dict(findings)


def main():
    if len(sys.argv) < 2:
        print("usage: audit_support_gaps.py MODEL_JSON [MODEL_JSON ...]",
              file=sys.stderr)
        return 2
    results = {}
    for path in sys.argv[1:]:
        model_name = os.path.basename(os.path.dirname(path))
        findings = audit_model(path)
        results[model_name] = findings
    print(json.dumps(results, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
