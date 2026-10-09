#!/usr/bin/env python3
"""G24-2: 在 clean 图上叠加触摸区域椭圆生成 overlay 图。

触摸区域数据来自 source-manifest.json 的 projected_regions。
overlay 图只用于视觉参考，不自动生成观察结论。
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
ZONES = ("bust", "eye", "face", "head", "pant")
ZONE_COLORS = {
    "bust": (255, 0, 0, 180),
    "eye": (255, 255, 0, 180),
    "face": (0, 255, 0, 180),
    "head": (0, 255, 255, 180),
    "pant": (255, 0, 255, 180),
}
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


def png_center(projected_center):
    return [PNG_WIDTH // 2 + int(round(projected_center[0])),
            int(round(projected_center[1] + PNG_HEIGHT / 2))]


def draw_ellipse(draw, center, radius, color, label):
    from PIL import ImageDraw
    cx, cy = center
    rx, ry = radius
    bbox = [cx - rx, cy - ry, cx + rx, cy + ry]
    draw.ellipse(bbox, outline=color, width=2)
    draw.text((cx - 10, cy - 5), label, fill=color[:3])


CONDITION_MAP = {
    "default": "default_idle",
    "idle120": "idle_120",
    "moved": "moved",
    "scaled_half": "scaled_half",
    "lower_body_in_frame": "lower_body_in_frame",
}
LOWER_BODY_CONDITION = {"name": "lower_body_in_frame", "steps": 0, "origin": [0, -200], "scale": 1.0}


def projected_regions(author, mirror, author_scale, cond):
    import struct
    def f32(value):
        return struct.unpack("f", struct.pack("f", value))[0]
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-manifest", required=True)
    parser.add_argument("--captures", required=True, help="qt-captures 目录")
    parser.add_argument("--output", required=True, help="输出 overlay 目录")
    args = parser.parse_args()

    source = json.loads(pathlib.Path(args.source_manifest).read_text())
    captures = json.loads(pathlib.Path(pathlib.Path(args.captures) / "capture-manifest.json").read_text())
    output = pathlib.Path(args.output).resolve()
    if output.exists():
        raise SystemExit(f"output already exists: {output}")
    output.mkdir(parents=True)

    from PIL import Image, ImageDraw

    manifest = {
        "task": "G24-2-overlay",
        "purpose": "在 clean 图上叠加触摸区域椭圆生成 overlay 图",
        "source_manifest": file_record(args.source_manifest),
        "captures_manifest": file_record(pathlib.Path(args.captures) / "capture-manifest.json"),
        "generator": file_record(__file__, "generator_script"),
        "models": {},
        "all_ok": True,
    }

    for model_name, model_data in source["models"].items():
        model_output = output / model_name
        model_output.mkdir()
        manifest["models"][model_name] = {}

        for cond_name, cond_data in captures["models"][model_name]["conditions"].items():
            clean_path = pathlib.Path(args.captures) / model_name / cond_name / "package.png"
            if not clean_path.is_file():
                continue

            source_cond_name = CONDITION_MAP.get(cond_name, cond_name)
            cond_data_source = model_data["conditions"].get(source_cond_name)
            if cond_data_source is None and cond_name == "lower_body_in_frame":
                cond_data_source = {
                    "projected_regions": projected_regions(
                        model_data["source_regions"],
                        model_data["mirror"],
                        model_data["author_scale"],
                        LOWER_BODY_CONDITION,
                    )
                }
            if cond_data_source is None:
                continue

            overlay_path = model_output / f"{cond_name}_overlay.png"
            with Image.open(clean_path) as img:
                img = img.convert("RGBA")
                overlay = img.copy()
                draw = ImageDraw.Draw(overlay)

                for zone in ZONES:
                    projected = cond_data_source["projected_regions"].get(zone)
                    if projected is None:
                        continue
                    center = png_center(projected["center"])
                    radius = [int(round(r)) for r in projected["radius"]]
                    color = ZONE_COLORS[zone]
                    draw_ellipse(draw, center, radius, color, zone)

                overlay.save(str(overlay_path), "PNG")

            manifest["models"][model_name][cond_name] = {
                "clean_png": file_record(clean_path, "clean_png"),
                "overlay_png": file_record(overlay_path, "overlay_png"),
            }
            print(f"  {model_name}/{cond_name}: overlay saved", file=sys.stderr)

    manifest_path = output / "overlay-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False, allow_nan=False) + "\n")
    print(f"wrote {manifest_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
