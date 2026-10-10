#!/usr/bin/env python3
"""G28: Bind touch point CPU evidence from G12 ledger to frozen source manifest.

Extracts every touch probe request/response hash from a G12 ledger produced by
run_touch_audit.py, binds model/session hashes to source-manifest.json, and
writes point-binding.json.  Replaces the legacy bind_point_evidence.py which
required the now-unavailable g21-results-reviewed.json.

Does not re-run any probe; only freezes and binds existing evidence.
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


def extract_points(ledger):
    """Walk G12 ledger conditions/zone_tests and collect touch probe evidence."""
    points = []
    for model in ledger.get("models", []):
        model_name = model["model"]
        for cond in model.get("conditions", []):
            cond_name = cond["name"]
            for zone_name, zone_test in cond.get("zone_tests", {}).items():
                if zone_test.get("skipped"):
                    continue
                for tag in ("center_test", "inside_test"):
                    check = zone_test.get(tag)
                    if not isinstance(check, dict):
                        continue
                    points.append(_point_from_check(check, model_name, cond_name,
                                                    zone_name, tag))
            outside = cond.get("outside_test")
            if isinstance(outside, dict):
                points.append(_point_from_check(outside, model_name, cond_name,
                                                "outside", "outside"))
    return points


def _point_from_check(check, model_name, cond_name, zone_name, tag):
    evidence_path = check.get("evidence", "")
    result = {}
    if evidence_path and pathlib.Path(evidence_path).is_file():
        result = json.loads(pathlib.Path(evidence_path).read_text())
    point = {
        "model": model_name,
        "condition": cond_name,
        "zone": zone_name,
        "tag": tag,
        "ok": check.get("pass", False),
        "expected_zone": check.get("expected_zone"),
        "actual_reaction": check.get("reaction"),
        "actual_phase": check.get("phase"),
        "evidence_path": evidence_path,
    }
    if result:
        point["probe_ok"] = result.get("ok", False)
        point["probe_returncode"] = result.get("returncode")
        inp = result.get("input")
        out = result.get("output")
        if isinstance(inp, dict):
            point["request"] = {"path": inp.get("path"), "sha256": inp.get("sha256"),
                                "bytes": inp.get("bytes")}
        if isinstance(out, dict):
            point["response"] = {"path": out.get("path"), "sha256": out.get("sha256"),
                                 "bytes": out.get("bytes")}
    return point


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ledger", required=True, help="G12 ledger.json 路径")
    parser.add_argument("--source-manifest", required=True, help="source-manifest.json 路径")
    parser.add_argument("--output", required=True, help="输出绑定 manifest 路径")
    args = parser.parse_args()

    ledger = json.loads(pathlib.Path(args.ledger).read_text())
    source_manifest = json.loads(pathlib.Path(args.source_manifest).read_text())
    output = pathlib.Path(args.output).resolve()
    if output.exists():
        raise SystemExit(f"output already exists: {output}")

    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip())

    source_hashes = {}
    for model_name, model_data in source_manifest["models"].items():
        source_hashes[model_name] = {
            "model_asset": model_data["inputs"]["model_asset"]["sha256"],
            "session_asset": model_data["inputs"]["session_asset"]["sha256"],
            "source_json": model_data["inputs"]["source_json"]["sha256"],
        }

    points = extract_points(ledger)
    all_ok = True
    all_hashes_verified = True

    for point in points:
        model = point["model"]
        src_hashes = source_hashes.get(model, {})

        ledger_model = next((m for m in ledger["models"] if m["model"] == model), None)
        if ledger_model is None:
            all_ok = False
            continue

        ledger_model_hash = None
        ledger_session_hash = None
        for asset in ledger_model.get("assets", []):
            if asset.get("role") == "model_asset":
                ledger_model_hash = asset["sha256"]
            elif asset.get("role") == "session_asset":
                ledger_session_hash = asset["sha256"]

        point["model_hash_matches_source"] = ledger_model_hash == src_hashes.get("model_asset")
        point["session_hash_matches_source"] = ledger_session_hash == src_hashes.get("session_asset")

        if not point["ok"]:
            all_ok = False
        if not (point["model_hash_matches_source"] and point["session_hash_matches_source"]):
            all_hashes_verified = False
        if "request" in point and "sha256" in point["request"]:
            req_path = pathlib.Path(point["request"]["path"])
            if req_path.is_file():
                actual = sha256_file(req_path)
                point["request"]["hash_matches_actual"] = actual == point["request"]["sha256"]
                if not point["request"]["hash_matches_actual"]:
                    all_hashes_verified = False
            else:
                point["request"]["hash_matches_actual"] = False
                all_hashes_verified = False
        if "response" in point and "sha256" in point["response"]:
            resp_path = pathlib.Path(point["response"]["path"])
            if resp_path.is_file():
                actual = sha256_file(resp_path)
                point["response"]["hash_matches_actual"] = actual == point["response"]["sha256"]
                if not point["response"]["hash_matches_actual"]:
                    all_hashes_verified = False
            else:
                point["response"]["hash_matches_actual"] = False
                all_hashes_verified = False

    binding = {
        "task": "G28-point-binding",
        "purpose": "触摸点 CPU 证据绑定到冻结来源 manifest（从 G12 ledger 提取）",
        "head_commit": head,
        "dirty": dirty,
        "source_manifest": file_record(args.source_manifest, "source_manifest"),
        "ledger": file_record(args.ledger, "g12_ledger"),
        "generator": file_record(__file__, "generator_script"),
        "points": points,
        "actual_count": len(points),
        "all_ok": all_ok,
        "all_hashes_verified": all_hashes_verified,
        "all_bound": all_ok and all_hashes_verified,
    }

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(binding, indent=2, ensure_ascii=False, allow_nan=False) + "\n")
    print(f"wrote {output}")
    print(f"  points: {binding['actual_count']}", file=sys.stderr)
    print(f"  all_ok: {binding['all_ok']}", file=sys.stderr)
    print(f"  all_hashes_verified: {binding['all_hashes_verified']}", file=sys.stderr)
    print(f"  all_bound: {binding['all_bound']}", file=sys.stderr)
    return 0 if binding["all_bound"] else 1


if __name__ == "__main__":
    sys.exit(main())
