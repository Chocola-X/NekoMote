#!/usr/bin/env python3
"""Build a touch visual ledger from explicit observations, never from CPU success.

Observation JSON version 3: {version: 3, observations: [{model,
status: MATCH|MISMATCH, observer, notes, conditions: [name], checked_zones: [zone],
evidence: [{role, path, sha256, condition}]}],
candidates: [{model, zone, current_author_region, suggested_author_region,
rationale, conditions: [default_idle, another_pose], measured_points: [...],
evidence: [...]}]}. Evidence paths are relative to this input JSON. Required roles
are model_asset, session_asset, source, clean, overlay, probe and reference.
Reference may be public WebGL region/point output or a source-field comparison.
Each clean/overlay/probe entry must name a declared condition and match that
condition's frozen G13 capture/probe output, with exactly one entry per role.
Candidates remain PENDING_EVIDENCE until real per-point request/response validation
is implemented; syntactically valid point lists do not certify actual execution.
Statuses describe the listed conditions/zones only; they do not certify all poses.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

ZONES = {"bust", "eye", "face", "head", "pant"}
ROLES = {"model_asset", "session_asset", "source", "clean", "overlay", "probe", "reference"}


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_evidence(items, base: Path) -> tuple[list[dict], list[str]]:
    checked, errors = [], []
    if not isinstance(items, list):
        return [], ["evidence must be a list"]
    for item in items:
        if not isinstance(item, dict):
            errors.append("invalid evidence entry")
            continue
        role, value = item.get("role"), item.get("path")
        expected = item.get("sha256")
        if not isinstance(value, str) or not value or not isinstance(role, str):
            errors.append("evidence role/path missing")
            continue
        path = (base / value).resolve()
        if not path.is_file():
            errors.append(f"missing evidence: {role}: {value}")
            continue
        actual = file_hash(path)
        if expected != actual:
            errors.append(f"hash mismatch: {role}: {value}")
            continue
        checked.append({"role": role, "path": str(path), "sha256": actual,
                        "condition": item.get("condition")})
    missing = ROLES - {item["role"] for item in checked}
    errors.extend(f"missing evidence role: {role}" for role in sorted(missing))
    return checked, errors


def conditions_valid(value) -> bool:
    return isinstance(value, list) and bool(value) and all(
        isinstance(item, str) and bool(item) for item in value
    ) and len(set(value)) == len(value)


def check_frozen_inputs(model: dict, checked: list[dict]) -> list[str]:
    """Bind per-model evidence to the G12 frozen source and imported assets."""
    errors = []
    for role, collection, basename in (("model_asset", "assets", "model.nkm.json"),
                                       ("session_asset", "assets", "session.nks.json"),
                                       ("source", "sources", None)):
        records = model.get(collection, [])
        expected = [item for item in records if isinstance(item, dict) and (
            item.get("role") == role or
            (basename is not None and Path(item.get("path", "")).name == basename)
        )]
        hashes = {item.get("sha256") for item in expected if item.get("sha256")}
        supplied = [item for item in checked if item["role"] == role]
        if not hashes:
            errors.append(f"frozen G12 input hash unavailable: {role}")
        elif len(supplied) != 1 or supplied[0]["sha256"] not in hashes:
            errors.append(f"evidence differs from frozen G12 model input: {role}")
    return errors


def check_frozen_conditions(model: dict, checked: list[dict], poses, base: Path) -> list[str]:
    """Bind every declared pose to its frozen screenshots and successful probe output."""
    if not conditions_valid(poses):
        return ["declared conditions missing/invalid"]
    frozen = model.get("conditions")
    if not isinstance(frozen, list) or any(not isinstance(c, dict) for c in frozen):
        return ["frozen G13 conditions unavailable"]
    names = [c.get("name") for c in frozen]
    if not conditions_valid(names):
        return ["frozen G13 condition names missing/duplicated"]
    by_name = {c["name"]: c for c in frozen}
    errors = []
    for item in checked:
        if item["role"] in {"clean", "overlay", "probe"} and item["condition"] not in poses:
            errors.append(f"evidence condition missing/undeclared: {item['role']}")
    for pose in poses:
        condition = by_name.get(pose)
        if condition is None:
            errors.append(f"unknown frozen G13 condition: {pose}")
            continue
        if condition.get("pass") is not True or condition.get("errors") != []:
            errors.append(f"frozen G13 condition unsuccessful: {pose}")
        for role, capture_name in (("clean", "plain"), ("overlay", "overlay"), ("probe", None)):
            supplied = [e for e in checked if e["role"] == role and e["condition"] == pose]
            if len(supplied) != 1:
                errors.append(f"condition requires exactly one {role} evidence: {pose}")
                continue
            expected = None
            if capture_name is not None:
                captures = condition.get("capture")
                capture = captures.get(capture_name) if isinstance(captures, dict) else None
                if isinstance(capture, dict) and capture.get("ok") is True and capture.get("returncode") == 0:
                    expected = capture.get("image")
            else:
                # G13 stores probe output provenance in the retained result manifest.
                manifest_path = condition.get("evidence")
                if isinstance(manifest_path, str) and manifest_path:
                    try:
                        manifest = json.loads((base / manifest_path).read_text())
                        if isinstance(manifest, dict) and manifest.get("ok") is True and manifest.get("returncode") == 0:
                            expected = manifest.get("output")
                    except (ValueError, OSError):
                        pass
            if not isinstance(expected, dict) or not isinstance(expected.get("path"), str) or not expected.get("sha256"):
                errors.append(f"frozen G13 {role} output unavailable: {pose}")
            elif supplied[0]["sha256"] != expected["sha256"] or supplied[0]["path"] != str((base / expected["path"]).resolve()):
                errors.append(f"evidence differs from frozen G13 condition output: {pose}: {role}")
    return errors


def region_valid(value) -> bool:
    if not isinstance(value, dict):
        return False
    for key in ("center", "radius"):
        pair = value.get(key)
        if not isinstance(pair, list) or len(pair) != 2 or not all(
            isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x)
            for x in pair
        ):
            return False
    return all(x > 0 for x in value["radius"])


def build_ledger(g12: dict, records: dict, base: Path) -> dict:
    if not isinstance(g12, dict) or not isinstance(records, dict):
        raise ValueError("G12 and observation inputs must be objects")
    models = g12.get("models")
    if not isinstance(models, list) or not models or any(not isinstance(m, dict) for m in models):
        raise ValueError("G12 model coverage is empty")
    names = [m.get("model") for m in models]
    if any(not isinstance(n, str) or not n for n in names) or len(set(names)) != len(names):
        raise ValueError("G12 model names missing/duplicated")
    observations = records.get("observations", [])
    candidates = records.get("candidates", [])
    if not isinstance(observations, list) or not isinstance(candidates, list):
        raise ValueError("observations/candidates must be lists")
    by_name = {}
    for item in observations:
        if not isinstance(item, dict) or item.get("model") not in names:
            raise ValueError("observation model not in coverage")
        if item["model"] in by_name:
            raise ValueError("duplicate observation model")
        by_name[item["model"]] = item
    result, by_model = [], {m["model"]: m for m in models}
    for model in models:
        name = model["model"]
        row = {"model": name, "input_status": model.get("status", "UNKNOWN"),
               "visual_alignment": "UNREVIEWED", "evidence": [], "errors": [],
               "conditions": [], "checked_zones": [], "observer": None}
        if model.get("status") != "OK":
            row["visual_alignment"] = "BLOCKED_INPUT"
        elif name in by_name:
            record = by_name[name]
            checked, errors = check_evidence(record.get("evidence"), base)
            errors.extend(check_frozen_inputs(model, checked))
            if records.get("version") != 3:
                errors.append("observation input version unsupported; rebuild version 3 evidence")
            errors.extend(check_frozen_conditions(model, checked, record.get("conditions"), base))
            zones = record.get("checked_zones")
            if record.get("status") not in {"MATCH", "MISMATCH"}:
                errors.append("invalid observation status")
            if not isinstance(record.get("observer"), str) or not record["observer"].strip():
                errors.append("observer missing")
            if not isinstance(record.get("notes"), str) or not record["notes"].strip():
                errors.append("observations/notes missing")
            if not conditions_valid(record.get("conditions")):
                errors.append("observation conditions missing/invalid")
            if not isinstance(zones, list) or not zones or any(z not in ZONES for z in zones):
                errors.append("checked zones missing/invalid")
            row.update(evidence=checked, errors=errors)
            if not errors:
                row.update(visual_alignment=record["status"], observer=record["observer"],
                           notes=record["notes"], conditions=record["conditions"], checked_zones=zones)
            else:
                row["visual_alignment"] = "PENDING_EVIDENCE"
        result.append(row)
    reviewed_candidates = []
    for candidate in candidates:
        if not isinstance(candidate, dict):
            raise ValueError("candidate must be an object")
        checked, errors = check_evidence(candidate.get("evidence"), base)
        if records.get("version") != 3:
            errors.append("observation input version unsupported; rebuild version 3 evidence")
        model = by_model.get(candidate.get("model"))
        zone = candidate.get("zone")
        if model is None or model.get("status") != "OK" or zone not in ZONES:
            errors.append("candidate model/zone unavailable")
        elif candidate.get("current_author_region") != model.get("author_regions", {}).get(zone):
            errors.append("candidate current value differs from frozen G12")
        if model is not None:
            errors.extend(check_frozen_inputs(model, checked))
            errors.extend(check_frozen_conditions(model, checked, candidate.get("conditions"), base))
        if not region_valid(candidate.get("current_author_region")) or not region_valid(
            candidate.get("suggested_author_region")
        ):
            errors.append("candidate region invalid")
        poses = candidate.get("conditions")
        if not conditions_valid(poses) or "default_idle" not in poses or len(poses) < 2:
            errors.append("candidate requires default and another observed pose")
        points = candidate.get("measured_points")
        if not isinstance(points, list) or not points:
            errors.append("candidate measured points missing")
        else:
            for point in points:
                if not isinstance(point, dict) or point.get("condition") not in (poses or []) or not all(
                    isinstance(point.get(key), list) and len(point[key]) == 2 and all(
                        isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x)
                        for x in point[key]
                    ) for key in ("author_point", "output_point")
                ) or point.get("expected") not in {"hit", "miss"}:
                    errors.append("candidate measured point invalid")
            if conditions_valid(poses) and not set(poses).issubset(
                {p.get("condition") for p in points if isinstance(p, dict)}
            ):
                errors.append("candidate points do not cover declared poses")
        if not isinstance(candidate.get("rationale"), str) or not candidate["rationale"].strip():
            errors.append("candidate rationale missing")
        errors.append("candidate point request/response validator unavailable; actual per-point execution is unverified")
        reviewed_candidates.append({**candidate, "evidence": checked, "errors": errors,
                                    "status": "PENDING_EVIDENCE"})
    statuses = {status: sum(r["visual_alignment"] == status for r in result)
                for status in ("MATCH", "MISMATCH", "UNREVIEWED", "PENDING_EVIDENCE", "BLOCKED_INPUT")}
    return {"task": "V9", "version": 3, "total_models": len(result), "summary": statuses,
            "scope": "Visual status covers only explicit observed conditions and zones; candidates are never applied.",
            "models": result, "candidates": reviewed_candidates}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--g12-ledger", type=Path, required=True)
    parser.add_argument("--observations", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output already exists; use a new evidence directory")
    try:
        records = json.loads(args.observations.read_text()) if args.observations else {}
        ledger = build_ledger(json.loads(args.g12_ledger.read_text()), records,
                              args.observations.resolve().parent if args.observations else Path.cwd())
        ledger["inputs"] = {"g12_ledger": str(args.g12_ledger.resolve()),
                            "g12_sha256": file_hash(args.g12_ledger),
                            "tool_sha256": file_hash(Path(__file__)),
                            "observations": str(args.observations.resolve()) if args.observations else None,
                            "observations_sha256": file_hash(args.observations) if args.observations else None}
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.error(str(error))
    args.output.mkdir(parents=True)
    (args.output / "v9_touch_ledger.json").write_text(json.dumps(ledger, indent=2) + "\n")
    print(json.dumps(ledger["summary"]))
    return 1 if any(r["visual_alignment"] != "MATCH" for r in ledger["models"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())
