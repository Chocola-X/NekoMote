#!/usr/bin/env python3
"""G24-1: 冻结触摸来源字段参考。

直接从原仓库读取 pixelMarker（source.json 的 metadata.charaProfile.pixelMarker）
和 presentation（config/model-presentation.json 的 cutoffY/touchMarkers），用与
import_session.py 相同的公式重新计算 source_regions，再从本仓库 session.nks.json
读取 session_regions 并逐字段比较。所有输入文件 hash、生成脚本 hash、公式参数
和逐字段比较结果写入 manifest，作为可重复的冻结来源入口。

不复制 session/G13 区域充当来源；不修改任何输入文件。
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import struct
import subprocess
import sys

ZONES = ('bust', 'eye', 'face', 'head', 'pant')
CONDITIONS = [
    {"name": "default_idle", "steps": 0, "origin": [0, 400], "scale": 1.0},
    {"name": "idle_120", "steps": 120, "origin": [0, 400], "scale": 1.0},
    {"name": "moved", "steps": 0, "origin": [80, 400], "scale": 1.0},
    {"name": "scaled_half", "steps": 0, "origin": [0, 400], "scale": 0.5},
]
PNG_WIDTH = 800
PNG_HEIGHT = 1080


def sha256_file(path):
    path = pathlib.Path(path)
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def file_record(path, role=None):
    path = pathlib.Path(path).resolve()
    record = {"path": str(path), "sha256": sha256_file(path), "bytes": path.stat().st_size}
    if role:
        record["role"] = role
    return record


def f32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def ellipse(x, y, rx, ry):
    return {"center": [x, y], "radius": [rx, ry]}


def compute_source_regions(markers, presentation):
    """与 import_session.py 完全相同的公式。"""
    regions = {zone: None for zone in ZONES}
    eye = markers.get('eye')
    mouth = markers.get('mouth')
    if not all(isinstance(v, (int, float)) and math.isfinite(v) for v in (eye, mouth)):
        return regions, ["missing finite eye/mouth markers"]
    warnings = []
    size = max(60, abs(mouth - eye))
    bust = markers.get('bust', mouth + size * 2.5)
    bottom = presentation.get('cutoffY', markers.get('bottom'))
    waist = bust + (bottom - bust) * .55 if bottom is not None else bust + size * 4
    regions.update(
        eye=ellipse(0, eye, size * .5, size * .35),
        face=ellipse(0, (eye + mouth) / 2, size * .9, size * .8),
        head=ellipse(0, eye - size * .9, size * 1.6, size * 1.7),
        bust=ellipse(0, bust, size * 1.3, size),
        pant=ellipse(0, waist, size * 2, size * 2.5),
    )
    for zone in ('head', 'bust', 'pant'):
        points = [markers.get(zone + suffix) for suffix in ('AX', 'AY', 'BX', 'BY')]
        if all(isinstance(v, (int, float)) and math.isfinite(v) for v in points):
            ax, ay, bx, by = points
            regions[zone] = ellipse(
                (ax + bx) / 2, (ay + by) / 2,
                max(30, abs(bx - ax) / 2), max(30, abs(by - ay) / 2),
            )
    return regions, warnings


def projected_regions(author, mirror, author_scale, cond):
    scale = f32(f32(cond["scale"]) * f32(author_scale))
    origin = [f32(v) for v in cond["origin"]]
    direction = -1 if mirror else 1
    result = {}
    for zone in ZONES:
        region = author[zone]
        if region is None:
            result[zone] = None
        else:
            c, r = region["center"], region["radius"]
            result[zone] = {
                "center": [f32(origin[0] + direction * scale * f32(c[0])),
                           f32(origin[1] + scale * f32(c[1]))],
                "radius": [f32(scale * f32(v)) for v in r],
            }
    return result


def png_center(projected_center):
    """条件投影中心 → PNG 像素坐标（Y 向下，原点为窗口中心）。"""
    return [PNG_WIDTH // 2 + int(round(projected_center[0])),
            int(round(projected_center[1] + PNG_HEIGHT / 2))]


def compare_regions(source, session):
    """逐字段比较 source_regions vs session_regions。"""
    comparisons = {}
    all_match = True
    for zone in ZONES:
        s = source[zone]
        t = session.get(zone)
        if s is None and t is None:
            comparisons[zone] = {"match": True, "source": None, "session": None}
        elif s is None or t is None:
            comparisons[zone] = {"match": False, "source": s, "session": t,
                                 "reason": "one side is None"}
            all_match = False
        else:
            zone_match = True
            diffs = {}
            for key in ("center", "radius"):
                for i in (0, 1):
                    sv = s[key][i]
                    tv = t[key][i]
                    if isinstance(sv, (int, float)) and isinstance(tv, (int, float)):
                        diff = abs(sv - tv)
                        if diff > 1e-6:
                            zone_match = False
                            diffs[f"{key}[{i}]"] = {"source": sv, "session": tv, "abs_diff": diff}
            comparisons[zone] = {"match": zone_match, "source": s, "session": t}
            if diffs:
                comparisons[zone]["diffs"] = diffs
                all_match = False
    return comparisons, all_match


def resolve_source_path(reference_root, model_name):
    catalog = json.loads((reference_root / "config/model-catalog.json").read_text())
    entry = catalog[model_name]
    character = entry["character"]
    costume = entry.get("costume", model_name)
    model_dir = reference_root / "model_conversion" / character / model_name
    if not model_dir.exists():
        model_dir = reference_root / "model_conversion" / character / costume
    source_path = model_dir / (model_name + ".json")
    if not source_path.exists():
        source_path = model_dir / (model_name + ".pure.json")
    return source_path, entry["config"]


def process_model(reference_root, assets_root, model_name):
    source_path, config_name = resolve_source_path(reference_root, model_name)
    source_json = json.loads(source_path.read_text())
    metadata = source_json["metadata"]
    pixel_marker = dict(metadata.get("charaProfile", {}).get("pixelMarker", {}))

    presentation_path = reference_root / "config/model-presentation.json"
    presentation_all = json.loads(presentation_path.read_text())
    presentation = presentation_all.get(model_name, {})
    touch_markers = presentation.get("touchMarkers", {})
    cutoff_y = presentation.get("cutoffY")
    combined_markers = dict(pixel_marker)
    combined_markers.update(touch_markers)

    source_regions, warnings = compute_source_regions(combined_markers, presentation)

    model_dir = assets_root / "characters" / model_name
    session_path = model_dir / "session.nks.json"
    model_path = model_dir / "model.nkm.json"
    session_def = json.loads(session_path.read_text())["session"]
    session_regions = session_def.get("interaction", {}).get("regions", {})

    model_asset = json.loads(model_path.read_text())["model"]
    mirror = model_asset["mirror"]
    scale_field = model_asset["author_scale"]
    author_scale = scale_field["value"] if scale_field["state"] == 2 else 1

    comparisons, all_match = compare_regions(source_regions, session_regions)

    conditions = {}
    for cond in CONDITIONS:
        projected = projected_regions(source_regions, mirror, author_scale, cond)
        png_centers = {}
        for zone in ZONES:
            if projected[zone] is not None:
                png_centers[zone] = png_center(projected[zone]["center"])
        conditions[cond["name"]] = {
            "params": cond,
            "projected_regions": projected,
            "png_centers": png_centers,
        }

    inputs = {
        "source_json": file_record(source_path, "source"),
        "model_presentation": file_record(presentation_path, "presentation"),
        "model_catalog": file_record(reference_root / "config/model-catalog.json", "catalog"),
        "session_asset": file_record(session_path, "session_asset"),
        "model_asset": file_record(model_path, "model_asset"),
    }
    texture_dir = model_dir / "textures"
    if texture_dir.is_dir():
        inputs["textures"] = [file_record(p, "texture_asset") for p in sorted(texture_dir.glob("*.png"))]

    return {
        "pixelMarker": pixel_marker,
        "touchMarkers": touch_markers,
        "cutoffY": cutoff_y,
        "mirror": mirror,
        "author_scale": author_scale,
        "source_regions": source_regions,
        "session_regions": session_regions,
        "region_comparison": comparisons,
        "regions_match": all_match,
        "conditions": conditions,
        "inputs": inputs,
        "warnings": warnings,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-root", required=True, help="原仓库根目录")
    parser.add_argument("--assets", required=True, help="本仓库 assets 目录")
    parser.add_argument("--output", required=True, help="输出 manifest 路径")
    parser.add_argument("--models", nargs="+", default=["fraise-france-maid-b", "cinnamon-maid-b", "coconut-santa-b"])
    args = parser.parse_args()

    reference_root = pathlib.Path(args.reference_root).resolve()
    assets_root = pathlib.Path(args.assets).resolve()
    output = pathlib.Path(args.output).resolve()

    if output.exists():
        raise SystemExit(f"output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)

    root = pathlib.Path(__file__).resolve().parents[2]
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip())

    manifest = {
        "task": "G24-1",
        "purpose": "冻结触摸来源字段参考：直接从原仓库 pixelMarker/presentation 重新计算，逐字段比较 session",
        "generator": file_record(__file__, "generator_script"),
        "head_commit": head,
        "dirty": dirty,
        "argv": sys.argv,
        "python_version": sys.version,
        "models": {},
        "all_regions_match": True,
    }

    for model_name in args.models:
        result = process_model(reference_root, assets_root, model_name)
        manifest["models"][model_name] = result
        if not result["regions_match"]:
            manifest["all_regions_match"] = False

    output.write_text(json.dumps(manifest, indent=2, ensure_ascii=False, allow_nan=False) + "\n")
    print(f"wrote {output}")
    for name, result in manifest["models"].items():
        status = "MATCH" if result["regions_match"] else "MISMATCH"
        print(f"  {name}: {status}", file=sys.stderr)
    return 0 if manifest["all_regions_match"] else 1


if __name__ == "__main__":
    sys.exit(main())
