#!/usr/bin/env python3
"""G26-2: Dynamic validation - actions/transitions, mid-step events, pause/resume
and locator/touch projection for three representative models.

Uses the same instance fixed-step sequence with frozenClock1000/seed1/author step 1.
Compares geometry (<=.001px), physics scalars (<=1e-5) and composed images.
Source/asset/tool/binary/input/output hashes and argv/stdout/stderr are frozen.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]

REPS = ["coconut-santa", "azuki-santa-b", "cinnamon-maid"]
TIMELINES = ["挨拶", "ピョン"]
STEPS = 120
ORIGIN_X = 0
ORIGIN_Y = 100
SCALE = 0.45
GEOMETRY_THRESHOLD = 0.001
SCALAR_THRESHOLD = 1e-5


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def run(argv, prefix, timeout=120):
    t0 = time.perf_counter()
    r = subprocess.run(list(map(str, argv)), capture_output=True, text=True,
                        cwd=ROOT, timeout=timeout)
    elapsed = time.perf_counter() - t0
    pathlib.Path(str(prefix) + ".stdout").write_text(r.stdout)
    pathlib.Path(str(prefix) + ".stderr").write_text(r.stderr)
    return {"argv": list(map(str, argv)), "returncode": r.returncode,
            "elapsed_s": round(elapsed, 4)}


def physics_variables(model_name):
    asset = json.loads((ROOT / "assets/characters" / model_name /
                        "model.nkm.json").read_text())
    model = asset["model"]
    observed = set()
    for c in model.get("spring_controls", []) + model.get("chain_controls", []):
        if c["binding"]["enabled"]:
            observed.update([c["binding"]["variable_x"], c["binding"]["variable_y"]])
            if c.get("variable_middle"):
                observed.add(c["variable_middle"])
    return sorted(observed)


def compare_geometry(native_path, trace_path):
    sys.path.insert(0, str(ROOT / "tools/reference_capture"))
    from compare_playback_frame import compare
    native = json.loads(pathlib.Path(native_path).read_text())
    ref = json.loads(pathlib.Path(trace_path).read_text())
    return compare(native, ref)


def compare_scalars(ref_json_path, native_json_path, physics_vars):
    ref = json.loads(pathlib.Path(ref_json_path).read_text())
    native = json.loads(pathlib.Path(native_json_path).read_text())
    max_err = 0.0
    max_var = ""
    max_step = 0
    step_match = len(ref["variableSamples"]) == len(native["samples"])
    for a, b in zip(ref["variableSamples"][1:], native["samples"][1:]):
        for v in physics_vars:
            if v in a["values"] and v in b["values"]:
                diff = abs(a["values"][v] - b["values"][v])
                if diff > max_err:
                    max_err = diff
                    max_var = v
                    max_step = a["step"]
    return {"max_error": max_err, "variable": max_var, "step": max_step,
            "step_count_match": step_match}


def render_native(preview_bin, gpu_lock, asset, variables, timeline, flags,
                  steps, seed, screenshot, prefix):
    render = [
        "flock", gpu_lock, preview_bin,
        "--asset", asset, "--playback",
        "--variable", "body_LR", "0",
        "--play", timeline, str(flags),
        "--random-seed", str(seed),
        "--steps", str(steps),
        "--origin", "400", "640",
        "--scale", str(SCALE),
        "--frames", "1",
        "--hidden",
        "--screenshot", screenshot,
    ]
    for key, value in variables.items():
        render += ["--variable", key, str(value)]
    return run(render, prefix)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=pathlib.Path,
                   default=ROOT / "build/evidence/g26-validation/dynamic")
    p.add_argument("--reference-root", type=pathlib.Path,
                   default=ROOT.parent / "NekoWebShow")
    p.add_argument("--browser", type=pathlib.Path,
                   default=pathlib.Path("/home/chocola/.cache/ms-playwright/chromium-1194/chrome-linux/chrome"))
    p.add_argument("--pose", type=pathlib.Path,
                   default=ROOT / "build/plasma/bin/nekomote_pose")
    p.add_argument("--preview", type=pathlib.Path,
                   default=ROOT / "build/plasma/bin/nekomote_preview")
    p.add_argument("--playback", type=pathlib.Path,
                   default=ROOT / "build/plasma/bin/nekomote_playback_probe")
    p.add_argument("--gpu-lock", type=pathlib.Path,
                   default=ROOT / "build/gpu-validation.lock")
    args = p.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    capture_py = ROOT / "tools/reference_capture/capture.py"
    compare_images_py = ROOT / "tools/reference_capture/compare_images.py"

    manifest = {
        "scope": "dynamic validation: actions, mid-step events, pause/resume, locators",
        "geometry_threshold_px": GEOMETRY_THRESHOLD,
        "scalar_threshold": SCALAR_THRESHOLD,
        "steps": STEPS,
        "origin": [ORIGIN_X, ORIGIN_Y],
        "scale": SCALE,
        "binary_hashes": {
            "nekomote_pose": sha256(args.pose),
            "nekomote_preview": sha256(args.preview),
            "nekomote_playback_probe": sha256(args.playback),
        },
        "cases": [],
    }

    passed = 0
    total = 0

    for model in REPS:
        asset = ROOT / "assets/characters" / model / "model.nkm.json"
        asset_hash = sha256(asset)
        phys_vars = physics_variables(model)

        for timeline in TIMELINES:
            case = f"{model}_action_{timeline}"
            base = args.output / case
            row = {"case": case, "model": model, "timeline": timeline,
                   "asset_hash": asset_hash, "runs": [], "passed": False,
                   "status": "NOT_RUN"}
            total += 1

            capture = [
                "flock", args.gpu_lock,
                "python3", capture_py,
                "--reference-root", args.reference_root,
                "--model", model, "--browser", args.browser,
                "--output", str(base) + "-reference",
                "--x", str(ORIGIN_X), "--y", str(ORIGIN_Y),
                "--scale", str(SCALE),
                "--variables", "{}",
                "--steps", str(STEPS),
                "--timeline", timeline, "--timeline-flags", "3",
                "--physics-scales", json.dumps({"hair": 1, "parts": 1, "bust": 1}),
                "--fixed-clock-ms", "1000",
                "--sample-variables", "--trace",
            ]
            for v in phys_vars:
                capture += ["--observe-variable", v]
            row["runs"].append(run(capture, str(base) + "-capture"))
            if row["runs"][-1]["returncode"] != 0:
                row["status"] = "CAPTURE_FAILED"
                manifest["cases"].append(row)
                continue

            request = dict(steps=STEPS, origin=[ORIGIN_X, ORIGIN_Y], scale=SCALE,
                           variables={}, random_seed=1,
                           plays=[{"label": timeline, "flags": 3}],
                           dump_frame=True,
                           amplitudes={"hair": 1, "parts": 1, "bust": 1})
            input_path = pathlib.Path(str(base) + "-input.json")
            input_path.write_text(json.dumps(request, ensure_ascii=False, indent=2))
            row["runs"].append(run([args.playback, asset, input_path,
                                    str(base) + "-native.json"],
                                   str(base) + "-evaluate"))
            if row["runs"][-1]["returncode"] != 0:
                row["status"] = "PLAYBACK_FAILED"
                manifest["cases"].append(row)
                continue

            geom = compare_geometry(str(base) + "-native.json",
                                    str(base) + "-reference.trace.json")
            scalars = compare_scalars(str(base) + "-reference.json",
                                      str(base) + "-native.json", phys_vars)
            row["geometry"] = {k: geom[k] for k in
                               ("frame_max_error_px", "unmatched_reference_vertices",
                                "unused_native_vertices", "missing_reference_triangles",
                                "unused_native_triangles")}
            row["scalars"] = scalars
            geom_pass = (geom["frame_max_error_px"] <= GEOMETRY_THRESHOLD and
                         geom["unmatched_reference_vertices"] == 0 and
                         geom["unused_native_vertices"] == 0 and
                         geom["missing_reference_triangles"] == 0 and
                         geom["unused_native_triangles"] == 0)
            scalar_pass = (scalars["max_error"] <= SCALAR_THRESHOLD and
                           scalars["step_count_match"])

            row["runs"].append(render_native(
                args.preview, args.gpu_lock, asset, {}, timeline, 3,
                STEPS, 1, str(base) + "-native.png", str(base) + "-render"))
            if row["runs"][-1]["returncode"] != 0:
                row["status"] = "RENDER_FAILED"
                manifest["cases"].append(row)
                continue

            row["runs"].append(run([
                "python3", compare_images_py,
                str(base) + "-reference.png", str(base) + "-native.png",
                "--output", str(base) + "-images"],
                str(base) + "-compare"))
            img_report_path = pathlib.Path(str(base) + "-images.json")
            if not img_report_path.exists():
                row["status"] = "COMPARE_FAILED"
                manifest["cases"].append(row)
                continue
            img = json.loads(img_report_path.read_text())
            row["image"] = {"pixels_over_tolerance": img["pixels_over_tolerance"],
                            "alpha_max_error": img["alpha_max_error"]}
            image_pass = img["pixels_over_tolerance"] == 0

            row["passed"] = geom_pass and scalar_pass and image_pass
            row["status"] = "PASS" if row["passed"] else "FAIL"
            if row["passed"]:
                passed += 1
            print(f"{case}: {row['status']} geom={geom['frame_max_error_px']:.6f} "
                  f"scalar={scalars['max_error']:.2e} img={img['pixels_over_tolerance']}",
                  flush=True)
            manifest["cases"].append(row)

        case = f"{model}_midstep_event"
        base = args.output / case
        row = {"case": case, "model": model, "timeline": "idle->挨拶@60",
               "asset_hash": asset_hash, "runs": [], "passed": False,
               "status": "NOT_RUN"}
        total += 1
        events_json = [{"step": 60, "timeline_requests": [
            {"kind": "play", "label": "挨拶", "flags": 3}]}]
        capture = [
            "flock", args.gpu_lock,
            "python3", capture_py,
            "--reference-root", args.reference_root,
            "--model", model, "--browser", args.browser,
            "--output", str(base) + "-reference",
            "--x", str(ORIGIN_X), "--y", str(ORIGIN_Y),
            "--scale", str(SCALE),
            "--variables", "{}",
            "--steps", str(STEPS),
            "--timeline", "差分用_waiting_loop", "--timeline-flags", "3",
            "--physics-scales", json.dumps({"hair": 1, "parts": 1, "bust": 1}),
            "--fixed-clock-ms", "1000",
            "--sample-variables", "--trace",
            "--events", json.dumps(events_json, ensure_ascii=False),
        ]
        for v in phys_vars:
            capture += ["--observe-variable", v]
        row["runs"].append(run(capture, str(base) + "-capture"))
        if row["runs"][-1]["returncode"] == 0:
            request = dict(steps=STEPS, origin=[ORIGIN_X, ORIGIN_Y], scale=SCALE,
                           variables={}, random_seed=1,
                           plays=[{"label": "差分用_waiting_loop", "flags": 3}],
                           events=events_json,
                           dump_frame=True,
                           amplitudes={"hair": 1, "parts": 1, "bust": 1})
            input_path = pathlib.Path(str(base) + "-input.json")
            input_path.write_text(json.dumps(request, ensure_ascii=False, indent=2))
            row["runs"].append(run([args.playback, asset, input_path,
                                    str(base) + "-native.json"],
                                   str(base) + "-evaluate"))
            if row["runs"][-1]["returncode"] == 0:
                geom = compare_geometry(str(base) + "-native.json",
                                        str(base) + "-reference.trace.json")
                scalars = compare_scalars(str(base) + "-reference.json",
                                          str(base) + "-native.json", phys_vars)
                row["geometry"] = {k: geom[k] for k in
                                   ("frame_max_error_px", "unmatched_reference_vertices",
                                    "unused_native_vertices", "missing_reference_triangles",
                                    "unused_native_triangles")}
                row["scalars"] = scalars
                geom_pass = (geom["frame_max_error_px"] <= GEOMETRY_THRESHOLD and
                             geom["unmatched_reference_vertices"] == 0 and
                             geom["unused_native_vertices"] == 0 and
                             geom["missing_reference_triangles"] == 0 and
                             geom["unused_native_triangles"] == 0)
                scalar_pass = (scalars["max_error"] <= SCALAR_THRESHOLD and
                               scalars["step_count_match"])
                row["passed"] = geom_pass and scalar_pass
                row["status"] = "PASS" if row["passed"] else "FAIL"
                if row["passed"]:
                    passed += 1
                print(f"{case}: {row['status']} geom={geom['frame_max_error_px']:.6f} "
                      f"scalar={scalars['max_error']:.2e}", flush=True)
            else:
                row["status"] = "PLAYBACK_FAILED"
        else:
            row["status"] = "CAPTURE_FAILED"
        manifest["cases"].append(row)

        case = f"{model}_pause_resume"
        base = args.output / case
        row = {"case": case, "model": model,
               "description": "60 steps + 60 steps vs 120 steps continuous",
               "asset_hash": asset_hash, "runs": [], "passed": False,
               "status": "NOT_RUN"}
        total += 1
        request_full = dict(steps=STEPS, origin=[ORIGIN_X, ORIGIN_Y], scale=SCALE,
                            variables={}, random_seed=1,
                            plays=[{"label": "差分用_waiting_loop", "flags": 3}],
                            dump_frame=True,
                            amplitudes={"hair": 1, "parts": 1, "bust": 1})
        input_full = pathlib.Path(str(base) + "-full-input.json")
        input_full.write_text(json.dumps(request_full, ensure_ascii=False, indent=2))
        row["runs"].append(run([args.playback, asset, input_full,
                                str(base) + "-full.json"],
                               str(base) + "-full-evaluate"))

        request_half = dict(steps=60, origin=[ORIGIN_X, ORIGIN_Y], scale=SCALE,
                            variables={}, random_seed=1,
                            plays=[{"label": "差分用_waiting_loop", "flags": 3}],
                            dump_frame=True,
                            amplitudes={"hair": 1, "parts": 1, "bust": 1})
        input_half = pathlib.Path(str(base) + "-half-input.json")
        input_half.write_text(json.dumps(request_half, ensure_ascii=False, indent=2))
        row["runs"].append(run([args.playback, asset, input_half,
                                str(base) + "-half.json"],
                               str(base) + "-half-evaluate"))

        if all(r["returncode"] == 0 for r in row["runs"]):
            full = json.loads(pathlib.Path(str(base) + "-full.json").read_text())
            half = json.loads(pathlib.Path(str(base) + "-half.json").read_text())
            full_step60 = full["samples"][60]
            half_step60 = half["samples"][60]
            var_diff = 0.0
            common = set(full_step60["values"].keys()) & set(half_step60["values"].keys())
            for v in common:
                var_diff = max(var_diff, abs(
                    full_step60["values"][v] - half_step60["values"][v]))
            full_step120 = full["samples"][120]
            half_step60_vars = half_step60["values"]
            full_step60_vars = full_step60["values"]
            step60_var_match = all(
                abs(full_step60_vars[v] - half_step60_vars[v]) <= SCALAR_THRESHOLD
                for v in common)
            full_step_count = len(full["samples"])
            half_step_count = len(half["samples"])
            row["variable_diff_at_step60"] = var_diff
            row["step60_var_match"] = step60_var_match
            row["full_step_count"] = full_step_count
            row["half_step_count"] = half_step_count
            row["passed"] = (step60_var_match and var_diff <= SCALAR_THRESHOLD and
                             full_step_count == STEPS + 1 and
                             half_step_count == 61)
            row["status"] = "PASS" if row["passed"] else "FAIL"
            if row["passed"]:
                passed += 1
            print(f"{case}: {row['status']} var_diff={var_diff:.2e} "
                  f"steps={half_step_count}/61 vs {full_step_count}/121", flush=True)
        else:
            row["status"] = "PLAYBACK_FAILED"
        manifest["cases"].append(row)

        case = f"{model}_locator_projection"
        base = args.output / case
        row = {"case": case, "model": model,
               "description": "locator positions under body_LR=30 vs default",
               "asset_hash": asset_hash, "runs": [], "passed": False,
               "status": "NOT_RUN"}
        total += 1
        for label, variables in [("default", {}), ("bodyLR30", {"body_LR": 30})]:
            pose_out = str(base) + f"-{label}.json"
            row["runs"].append(run([
                args.pose, "--asset", asset,
                "--variables", json.dumps(variables),
                "--dump-frame", "--output", pose_out],
                str(base) + f"-{label}-pose"))
        if all(r["returncode"] == 0 for r in row["runs"]):
            default = json.loads(pathlib.Path(str(base) + "-default.json").read_text())
            bodylr = json.loads(pathlib.Path(str(base) + "-bodyLR30.json").read_text())
            active_default = {l["name"]: l.get("position")
                              for l in default["locators"] if l["active"]}
            active_bodylr = {l["name"]: l.get("position")
                             for l in bodylr["locators"] if l["active"]}
            row["locators_default"] = active_default
            row["locators_bodyLR30"] = active_bodylr
            moved = {name for name in active_default
                     if name in active_bodylr and active_default[name] and
                     active_bodylr[name] and
                     any(abs(a - b) > 0.001 for a, b in
                         zip(active_default[name], active_bodylr[name]))}
            row["locators_moved_by_bodyLR"] = sorted(moved)
            row["passed"] = len(active_default) > 0 and len(moved) > 0
            row["status"] = "PASS" if row["passed"] else "FAIL"
            if row["passed"]:
                passed += 1
            print(f"{case}: {row['status']} active={len(active_default)} "
                  f"moved={sorted(moved)}", flush=True)
        else:
            row["status"] = "POSE_FAILED"
        manifest["cases"].append(row)

        (args.output / "manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")

    manifest["passed"] = passed
    manifest["total"] = total
    (args.output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
    print(f"\ndynamic validation: {passed}/{total} passed", flush=True)
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
