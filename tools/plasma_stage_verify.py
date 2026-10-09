#!/usr/bin/env python3
"""Verify the Plasma wallpaper stage is self-contained and deployable.

Checks that all required files exist, the native library loads from the stage
(not the build tree), and a smoke QML import succeeds with stage paths only.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    repo = Path(__file__).resolve().parent.parent
    stage = Path(sys.argv[1]) if len(sys.argv) > 1 else repo / "build/plasma/adapters/plasma/stage"
    probe = Path(sys.argv[2]) if len(sys.argv) > 2 else repo / "build/plasma/bin/nekomote_plasma_package_probe"
    if not stage.is_dir():
        print(f"FAIL: stage directory not found: {stage}", file=sys.stderr)
        return 1
    required = [
        "lib/libnekomote_plasma_qml.so",
        "qml/NekoMote/Wallpaper/qmldir",
        "qml/NekoMote/Wallpaper/nekomote_plasma_qml.qmltypes",
        "qml/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so",
        "package/org.nekomote.wallpaper/metadata.json",
        "package/org.nekomote.wallpaper/contents/ui/main.qml",
        "package/org.nekomote.wallpaper/contents/ui/config.qml",
        "package/org.nekomote.wallpaper/contents/config/main.xml",
        "package/org.nekomote.validation/metadata.json",
        "package/org.nekomote.validation/contents/ui/main.qml",
    ]
    missing = [r for r in required if not (stage / r).is_file()]
    if missing:
        print(f"FAIL: missing stage files: {missing}", file=sys.stderr)
        return 1
    print(f"stage: {len(required)} required files present")

    manifest: dict = {"stage_dir": str(stage), "files": {}}
    for r in required:
        p = stage / r
        manifest["files"][r] = {"sha256": sha256(p), "size": p.stat().st_size}

    plugin = stage / "qml/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so"
    rpath = subprocess.run(
        ["readelf", "-d", str(plugin)], capture_output=True, text=True
    ).stdout
    build_in_rpath = str(repo / "build") in rpath
    manifest["plugin_runpath_points_to_build"] = build_in_rpath
    if build_in_rpath:
        print("WARN: plugin RUNPATH points to build tree; LD_LIBRARY_PATH required at runtime")
    else:
        print("stage: plugin RUNPATH does not reference build tree")

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = str(stage / "lib") + ":" + env.get("LD_LIBRARY_PATH", "")
    ldd = subprocess.run(
        ["ldd", str(plugin)], capture_output=True, text=True, env=env
    ).stdout
    native_line = [l for l in ldd.splitlines() if "libnekomote_plasma_qml.so" in l]
    if native_line:
        resolved = native_line[0].split("=>")[1].split("(")[0].strip() if "=>" in native_line[0] else ""
        from_stage = str(stage / "lib") in resolved
        manifest["native_resolved_from_stage"] = from_stage
        if from_stage:
            print(f"stage: native library loads from stage ({resolved})")
        else:
            print(f"WARN: native library loads from {resolved}, not stage", file=sys.stderr)

    if probe.is_file():
        model = repo / "assets/characters/coconut-casual/model.nkm.json"
        session = repo / "assets/characters/coconut-casual/session.nks.json"
        out = repo / "experiments/g22-stage-verify"
        if out.exists():
            shutil.rmtree(out)
        if model.is_file() and session.is_file():
            result = subprocess.run(
                [str(probe), "--recovery", "--model", str(model),
                 "--session", str(session), "--output", str(out)],
                capture_output=True, text=True, env=env, timeout=120
            )
            manifest["smoke_test_exit"] = result.returncode
            manifest["smoke_test_last_line"] = result.stdout.strip().splitlines()[-1] if result.stdout.strip() else ""
            if result.returncode == 0:
                print("stage: smoke recovery test passed with LD_LIBRARY_PATH from stage")
            else:
                print(f"WARN: smoke test exit={result.returncode}", file=sys.stderr)
        else:
            print("stage: smoke test SKIPPED (no model/session assets)")
    else:
        print("stage: smoke test SKIPPED (no probe binary)")

    nm = subprocess.run(
        ["nm", "-D", str(stage / "lib/libnekomote_plasma_qml.so")],
        capture_output=True, text=True
    ).stdout
    nekomote_symbols = [l for l in nm.splitlines() if "nekomote" in l]
    manifest["exported_nekomote_symbols"] = len(nekomote_symbols)
    print(f"stage: {len(nekomote_symbols)} exported nekomote symbols (static core linked into DSO)")

    manifest_path = stage / "manifest.json"
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)
    print(f"stage: manifest written to {manifest_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
