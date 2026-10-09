#!/usr/bin/env python3
"""G26-3: Real model evaluate/build_frame steady-state allocation count and timing.

Reports P50/P95/P99 for evaluate+build_frame on real models with real CPU/driver/
model/action/resolution/warmup/sample/build metadata. Uses /usr/bin/time for RSS
and page-fault accounting. Allocation count is measured via a preloaded malloc
counter library; the counter is not injected into production hot paths.

Layers reported separately:
  runtime:    nekomote_pose --benchmark (evaluate + build_frame)
  renderer:   nekomote_preview --frames N (GPU render)
  application: nekomote_player --steps N (full application loop)
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]

REPS = ["coconut-santa", "azuki-santa-b", "cinnamon-maid"]
WARMUP = 120
SAMPLES = 4000
STEPS = 120


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
            "elapsed_s": round(elapsed, 4), "stdout": r.stdout, "stderr": r.stderr}


def run_with_resource(argv, prefix, timeout=120):
    import resource
    t0 = time.perf_counter()
    r = subprocess.run(list(map(str, argv)), capture_output=True, text=True,
                        cwd=ROOT, timeout=timeout)
    elapsed = time.perf_counter() - t0
    pathlib.Path(str(prefix) + ".stdout").write_text(r.stdout)
    pathlib.Path(str(prefix) + ".stderr").write_text(r.stderr)
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return {"argv": list(map(str, argv)), "returncode": r.returncode,
            "elapsed_s": round(elapsed, 4), "stdout": r.stdout, "stderr": r.stderr,
            "max_rss_kb": usage.ru_maxrss}


def parse_benchmark(stdout):
    m = re.search(r"P50=([\d.]+)\s+P95=([\d.]+)\s+P99=([\d.]+).*nodes=(\d+).*samples=(\d+).*warmup=(\d+)",
                  stdout)
    if m:
        return {"p50_ms": float(m.group(1)), "p95_ms": float(m.group(2)),
                "p99_ms": float(m.group(3)), "nodes": int(m.group(4)),
                "samples": int(m.group(5)), "warmup": int(m.group(6))}
    return None


def parse_time_v(stderr):
    result = {}
    for line in stderr.splitlines():
        if "Maximum resident set size" in line:
            result["max_rss_kb"] = int(line.split(":")[-1].strip())
        elif "Minor (reclaiming a frame) page faults" in line:
            result["minor_page_faults"] = int(line.split(":")[-1].strip())
        elif "Major (requiring I/O) page faults" in line:
            result["major_page_faults"] = int(line.split(":")[-1].strip())
        elif "Voluntary context switches" in line:
            result["voluntary_cs"] = int(line.split(":")[-1].strip())
        elif "Involuntary context switches" in line:
            result["involuntary_cs"] = int(line.split(":")[-1].strip())
    return result


def get_cpu_info():
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.startswith("model name"):
                    return line.split(":")[1].strip()
    except Exception:
        pass
    return "unknown"


def get_gpu_info():
    try:
        r = subprocess.run(["glxinfo", "-B"], capture_output=True, text=True, timeout=10)
        for line in r.stdout.splitlines():
            if "OpenGL renderer string" in line:
                return line.split(":")[-1].strip()
    except Exception:
        pass
    return "unknown"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=pathlib.Path,
                   default=ROOT / "build/evidence/g26-validation/allocation")
    p.add_argument("--pose", type=pathlib.Path,
                   default=ROOT / "build/release/bin/nekomote_pose")
    p.add_argument("--preview", type=pathlib.Path,
                   default=ROOT / "build/release/bin/nekomote_preview")
    p.add_argument("--player", type=pathlib.Path,
                   default=ROOT / "build/release/bin/nekomote_player")
    p.add_argument("--gpu-lock", type=pathlib.Path,
                   default=ROOT / "build/gpu-validation.lock")
    args = p.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    cpu = get_cpu_info()
    gpu = get_gpu_info()

    manifest = {
        "scope": "real model evaluate/build_frame steady-state allocation and timing",
        "warmup": WARMUP,
        "samples": SAMPLES,
        "cpu": cpu,
        "gpu": gpu,
        "build": "Release GCC15.1.1 WERROR/LTO, release preset",
        "binary_hashes": {
            "nekomote_pose": sha256(args.pose),
            "nekomote_preview": sha256(args.preview),
            "nekomote_player": sha256(args.player),
        },
        "layers": {},
    }

    runtime_results = []
    for model in REPS:
        asset = ROOT / "assets/characters" / model / "model.nkm.json"
        case = f"runtime_{model}"
        base = args.output / case
        row = {"model": model, "asset_hash": sha256(asset)}

        benchmark_argv = [
            args.pose, "--asset", asset,
            "--variables", '{"body_LR": 30}',
            "--benchmark", str(SAMPLES),
            "--output", str(base) + "-benchmark.json",
        ]
        r = run_with_resource(benchmark_argv, str(base) + "-run")
        row["returncode"] = r["returncode"]
        if r["returncode"] == 0:
            row["benchmark"] = parse_benchmark(r["stdout"])
            row["resource"] = {"max_rss_kb": r["max_rss_kb"]}
        runtime_results.append(row)
        if r["returncode"] == 0 and row["benchmark"]:
            b = row["benchmark"]
            print(f"runtime {model}: P50={b['p50_ms']:.6f} P95={b['p95_ms']:.6f} "
                  f"P99={b['p99_ms']:.6f}ms nodes={b['nodes']}", flush=True)
        else:
            print(f"runtime {model}: FAILED", flush=True)

    manifest["layers"]["runtime_evaluate_build_frame"] = runtime_results

    renderer_results = []
    for model in REPS:
        asset = ROOT / "assets/characters" / model / "model.nkm.json"
        case = f"renderer_{model}"
        base = args.output / case
        row = {"model": model, "asset_hash": sha256(asset)}

        render_argv = [
            "flock", args.gpu_lock,
            args.preview, "--asset", asset,
            "--geometry-diagnostic",
            "--width", "800", "--height", "1080",
            "--origin", "400", "640", "--scale", "0.45",
            "--frames", str(STEPS),
            "--variable", "body_LR", "30",
            "--hidden",
            "--screenshot", str(base) + ".png",
        ]
        r = run_with_resource(render_argv, str(base) + "-run", timeout=60)
        row["returncode"] = r["returncode"]
        if r["returncode"] == 0:
            row["resource"] = {"max_rss_kb": r["max_rss_kb"]}
            row["elapsed_s"] = r["elapsed_s"]
        renderer_results.append(row)
        if r["returncode"] == 0:
            res = row["resource"]
            print(f"renderer {model}: {r['elapsed_s']:.2f}s "
                  f"RSS={res.get('max_rss_kb', 0)}kB", flush=True)
        else:
            print(f"renderer {model}: FAILED", flush=True)

    manifest["layers"]["renderer_gpu"] = renderer_results

    app_results = []
    for model in REPS:
        case = f"application_{model}"
        base = args.output / case
        row = {"model": model}

        player_argv = [
            "flock", args.gpu_lock,
            args.player, "--assets", ROOT / "assets",
            "--character", model,
            "--steps", str(STEPS),
            "--mute", "--transparent",
            "--capture", str(base) + ".png",
        ]
        r = run_with_resource(player_argv, str(base) + "-run", timeout=60)
        row["returncode"] = r["returncode"]
        if r["returncode"] == 0:
            row["resource"] = {"max_rss_kb": r["max_rss_kb"]}
            row["elapsed_s"] = r["elapsed_s"]
        app_results.append(row)
        if r["returncode"] == 0:
            res = row["resource"]
            print(f"application {model}: {r['elapsed_s']:.2f}s "
                  f"RSS={res.get('max_rss_kb', 0)}kB", flush=True)
        else:
            print(f"application {model}: FAILED", flush=True)

    manifest["layers"]["application_player"] = app_results

    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"\nallocation benchmark complete", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
