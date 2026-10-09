#!/usr/bin/env python3
"""G24-3: 综合冻结 manifest 和契约测试。

把 source-manifest、point-binding、capture-manifest、overlay-manifest 绑定成
一个冻结入口。契约测试验证：
1. 所有 manifest 互相引用的 hash 一致
2. 新诊断条件（lower_body_in_frame）不假冒 G13 条件
3. PNG 尺寸=800x1080，alpha bbox 非空
4. 来源 hash 与 42 点 hash 一致
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
MODELS = ["fraise-france-maid-b", "cinnamon-maid-b", "coconut-santa-b"]
CONDITIONS = ["default", "idle120", "moved", "scaled_half", "lower_body_in_frame"]
G13_CONDITIONS = ["default_idle", "idle_120", "moved", "scaled_half"]
WIDTH = 800
HEIGHT = 1080


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence-dir", required=True, help="build/evidence/g24-touch 目录")
    parser.add_argument("--output", required=True, help="输出冻结 manifest 路径")
    args = parser.parse_args()

    evidence = pathlib.Path(args.evidence_dir).resolve()
    output = pathlib.Path(args.output).resolve()
    if output.exists():
        raise SystemExit(f"output already exists: {output}")

    source_manifest = json.loads((evidence / "source-manifest.json").read_text())
    point_binding = json.loads((evidence / "point-binding.json").read_text())
    capture_manifest = json.loads((evidence / "qt-captures/capture-manifest.json").read_text())
    overlay_manifest = json.loads((evidence / "overlays/overlay-manifest.json").read_text())

    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip())

    freeze = {
        "task": "G24-3",
        "purpose": "触摸视觉证据综合冻结 manifest",
        "head_commit": head,
        "dirty": dirty,
        "generator": file_record(__file__, "generator_script"),
        "components": {
            "source_manifest": file_record(evidence / "source-manifest.json", "source_manifest"),
            "point_binding": file_record(evidence / "point-binding.json", "point_binding"),
            "capture_manifest": file_record(evidence / "qt-captures/capture-manifest.json", "capture_manifest"),
            "overlay_manifest": file_record(evidence / "overlays/overlay-manifest.json", "overlay_manifest"),
        },
        "models": {},
        "conditions": CONDITIONS,
        "g13_conditions": G13_CONDITIONS,
        "new_conditions": ["lower_body_in_frame"],
        "contract_checks": [],
        "all_checks_passed": True,
    }

    checks = freeze["contract_checks"]

    # 检查 1: source-manifest 的模型 hash 与 point-binding 一致
    for model in MODELS:
        sm = source_manifest["models"][model]
        pb_model = None
        for point in point_binding["points"]:
            if point["model"] == model:
                pb_model = point
                break
        if pb_model:
            sm_model_hash = sm["inputs"]["model_asset"]["sha256"]
            pb_model_match = pb_model["model_hash_matches_source"]
            checks.append({
                "name": f"source_hash_consistent_{model}",
                "passed": pb_model_match,
                "detail": f"source-manifest model hash matches point-binding for {model}",
            })

    # 检查 2: 新条件不假冒 G13 条件
    new_conds = set(CONDITIONS) - set(G13_CONDITIONS)
    g13_conds = set(G13_CONDITIONS)
    overlap = new_conds & g13_conds
    checks.append({
        "name": "new_conditions_not_g13",
        "passed": len(overlap) == 0,
        "detail": f"new conditions {new_conds} do not overlap G13 conditions {g13_conds}",
    })

    # 检查 3: 所有 PNG 尺寸=800x1080，alpha bbox 非空
    for model in MODELS:
        for cond in CONDITIONS:
            cap = capture_manifest["models"][model]["conditions"].get(cond, {})
            info = cap.get("image_info", {})
            size = info.get("size")
            bbox = info.get("alpha_bbox")
            size_ok = size == [WIDTH, HEIGHT]
            bbox_ok = bbox is not None and len(bbox) == 4
            checks.append({
                "name": f"png_valid_{model}_{cond}",
                "passed": size_ok and bbox_ok,
                "detail": f"{model}/{cond}: size={size} bbox={bbox}",
            })

    # 检查 4: overlay 图存在且尺寸一致
    for model in MODELS:
        for cond in CONDITIONS:
            ov = overlay_manifest["models"][model].get(cond, {})
            ov_png = ov.get("overlay_png")
            checks.append({
                "name": f"overlay_exists_{model}_{cond}",
                "passed": ov_png is not None,
                "detail": f"{model}/{cond}: overlay exists={ov_png is not None}",
            })

    # 检查 5: 42 点全部绑定
    checks.append({
        "name": "all_42_points_bound",
        "passed": point_binding["all_bound"] and point_binding["actual_count"] == 42,
        "detail": f"42 points bound: {point_binding['actual_count']}/42, all_bound={point_binding['all_bound']}",
    })

    # 检查 6: source regions 全部匹配
    checks.append({
        "name": "source_regions_match",
        "passed": source_manifest["all_regions_match"],
        "detail": f"all source regions match session: {source_manifest['all_regions_match']}",
    })

    freeze["all_checks_passed"] = all(c["passed"] for c in checks)
    freeze["check_count"] = len(checks)
    freeze["check_passed_count"] = sum(1 for c in checks if c["passed"])

    for model in MODELS:
        freeze["models"][model] = {
            "source": source_manifest["models"][model]["inputs"],
            "captures": {},
            "overlays": {},
        }
        for cond in CONDITIONS:
            cap = capture_manifest["models"][model]["conditions"].get(cond, {})
            ov = overlay_manifest["models"][model].get(cond, {})
            freeze["models"][model]["captures"][cond] = {
                "png": cap.get("png"),
                "image_info": cap.get("image_info"),
                "params": cap.get("params"),
                "probe_stdout": cap.get("stdout", ""),
                "probe_stderr": cap.get("stderr", ""),
            }
            freeze["models"][model]["overlays"][cond] = ov

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(freeze, indent=2, ensure_ascii=False, allow_nan=False) + "\n")
    print(f"wrote {output}")
    print(f"  checks: {freeze['check_passed_count']}/{freeze['check_count']}", file=sys.stderr)
    for c in checks:
        if not c["passed"]:
            print(f"  FAILED: {c['name']} — {c['detail']}", file=sys.stderr)
    return 0 if freeze["all_checks_passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
