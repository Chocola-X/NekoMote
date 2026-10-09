#!/usr/bin/env python3
"""G24-1: 绑定 42 点 CPU 证据到冻结来源 manifest。

读取 g21-results-reviewed.json 的 42 点，逐点加载 result.json，
提取 request/response hash、probe argv、frozen_inputs，
绑定到 source-manifest.json 的模型 hash。
不重新运行 probe，只冻结和绑定现有证据。
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


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
    parser.add_argument("--reviewed", required=True, help="g21-results-reviewed.json 路径")
    parser.add_argument("--source-manifest", required=True, help="source-manifest.json 路径")
    parser.add_argument("--output", required=True, help="输出绑定 manifest 路径")
    args = parser.parse_args()

    reviewed = json.loads(pathlib.Path(args.reviewed).read_text())
    source_manifest = json.loads(pathlib.Path(args.source_manifest).read_text())
    output = pathlib.Path(args.output).resolve()
    if output.exists():
        raise SystemExit(f"output already exists: {output}")

    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip())

    binding = {
        "task": "G24-1",
        "purpose": "42 点 CPU 证据绑定到冻结来源 manifest",
        "head_commit": head,
        "dirty": dirty,
        "source_manifest": file_record(args.source_manifest, "source_manifest"),
        "reviewed_index": file_record(args.reviewed, "reviewed_index"),
        "generator": file_record(__file__, "generator_script"),
        "scope": reviewed["scope"],
        "expected_count": reviewed["count"],
        "points": [],
        "all_ok": True,
        "all_hashes_verified": True,
    }

    source_hashes = {}
    for model_name, model_data in source_manifest["models"].items():
        source_hashes[model_name] = {
            "model_asset": model_data["inputs"]["model_asset"]["sha256"],
            "session_asset": model_data["inputs"]["session_asset"]["sha256"],
            "source_json": model_data["inputs"]["source_json"]["sha256"],
        }

    for entry in reviewed["results"]:
        result_path = ROOT / entry["path"]
        result = json.loads(result_path.read_text())
        model = result["spec"]["model"]

        request_path = pathlib.Path(result["request"]["path"])
        response_path = pathlib.Path(result["response"]["path"])
        request_hash = sha256_file(request_path)
        response_hash = sha256_file(response_path)

        request_hash_match = request_hash == result["request"]["sha256"]
        response_hash_match = response_hash == result["response"]["sha256"]
        reviewed_hash_match = entry["sha256"] == sha256_file(result_path)

        model_hash_in_source = source_hashes.get(model, {}).get("model_asset")
        model_hash_in_point = None
        for frozen in result.get("frozen_inputs", []):
            if frozen.get("role") == "model_asset":
                model_hash_in_point = frozen["sha256"]
                break
        model_hash_match = model_hash_in_source == model_hash_in_point

        session_hash_in_source = source_hashes.get(model, {}).get("session_asset")
        session_hash_in_point = None
        for frozen in result.get("frozen_inputs", []):
            if frozen.get("role") == "session_asset":
                session_hash_in_point = frozen["sha256"]
                break
        session_hash_match = session_hash_in_source == session_hash_in_point

        point = {
            "tag": result_path.parent.name,
            "model": model,
            "condition": result["spec"]["condition"],
            "ok": entry["ok"] and result["ok"],
            "result_file": file_record(result_path, "result"),
            "request": {"path": str(request_path), "sha256": request_hash,
                        "bytes": request_path.stat().st_size,
                        "hash_matches_result": request_hash_match},
            "response": {"path": str(response_path), "sha256": response_hash,
                         "bytes": response_path.stat().st_size,
                         "hash_matches_result": response_hash_match},
            "reviewed_hash_matches_actual": reviewed_hash_match,
            "model_hash_matches_source": model_hash_match,
            "session_hash_matches_source": session_hash_match,
            "actual_zone": result.get("actual_zone"),
            "actual_phase": result.get("actual_phase"),
            "actual_reaction": result.get("actual_reaction"),
            "expected_zone": result["spec"].get("expected_zone"),
        }
        binding["points"].append(point)

        if not point["ok"]:
            binding["all_ok"] = False
        if not (request_hash_match and response_hash_match and reviewed_hash_match
                and model_hash_match and session_hash_match):
            binding["all_hashes_verified"] = False

    binding["actual_count"] = len(binding["points"])
    binding["all_bound"] = binding["all_ok"] and binding["all_hashes_verified"]

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(binding, indent=2, ensure_ascii=False, allow_nan=False) + "\n")
    print(f"wrote {output}")
    print(f"  points: {binding['actual_count']}/{binding['expected_count']}", file=sys.stderr)
    print(f"  all_ok: {binding['all_ok']}", file=sys.stderr)
    print(f"  all_hashes_verified: {binding['all_hashes_verified']}", file=sys.stderr)
    print(f"  all_bound: {binding['all_bound']}", file=sys.stderr)
    return 0 if binding["all_bound"] else 1


if __name__ == "__main__":
    sys.exit(main())
