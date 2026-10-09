#!/usr/bin/env python3
"""Reproduce the FAIL subset of a frozen full catalog, preserving all evidence.

Other rows remain NOT_RERUN. Reproduced failures are still FAIL and produce exit 1;
matching a known error is a separate observation, never support certification.
"""
from __future__ import annotations
import argparse
from collections import Counter
import datetime
import json
from pathlib import Path
import shutil
import subprocess
import sys
import audit_support_gaps as audit

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/import_emote"))
import catalog_index

INPUT_JSON = {"steps": 0, "random_seed": 1, "reaction_seed": 1,
              "origin": [0, 400], "scale": 1.0}


def write_json(path, value):
    path = Path(path)
    temporary = Path(str(path) + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n")
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", default="build/release/bin/nekomote_session_probe")
    parser.add_argument("--assets", default="assets")
    parser.add_argument("--ledger", default="build/evidence/g13-touch-audit-2026-10-08/full-catalog/ledger.json")
    parser.add_argument("--output", required=True, help="new evidence directory")
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()
    probe, assets, ledger_path, output = (Path(v).resolve() for v in
                                         (args.probe, args.assets, args.ledger, args.output))
    try:
        baseline = json.loads(ledger_path.read_text())
        audit.require(args.timeout > 0, "timeout must be positive")
        audit.require(not output.exists(), "output already exists; choose a fresh directory")
        audit.require(baseline.get("complete") is True and baseline.get("scope") == "full_catalog",
                      "baseline is not a completed full catalog")
        rows = baseline["models"]
        names = [row["model"] for row in rows]
        audit.require(len(names) == len(set(names)) == baseline["catalog_total"], "catalog coverage invalid")
        for name in names:
            catalog_index.validate_model_name(name)
        catalog = baseline["catalog"]
        audit.require(audit.file_record(catalog["path"])["sha256"] == catalog["sha256"], "catalog hash changed")
        audit.require(set(json.loads(Path(catalog["path"]).read_text())) == set(names), "catalog name coverage mismatch")
        probe_record = audit.file_record(probe)
        audit.require(probe_record["sha256"] == baseline["probe"]["sha256"], "probe hash differs from frozen ledger")
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    output.mkdir(parents=True)
    shutil.copyfile(ledger_path, output / "baseline-ledger.json")
    result = {"task": "G17", "format_version": 2,
              "generated_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "head_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
              "argv": sys.argv, "timeout_seconds": args.timeout, "assets_root": str(assets),
              "baseline_ledger": audit.file_record(output / "baseline-ledger.json"),
              "baseline_source": audit.file_record(ledger_path), "catalog": catalog,
              "tools": [audit.file_record(__file__), audit.file_record(audit.__file__),
                        audit.file_record(catalog_index.__file__)],
              "runtime_source": audit.file_record(ROOT / "src/runtime/geometry_scene.cpp"),
              "importer_source": audit.file_record(ROOT / "src/asset_io/import_emote.cpp"),
              "probe": probe_record, "python": {"version": sys.version,
                                                  "binary": audit.file_record(sys.executable)},
              "catalog_total": len(rows), "complete": False, "runtime_trigger_certified": False,
              "models": []}
    for original in rows:
        name = original["model"]
        row = {"model": name, "baseline_status": original["status"], "status": "NOT_RERUN"}
        result["models"].append(row)
        if original["status"] != "FAIL":
            continue
        directory = output / name
        directory.mkdir()
        model = assets / "characters" / name / "model.nkm.json"
        session = model.with_name("session.nks.json")
        row.update(errors=[], assets=[], source_records=original.get("sources", []))
        try:
            frozen = {r["role"]: r for r in original["assets"] if r.get("role") in ("model_asset", "session_asset")}
            for role, path in (("model_asset", model), ("session_asset", session)):
                record = audit.file_record(path)
                record["role"] = role
                row["assets"].append(record)
                audit.require(record["sha256"] == frozen[role]["sha256"], "asset differs from frozen ledger: " + role)
            for record in row["source_records"]:
                audit.require(audit.file_record(record["path"])["sha256"] == record["sha256"],
                              "source differs from frozen ledger: " + record["path"])
            row["static_audit"] = audit.audit_model(model)
            write_json(directory / "static-audit.json", row["static_audit"])
            row["static_audit_record"] = audit.file_record(directory / "static-audit.json")
        except (OSError, ValueError, KeyError, TypeError) as error:
            row.update(status="INVALID_PROVENANCE_OR_ASSET", errors=[str(error)])
            continue
        request_path = directory / "request.json"
        write_json(request_path, INPUT_JSON)
        response_path = directory / "response.json"
        command = [str(probe), str(model), str(session), str(request_path), str(response_path)]
        process = {"argv": command, "returncode": None, "timeout": False, "error": None}
        try:
            p = subprocess.run(command, capture_output=True, timeout=args.timeout)
            process["returncode"] = p.returncode
            stdout, stderr = p.stdout, p.stderr
        except subprocess.TimeoutExpired as error:
            process["timeout"] = True
            stdout, stderr = error.stdout or b"", error.stderr or b""
        except OSError as error:
            process["error"] = str(error)
            stdout = stderr = b""
        for label, contents in (("stdout", stdout), ("stderr", stderr)):
            path = directory / (label + ".log")
            path.write_bytes(contents)
            process[label] = audit.file_record(path)
        process["input"] = audit.file_record(request_path)
        process["output"] = audit.file_record(response_path) if response_path.is_file() else None
        write_json(directory / "process.json", process)
        row["process"] = process
        row["status"] = "PROBE_RETURNED_SUCCESS_UNVERIFIED" if process["returncode"] == 0 else "FAIL"
        try:
            prior = json.loads(Path(original["conditions"][0]["evidence"]).read_text())
            row["baseline_process"] = audit.file_record(original["conditions"][0]["evidence"])
            row["expected_failure_reproduced"] = (process["returncode"] != 0 and
                not process["timeout"] and process["error"] is None and
                process["returncode"] == prior["returncode"] and
                process["stderr"]["sha256"] == prior["stderr"]["sha256"])
        except (OSError, ValueError, KeyError, TypeError) as error:
            row["expected_failure_reproduced"] = False
            row["errors"].append("baseline process evidence unavailable: " + str(error))
        write_json(output / "ledger.json", result)
    records = result["tools"] + [result["probe"], result["catalog"], result["runtime_source"],
                                 result["importer_source"], result["baseline_source"],
                                 result["python"]["binary"]]
    for row in result["models"]:
        records.extend(row.get("assets", []) + row.get("source_records", []))
    result["freeze_errors"] = [r["path"] for r in records if not Path(r["path"]).is_file() or
                                audit.file_record(r["path"])["sha256"] != r["sha256"]]
    result["complete"] = len(result["models"]) == len(rows)
    result["summary"] = {"total": len(rows), "status_counts": dict(Counter(r["status"] for r in result["models"])),
                         "expected_failures_reproduced": sum(r.get("expected_failure_reproduced", False) for r in result["models"]),
                         "findings": dict(Counter(f["field"] for r in result["models"] for f in r.get("static_audit", {}).get("findings", [])))}
    write_json(output / "ledger.json", result)
    print(json.dumps(result["summary"], ensure_ascii=False))
    # Nonzero for unresolved input/geometry failures, even if expected.
    return 1 if result["freeze_errors"] or any(row["status"] != "NOT_RERUN" for row in result["models"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())
