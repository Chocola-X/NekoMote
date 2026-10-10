#!/usr/bin/env python3
"""Deterministic touch checks; geometry, screenshots and visual acceptance are separate.

All points use centered output pixels, Y down. Retain each request, response and
process result. Existing output directories are rejected so stale evidence cannot pass.
"""
from __future__ import annotations

import argparse
import datetime
import fcntl
import hashlib
import json
import math
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/import_emote"))
import catalog_index

ZONE_ORDER = ["bust", "eye", "face", "head", "pant"]
ZONE_COLORS = dict(zip(ZONE_ORDER, ["red", "yellow", "green", "cyan", "purple"]))
CONDITIONS = [
    {"name": "default_idle", "steps": 0, "origin": [0, 400], "scale": 1.0},
    {"name": "idle_120", "steps": 120, "origin": [0, 400], "scale": 1.0},
    {"name": "moved", "steps": 0, "origin": [80, 400], "scale": 1.0},
    {"name": "scaled_half", "steps": 0, "origin": [0, 400], "scale": 0.5},
]
RANDOM_SEED = REACTION_SEED = 1
OUTSIDE_POINT = [99999.0, 99999.0]


def sha256_file(path):
    path = Path(path)
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def file_record(path):
    path = Path(path).resolve()
    return {"path": str(path), "sha256": sha256_file(path), "bytes": path.stat().st_size}


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n")


def finite_pair(value, positive=False):
    return (isinstance(value, list) and len(value) == 2 and
            all(isinstance(v, (int, float)) and not isinstance(v, bool) and
                math.isfinite(v) and (not positive or v > 0) for v in value))


def f32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def zone_reactions(session):
    result = {z: [] for z in ZONE_ORDER}
    for reaction in session["reactions"]:
        label = reaction["label"]
        if not isinstance(label, str) or not label:
            raise ValueError("invalid reaction label")
        for zone in reaction["zones"]:
            if zone not in result:
                raise ValueError("invalid reaction zone")
            result[zone].append(label)
    return result


def author_regions(session):
    result = {z: session.get("interaction", {}).get("regions", {}).get(z) for z in ZONE_ORDER}
    for region in result.values():
        if region is not None and (not isinstance(region, dict) or
                                  not finite_pair(region.get("center")) or
                                  not finite_pair(region.get("radius"), True)):
            raise ValueError("invalid author ellipse")
    return result


def projected_regions(author, reactions, mirror, author_scale, cond):
    scale = f32(f32(cond["scale"]) * f32(author_scale))
    origin = [f32(v) for v in cond["origin"]]
    direction = -1 if mirror else 1
    result = {}
    for zone in ZONE_ORDER:
        region = author[zone]
        if region is None or not reactions[zone]:
            result[zone] = None
        else:
            c, r = region["center"], region["radius"]
            result[zone] = {"center": [f32(origin[0] + direction * scale * f32(c[0])),
                                        f32(origin[1] + scale * f32(c[1]))],
                            "radius": [f32(scale * f32(v)) for v in r]}
    return result


def expected_zone(point, regions):
    """First eligible ellipse containing the point, independent of reaction labels."""
    for zone in ZONE_ORDER:
        r = regions[zone]
        if r is not None and sum(((point[i] - r["center"][i]) / r["radius"][i]) ** 2
                                 for i in (0, 1)) <= 1:
            return zone
    return None


def inside_point(center, radius):
    return [center[i] + radius[i] * 0.3 for i in (0, 1)]


def run_process(cmd, stem, timeout=60):
    result = {"argv": [str(v) for v in cmd], "returncode": None, "timeout": False,
              "error": None, "ok": False}
    try:
        p = subprocess.run(result["argv"], capture_output=True, timeout=timeout)
        result["returncode"] = p.returncode
        stdout, stderr = p.stdout, p.stderr
        result["ok"] = p.returncode == 0
    except subprocess.TimeoutExpired as error:
        result["timeout"] = True
        stdout, stderr = error.stdout or b"", error.stderr or b""
    except OSError as error:
        result["error"] = str(error)
        stdout = stderr = b""
    for name, contents in (("stdout", stdout), ("stderr", stderr)):
        path = Path(str(stem) + f".{name}.log")
        path.write_bytes(contents)
        result[name] = file_record(path)
    write_json(Path(str(stem) + ".command.json"), result)
    return result


def validate_probe(data, steps):
    if not isinstance(data, dict) or type(data.get("presentation_mirror")) is not bool:
        raise ValueError("missing presentation_mirror")
    samples = data.get("samples")
    if not isinstance(samples, list) or len(samples) != steps + 1:
        raise ValueError("incomplete sample sequence")
    for step, sample in enumerate(samples):
        if (not isinstance(sample, dict) or type(sample.get("step")) is not int or
                sample["step"] != step or not isinstance(sample.get("reaction"), str) or
                type(sample.get("phase")) is not int or sample["phase"] not in (0, 1, 2)):
            raise ValueError("invalid sample sequence/reaction/phase")
    regions = data.get("touch_regions")
    if not isinstance(regions, dict) or set(regions) != set(ZONE_ORDER):
        raise ValueError("incomplete projected region set")
    for r in regions.values():
        if r is not None and (not isinstance(r, dict) or not finite_pair(r.get("center")) or
                              not finite_pair(r.get("radius"), True)):
            raise ValueError("invalid projected ellipse")


def run_probe(probe, model, session, request, output):
    input_file = output.with_suffix(".input.json")
    if output.exists() or input_file.exists():
        raise ValueError("probe evidence output already exists")
    write_json(input_file, request)
    result = run_process([probe, model, session, input_file, output], output.with_suffix(""))
    result["input"] = file_record(input_file)
    result["data"] = None
    if result["ok"]:
        try:
            data = json.loads(output.read_text())
            validate_probe(data, request["steps"])
            result["data"] = data
        except (OSError, ValueError, KeyError, TypeError) as error:
            result["ok"] = False
            result["error"] = str(error)
    if output.exists():
        result["output"] = file_record(output)
    write_json(output.with_suffix(".result.json"), {k: v for k, v in result.items() if k != "data"})
    return result


def request_for(cond):
    return {"steps": cond["steps"], "random_seed": RANDOM_SEED,
            "reaction_seed": REACTION_SEED, "origin": cond["origin"], "scale": cond["scale"]}


def touch_check(probe, model, session, cond, point, regions, reactions, directory, tag):
    request = request_for(cond)
    # Probe dispatches event N before advance N: N=idle_steps+1 touches after
    # precisely idle_steps updates. Step 0 touches the initial pose directly.
    event_step = cond["steps"] + 1 if cond["steps"] else 0
    request["steps"] = event_step
    request["events"] = [{"step": event_step, "kind": "touch", "point": point}]
    result = run_probe(probe, model, session, request, directory / f"touch_{tag}.json")
    zone = expected_zone(point, regions)
    check = {"point": point, "expected_zone": zone, "pass": False,
             "reaction": None, "phase": None, "evidence": str(directory / f"touch_{tag}.result.json")}
    if not result["ok"]:
        check["error"] = "probe failed; unmeasured, never a miss"
        return check
    sample = result["data"]["samples"][event_step]
    check.update(reaction=sample["reaction"], phase=sample["phase"])
    if zone is None:
        check["pass"] = sample["reaction"] == "" and sample["phase"] == 0
    else:
        check["pass"] = sample["reaction"] in reactions[zone] and sample["phase"] in (1, 2)
    return check


def capture_pair(player, model, session, cond, directory, lock_path):
    captures = {}
    with Path(lock_path).open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for overlay in (False, True):
            name = "overlay" if overlay else "plain"
            png = directory / f"{name}.png"
            cmd = [player, "--model", model, "--session", session, "--width", "800",
                   "--height", "1080", "--origin-x", str(cond["origin"][0]),
                   "--origin-y", str(cond["origin"][1]), "--scale", str(cond["scale"]),
                   "--seed", str(RANDOM_SEED), "--transparent", "--mute", "--no-save",
                   "--steps", str(cond["steps"]), "--capture", png]
            if overlay:
                cmd.append("--show-touch-regions")
            result = run_process(cmd, directory / name)
            if result["ok"]:
                try:
                    from PIL import Image
                    with Image.open(png) as image:
                        image.load()
                        if image.size != (800, 1080) or image.format != "PNG":
                            raise ValueError("invalid capture dimensions/format")
                        if image.getbbox() is None:
                            raise ValueError("empty capture")
                    result["image"] = file_record(png)
                except (OSError, ValueError) as error:
                    result["ok"] = False
                    result["error"] = str(error)
            captures[name] = result
            write_json(directory / f"{name}.capture.json", result)
    return captures


def audit_model(probe, player, assets, model_name, output_dir, do_capture,
                source_files=None, lock_path=None):
    catalog_index.validate_model_name(model_name)
    model_dir = assets / "characters" / model_name
    model, session = model_dir / "model.nkm.json", model_dir / "session.nks.json"
    entry = {"model": model_name, "status": "UNMEASURED", "mirror": None,
             "author_regions": {}, "zone_reactions": {}, "conditions": [], "errors": [],
             "assets": [], "sources": source_files or [], "visual_acceptance": "NOT_REVIEWED"}
    for path, status in ((model, "MISSING_MODEL"), (session, "MISSING_SESSION")):
        if not path.is_file():
            entry.update(status=status, errors=[str(path) + " missing"])
            return entry
    try:
        definition = json.loads(session.read_text())["session"]
        asset = json.loads(model.read_text())["model"]
        author = author_regions(definition)
        reactions = zone_reactions(definition)
        entry.update(author_regions=author, zone_reactions=reactions)
        mirror = asset["mirror"]
        if type(mirror) is not bool:
            raise ValueError("invalid model mirror")
        scale_field = asset["author_scale"]
        author_scale = scale_field["value"] if scale_field["state"] == 2 else 1
        if not isinstance(author_scale, (int, float)) or not math.isfinite(author_scale) or author_scale <= 0:
            raise ValueError("invalid author scale")
        entry.update(mirror=mirror, author_scale=author_scale)
        paths = [model, session] + [model_dir / t["path"] for t in asset["textures"]]
        for path in paths:
            path.resolve().relative_to(assets.resolve())
            record = file_record(path)
            record["role"] = "model_asset" if path == model else "session_asset" if path == session else "texture_asset"
            entry["assets"].append(record)
    except (OSError, ValueError, KeyError, TypeError) as error:
        entry.update(status="INVALID_ASSET", errors=[str(error)])
        return entry
    directory = output_dir / model_name
    directory.mkdir()
    any_active = any(author[z] is not None and reactions[z] for z in ZONE_ORDER)
    for cond in CONDITIONS:
        case_dir = directory / cond["name"]
        case_dir.mkdir()
        case = {"name": cond["name"], "params": request_for(cond), "errors": [],
                "zone_tests": {}, "outside_test": None, "capture": None,
                "pass": False, "evidence": str(case_dir / "regions.result.json")}
        entry["conditions"].append(case)
        result = run_probe(probe, model, session, request_for(cond), case_dir / "regions.json")
        if not result["ok"]:
            case["errors"].append("region/initial state probe failed")
            # Only preserve the known diagnosed geometry error, not all process failures.
            stderr = Path(result["stderr"]["path"]).read_text(errors="replace")
            if len(entry["conditions"]) == 1 and "session=6 playback=3 geometry=5" in stderr:
                entry.update(status="CPU_REJECTED", errors=[stderr.strip()])
                return entry
            continue
        data = result["data"]
        actual = data["touch_regions"]
        expected = projected_regions(author, reactions, mirror, author_scale, cond)
        case.update(projected_regions=actual, expected_regions=expected)
        if data["presentation_mirror"] != mirror or actual != expected:
            case["errors"].append("projection mismatch against independent asset transform")
        before = data["samples"][-1]
        if before["phase"] != 0 or before["reaction"]:
            case["errors"].append("initial state not Idle; hit checks unavailable")
        if expected_zone(OUTSIDE_POINT, expected) is not None:
            case["errors"].append("global outside point is inside an ellipse")
        if not case["errors"]:
            for zone in ZONE_ORDER:
                r = expected[zone]
                if r is None:
                    case["zone_tests"][zone] = {"skipped": True, "reason": "no eligible author region/reaction"}
                    continue
                case["zone_tests"][zone] = {"skipped": False}
                for tag, point in (("center", r["center"]), ("inside", inside_point(r["center"], r["radius"]))):
                    case["zone_tests"][zone][tag + "_test"] = touch_check(
                        probe, model, session, cond, point, expected, reactions, case_dir, zone + "_" + tag)
            case["outside_test"] = touch_check(probe, model, session, cond, OUTSIDE_POINT,
                                                expected, reactions, case_dir, "outside")
        if do_capture:
            case["capture"] = capture_pair(player, model, session, cond, case_dir, lock_path)
            if not all(c["ok"] for c in case["capture"].values()):
                case["errors"].append("capture failed")
        checks = [check for zone in case["zone_tests"].values() if not zone["skipped"]
                  for check in (zone["center_test"], zone["inside_test"])]
        case["pass"] = (not case["errors"] and case["outside_test"] is not None and
                        case["outside_test"]["pass"] and all(check["pass"] for check in checks))
    entry["status"] = ("OK" if any_active else "NO_ACTIVE_ZONES") if all(
        c["pass"] for c in entry["conditions"]) else "FAIL"
    entry["errors"] = [f"{c['name']}: {e}" for c in entry["conditions"] for e in c["errors"]]
    return entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-root", required=True)
    parser.add_argument("--assets", required=True)
    parser.add_argument("--probe", required=True)
    parser.add_argument("--player")
    parser.add_argument("--output", required=True)
    parser.add_argument("--model", action="append")
    parser.add_argument("--capture", action="store_true")
    parser.add_argument("--report")
    args = parser.parse_args()
    assets, reference, output = (Path(v).resolve() for v in (args.assets, args.reference_root, args.output))
    probe = Path(args.probe).resolve()
    player = Path(args.player).resolve() if args.player else None
    report = Path(args.report).resolve() if args.report else output / "ledger.json"
    try:
        catalog, catalog_hash = catalog_index.load_catalog(reference)
        if not catalog:
            raise ValueError("catalog is empty")
        names = args.model if args.model else sorted(catalog)
        if any(name not in catalog for name in names):
            raise ValueError("requested model is not in catalog")
        if output.exists() or report.exists():
            raise ValueError("output/report already exists; choose a fresh directory")
        report.relative_to(output)
        if not probe.is_file() or (args.capture and (player is None or not player.is_file())):
            raise ValueError("probe/player binary missing")
    except (OSError, ValueError) as error:
        parser.error(str(error))
    output.mkdir(parents=True)
    report.parent.mkdir(parents=True, exist_ok=True)
    index = catalog_index.build_index(reference)
    lock_path = ROOT / "build/gpu-validation.lock"
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    ledger = {"task": "G12", "format_version": 2,
              "generated_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "head_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
              "argv": sys.argv, "reference_root": str(reference), "assets_root": str(assets),
              "catalog": file_record(reference / "config/model-catalog.json"), "catalog_sha256": catalog_hash,
              "catalog_total": len(catalog), "scope": "single_model" if args.model else "full_catalog",
              "tools": [file_record(Path(__file__)), file_record(Path(catalog_index.__file__))],
              "probe": file_record(probe), "player": file_record(player) if player else None,
              "python": {"version": sys.version, "binary": file_record(sys.executable)},
              "capture_requested": args.capture, "gpu_lock": str(lock_path),
              "conditions": CONDITIONS, "models": [], "complete": False,
              "visual_acceptance": "NOT_REVIEWED", "audio_scope": "muted, audio assets not audited"}
    for name in names:
        candidates = catalog_index.resolve_candidates(index, name)
        files = []
        for candidate in candidates:
            files.extend([candidate] + sorted(candidate.parent.glob("*.png")) +
                         sorted(candidate.parent.glob("*.resx.json")))
        config = reference / "config" / catalog[name]["config"]
        source_errors = []
        if len(candidates) != 1:
            source_errors.append("source model is missing or ambiguous")
        if config.is_file():
            files.append(config)
        else:
            source_errors.append("source configuration missing")
        for path in (reference / "data" / f"{name}.zip", reference / "data" / f"{name}.psb"):
            if path.is_file():
                files.append(path)
        source_files = []
        for path in sorted(set(files)):
            record = file_record(path)
            record["role"] = "source" if path in candidates else "source_resource"
            source_files.append(record)
        entry = audit_model(str(probe), str(player) if player else None, assets, name,
                            output, args.capture, source_files, lock_path)
        entry["source_resolution"] = "UNIQUE" if not source_errors else "MISSING_OR_AMBIGUOUS"
        entry["source_errors"] = source_errors
        if source_errors and entry["status"] in ("OK", "NO_ACTIVE_ZONES"):
            entry["status"] = "SOURCE_UNRESOLVED"
        ledger["models"].append(entry)
        counts = {}
        for row in ledger["models"]:
            counts[row["status"]] = counts.get(row["status"], 0) + 1
        ledger["summary"] = {"total": len(ledger["models"]), "status_counts": counts}
        write_json(report, ledger)
        print(f"{len(ledger['models'])}/{len(names)} {name}: {entry['status']}", file=sys.stderr)
    frozen = ledger["tools"] + [ledger["probe"], ledger["catalog"], ledger["python"]["binary"]]
    if ledger["player"]:
        frozen.append(ledger["player"])
    for row in ledger["models"]:
        frozen.extend(row["assets"] + row["sources"])
    ledger["freeze_errors"] = [record["path"] for record in frozen
                               if not Path(record["path"]).is_file() or
                               sha256_file(record["path"]) != record["sha256"]]
    ledger["complete"] = len(ledger["models"]) == len(names)
    ledger["all_requested_checks_passed"] = (not ledger["freeze_errors"] and
        all(row["status"] in ("OK", "NO_ACTIVE_ZONES") for row in ledger["models"]))
    write_json(report, ledger)
    return 0 if ledger["all_requested_checks_passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
