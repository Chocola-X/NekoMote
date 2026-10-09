#!/usr/bin/env python3
"""G26-1: 45 combination reference comparison for three representative models.

Restores the 15 variable combinations from G25's extended-combo and generates
both the original WebGL reference and the formal native geometry/image, then
compares geometry (<=.001px, complete vertices/triangles) and composed images
(alpha and black/white composition RGB <=2, zero pixels over tolerance).

Uses relative paths from the project root and CLI-configured binaries, so the
script is committable and reproducible on other machines.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]

REPS = [
    "coconut-santa",
    "azuki-santa-b",
    "cinnamon-maid",
]

COMBOS = [
    ("head_LR30", {"head_LR": 30}),
    ("head_LR-30", {"head_LR": -30}),
    ("head_UD30", {"head_UD": 30}),
    ("body_head_LR30", {"body_LR": 30, "head_LR": 30}),
    ("body_head_LR-30", {"body_LR": -30, "head_LR": -30}),
    ("body_UD_head_UD30", {"body_UD": 30, "head_UD": 30}),
    ("body_LR_UD30", {"body_LR": 30, "body_UD": 30}),
    ("body_LR_slant30", {"body_LR": 30, "body_slant": 30}),
    ("eye_LR30", {"face_eye_LR": 30}),
    ("eye_UD30", {"face_eye_UD": 30}),
    ("move_LR20_UD10", {"move_LR": 20, "move_UD": 10}),
    ("bust_LR20", {"bust_LR": 20}),
    ("mouth_open50", {"face_mouth": 0.5}),
    ("eye_open50", {"face_eye_open": 0.5}),
    ("full_combo", {"body_LR": 15, "body_UD": 10, "head_LR": 15, "head_UD": 10,
                    "body_slant": 10, "face_eye_LR": 10, "face_mouth": 0.3}),
]

ORIGIN_X = 0
ORIGIN_Y = 100
SCALE = 0.45
GEOMETRY_THRESHOLD = 0.001
IMAGE_TOLERANCE = 2


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


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=pathlib.Path,
                   default=ROOT / "build/evidence/g26-validation/combos-45")
    p.add_argument("--reference-root", type=pathlib.Path,
                   default=ROOT.parent / "NekoWebShow")
    p.add_argument("--browser", type=pathlib.Path,
                   default=pathlib.Path("/home/chocola/.cache/ms-playwright/chromium-1194/chrome-linux/chrome"))
    p.add_argument("--pose", type=pathlib.Path,
                   default=ROOT / "build/release/bin/nekomote_pose")
    p.add_argument("--preview", type=pathlib.Path,
                   default=ROOT / "build/release/bin/nekomote_preview")
    p.add_argument("--gpu-lock", type=pathlib.Path,
                   default=ROOT / "build/gpu-validation.lock")
    args = p.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    capture_py = ROOT / "tools/reference_capture/capture.py"
    compare_images_py = ROOT / "tools/reference_capture/compare_images.py"

    manifest = {
        "scope": "45 combinations: 3 representative models x 15 variable combos",
        "geometry_threshold_px": GEOMETRY_THRESHOLD,
        "image_tolerance": IMAGE_TOLERANCE,
        "origin": [ORIGIN_X, ORIGIN_Y],
        "scale": SCALE,
        "binary_hashes": {
            "nekomote_pose": sha256(args.pose),
            "nekomote_preview": sha256(args.preview),
        },
        "tool_hashes": {
            "capture.py": sha256(capture_py),
            "compare_images.py": sha256(compare_images_py),
        },
        "cases": [],
    }

    passed_count = 0
    for model in REPS:
        asset = ROOT / "assets/characters" / model / "model.nkm.json"
        asset_hash = sha256(asset)
        for combo_name, variables in COMBOS:
            case = f"{model}_{combo_name}"
            base = args.output / case
            row = {"case": case, "model": model, "combo": combo_name,
                   "variables": variables, "asset_hash": asset_hash,
                   "runs": [], "passed": False, "status": "NOT_RUN"}

            capture = [
                "flock", args.gpu_lock,
                "python3", capture_py,
                "--reference-root", args.reference_root,
                "--model", model,
                "--browser", args.browser,
                "--output", str(base) + "-reference",
                "--x", str(ORIGIN_X), "--y", str(ORIGIN_Y),
                "--scale", str(SCALE),
                "--variables", json.dumps(variables),
                "--trace",
            ]
            row["runs"].append(run(capture, str(base) + "-capture"))
            if row["runs"][-1]["returncode"] != 0:
                row["status"] = "CAPTURE_FAILED"
                manifest["cases"].append(row)
                continue

            trace = str(base) + "-reference.trace.json"
            geom_output = str(base) + "-geometry.json"
            pose = [
                args.pose,
                "--asset", asset,
                "--variables", json.dumps(variables),
                "--sample-trace", trace,
                "--dump-frame",
                "--output", geom_output,
            ]
            row["runs"].append(run(pose, str(base) + "-evaluate"))
            if row["runs"][-1]["returncode"] != 0:
                row["status"] = "POSE_FAILED"
                manifest["cases"].append(row)
                continue

            native = json.loads(pathlib.Path(geom_output).read_text())
            row["geometry"] = {
                "max_error": native.get("max_error", 0),
                "frame_max_error_px": native.get("frame_max_error_px", 0),
                "missing_primitives": len(native.get("missing_primitives", [])),
                "missing_reference_triangles": native.get("missing_reference_triangles", 0),
                "unused_native_triangles": native.get("unused_native_triangles", 0),
                "unmatched_reference_vertices": native.get("unmatched_reference_vertices", 0),
                "unused_native_vertices": native.get("unused_native_vertices", 0),
            }
            row["frame_vertices"] = native.get("frame_vertices", 0)
            row["frame_indices"] = native.get("frame_indices", 0)
            row["primitives"] = native.get("primitives", 0)
            g = row["geometry"]
            geom_pass = (g["max_error"] <= GEOMETRY_THRESHOLD and
                         g["frame_max_error_px"] <= GEOMETRY_THRESHOLD and
                         g["missing_primitives"] == 0 and
                         g["missing_reference_triangles"] == 0 and
                         g["unused_native_triangles"] == 0 and
                         g["unmatched_reference_vertices"] == 0 and
                         g["unused_native_vertices"] == 0)

            render = [
                "flock", args.gpu_lock,
                args.preview,
                "--asset", asset,
                "--geometry-diagnostic",
                "--width", "800", "--height", "1080",
                "--origin", "400", "640",
                "--scale", str(SCALE),
                "--frames", "1",
                "--hidden",
                "--screenshot", str(base) + "-native.png",
            ]
            for key, value in variables.items():
                render += ["--variable", key, str(value)]
            row["runs"].append(run(render, str(base) + "-render"))
            if row["runs"][-1]["returncode"] != 0:
                row["status"] = "RENDER_FAILED"
                manifest["cases"].append(row)
                continue

            comparison = [
                "python3", compare_images_py,
                str(base) + "-reference.png",
                str(base) + "-native.png",
                "--output", str(base) + "-images",
            ]
            row["runs"].append(run(comparison, str(base) + "-compare"))
            report_path = pathlib.Path(str(base) + "-images.json")
            if not report_path.exists():
                row["status"] = "COMPARE_FAILED"
                manifest["cases"].append(row)
                continue

            image_report = json.loads(report_path.read_text())
            row["image"] = {
                "alpha_max_error": image_report["alpha_max_error"],
                "composed_max_rgb_black_white": image_report["composed_max_rgb_black_white"],
                "pixels_over_tolerance": image_report["pixels_over_tolerance"],
            }
            image_pass = image_report["pixels_over_tolerance"] == 0

            row["geometry_pass"] = geom_pass
            row["image_pass"] = image_pass
            row["passed"] = geom_pass and image_pass
            if row["passed"]:
                row["status"] = "PASS"
                passed_count += 1
            else:
                # A parameter name cannot establish the cause of a failed comparison.
                # Keep failures visible until matched-input reference evidence diagnoses them.
                row["status"] = "FAIL"

            row["hashes"] = {
                p.name: sha256(p) for p in args.output.glob(case + "-*")
                if p.is_file() and p.suffix in (".json", ".png")
            }
            manifest["cases"].append(row)
            (args.output / "manifest.json").write_text(
                json.dumps(manifest, indent=2) + "\n")
            print(f"{case}: {row['status']} geom={g['frame_max_error_px']:.6f}px "
                  f"img_over={image_report['pixels_over_tolerance']}", flush=True)

    manifest["passed"] = passed_count
    manifest["total"] = len(manifest["cases"])
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"\n45 combos: {passed_count}/{len(manifest['cases'])} passed", flush=True)
    return 0 if passed_count == len(manifest["cases"]) else 1


if __name__ == "__main__":
    sys.exit(main())
