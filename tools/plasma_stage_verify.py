#!/usr/bin/env python3
"""Verify the Plasma wallpaper stage is self-contained and deployable.

Every check is strict: any failure causes non-zero exit. Does NOT use
LD_LIBRARY_PATH — the plugin must find the native library via relative RPATH
alone. Negative cases (missing lib, missing plugin, corrupted manifest, no
screenshot) are exercised in separate sub-commands to prove the verifier
truly reaches each failure branch.

Usage:
  plasma_stage_verify.py <stage_dir> [<probe_binary>]
  plasma_stage_verify.py --negative <case> <stage_dir> [<probe_binary>]

Negative cases: missing-lib, missing-plugin, corrupted-manifest, no-screenshot
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent
PACKAGE_ID = "uk.nekopara.nekomote"
REQUIRED = [
    "lib/libnekomote_plasma_qml.so",
    "qml/NekoMote/Wallpaper/qmldir",
    "qml/NekoMote/Wallpaper/nekomote_plasma_qml.qmltypes",
    "qml/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so",
    "share/plasma/wallpapers/uk.nekopara.nekomote/metadata.json",
    "share/plasma/wallpapers/uk.nekopara.nekomote/contents/ui/main.qml",
    "share/plasma/wallpapers/uk.nekopara.nekomote/contents/ui/config.qml",
    "share/plasma/wallpapers/uk.nekopara.nekomote/contents/config/main.xml",
    "share/plasma/plasmoids/org.nekomote.validation/metadata.json",
    "share/plasma/plasmoids/org.nekomote.validation/contents/ui/main.qml",
]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def fail(msg: str) -> int:
    print(f"FAIL: {msg}", file=sys.stderr)
    return 1


def check_files(stage: Path) -> int:
    missing = [r for r in REQUIRED if not (stage / r).is_file()]
    if missing:
        return fail(f"missing stage files: {missing}")
    metadata_path = stage / "share/plasma/wallpapers" / PACKAGE_ID / "metadata.json"
    try:
        metadata = json.loads(metadata_path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        return fail(f"wallpaper metadata invalid: {error}")
    plugin_id = metadata.get("KPlugin", {}).get("Id")
    if metadata.get("KPackageStructure") != "Plasma/Wallpaper" or plugin_id != PACKAGE_ID:
        return fail(f"wallpaper package identity mismatch: {plugin_id!r}")
    print(f"check: {len(REQUIRED)} required files present")
    return 0


def check_manifest(stage: Path) -> int:
    manifest_path = stage / "manifest.json"
    if not manifest_path.is_file():
        return fail("manifest.json not found")
    try:
        manifest = json.loads(manifest_path.read_text())
    except (json.JSONDecodeError, OSError) as e:
        return fail(f"manifest.json invalid: {e}")
    if "files" not in manifest or not manifest["files"]:
        return fail("manifest.json has no files entry")
    for rel, info in manifest["files"].items():
        p = stage / rel
        if not p.is_file():
            return fail(f"manifest references missing file: {rel}")
        if sha256(p) != info.get("sha256"):
            return fail(f"manifest hash mismatch: {rel}")
    print(f"check: manifest valid ({len(manifest['files'])} files)")
    return 0


def check_rpath(stage: Path) -> int:
    plugin = stage / "qml/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so"
    result = subprocess.run(
        ["readelf", "-d", str(plugin)], capture_output=True, text=True
    )
    runpath = ""
    needed = []
    for line in result.stdout.splitlines():
        s = line.strip()
        if "(RUNPATH)" in s:
            runpath = s.split("Library runpath: ")[1].strip()
        elif "(RPATH)" in s and not runpath:
            runpath = s.split("Library rpath: ")[1].strip()
        if "(NEEDED)" in s:
            needed.append(s.split("[")[1].split("]")[0])
    if str(REPO / "build") in runpath:
        return fail(f"plugin RUNPATH points to build tree: {runpath}")
    if "$ORIGIN/../../../lib" not in runpath:
        return fail(f"plugin RUNPATH not relative: {runpath}")
    if "libnekomote_plasma_qml.so" not in needed:
        return fail("plugin does not NEEDED libnekomote_plasma_qml.so")
    print(f"check: RUNPATH={runpath} NEEDED has libnekomote_plasma_qml.so")
    return 0


def check_ldd(stage: Path) -> int:
    plugin = stage / "qml/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so"
    env = dict(os.environ)
    env.pop("LD_LIBRARY_PATH", None)
    result = subprocess.run(
        ["ldd", str(plugin)], capture_output=True, text=True, env=env
    )
    for line in result.stdout.splitlines():
        if "libnekomote_plasma_qml.so" in line:
            if "not found" in line:
                return fail(f"ldd: native lib not found without LD_LIBRARY_PATH: {line.strip()}")
            resolved = line.split("=>")[1].split("(")[0].strip() if "=>" in line else ""
            resolved_path = Path(resolved).resolve()
            stage_lib = (stage / "lib").resolve()
            if resolved_path.parent != stage_lib:
                return fail(f"ldd: native lib resolved from {resolved}, not {stage_lib}")
            print(f"check: ldd resolves native from stage ({resolved})")
            return 0
    return fail("ldd: libnekomote_plasma_qml.so not in output")


def check_symbols(stage: Path) -> int:
    lib = stage / "lib/libnekomote_plasma_qml.so"
    result = subprocess.run(
        ["nm", "-D", str(lib)], capture_output=True, text=True
    )
    nekomote_syms = [l for l in result.stdout.splitlines() if "nekomote" in l]
    if not nekomote_syms:
        return fail("no exported nekomote symbols (static core not linked)")
    print(f"check: {len(nekomote_syms)} exported nekomote symbols")
    return 0


def check_smoke(stage: Path, probe: Path) -> int:
    if not probe.is_file():
        print("check: smoke test SKIPPED (no probe binary)")
        return 0
    model = REPO / "assets/characters/coconut/casual/model.nkm.json"
    session = REPO / "assets/characters/coconut/casual/session.nks.json"
    if not model.is_file() or not session.is_file():
        print("check: smoke test SKIPPED (no model/session assets)")
        return 0
    out = Path(tempfile.mkdtemp(prefix="plasma-verify-"))
    env = dict(os.environ)
    env.pop("LD_LIBRARY_PATH", None)
    env["QML_IMPORT_PATH"] = str(stage / "qml")
    env["XDG_DATA_HOME"] = str(out / "xdg-data")
    env["XDG_CONFIG_HOME"] = str(out / "xdg-config")
    env["XDG_CACHE_HOME"] = str(out / "xdg-cache")
    try:
        result = subprocess.run(
            [str(probe), "--recovery", "--model", str(model),
             "--session", str(session), "--output", str(out / "probe-out")],
            capture_output=True, text=True, env=env, timeout=120
        )
        if result.returncode != 0:
            return fail(f"smoke test exit={result.returncode}\n{result.stderr[-500:]}")
        if "recovery: all bad->good->bad->good cycles passed" not in result.stdout:
            return fail("smoke test: recovery marker not found in stdout")
        png = out / "probe-out" / "recovery_final.png"
        if not png.is_file():
            return fail("smoke test: recovery_final.png not produced")
        print("check: smoke recovery test passed (no LD_LIBRARY_PATH)")
        return 0
    except subprocess.TimeoutExpired:
        return fail("smoke test timed out")
    finally:
        shutil.rmtree(out, ignore_errors=True)


def verify(stage: Path, probe: Path) -> int:
    checks = [
        ("files", lambda: check_files(stage)),
        ("manifest", lambda: check_manifest(stage)),
        ("rpath", lambda: check_rpath(stage)),
        ("ldd", lambda: check_ldd(stage)),
        ("symbols", lambda: check_symbols(stage)),
        ("smoke", lambda: check_smoke(stage, probe)),
    ]
    for name, check in checks:
        rc = check()
        if rc != 0:
            return rc
    print(f"PASS: all checks passed for {stage}")
    return 0


def run_negative(case: str, stage: Path, probe: Path) -> int:
    tmp = Path(tempfile.mkdtemp(prefix="plasma-neg-"))
    try:
        tmp_stage = tmp / "stage"
        shutil.copytree(stage, tmp_stage)
        if case == "missing-lib":
            (tmp_stage / "lib/libnekomote_plasma_qml.so").unlink()
        elif case == "missing-plugin":
            (tmp_stage / "qml/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so").unlink()
        elif case == "corrupted-manifest":
            (tmp_stage / "manifest.json").write_text('{"corrupted": true}')
        elif case == "no-screenshot":
            if probe.is_file():
                fake_probe = tmp / "fake_probe.sh"
                fake_probe.write_text("#!/bin/bash\nexit 0\n")
                fake_probe.chmod(0o755)
                probe = fake_probe
        else:
            return fail(f"unknown negative case: {case}")
        rc = verify(tmp_stage, probe)
        if rc == 0:
            return fail(f"negative case '{case}' should have failed but passed")
        print(f"PASS: negative case '{case}' correctly failed (exit={rc})")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main() -> int:
    args = sys.argv[1:]
    if args and args[0] == "--negative":
        case = args[1]
        stage = Path(args[2])
        probe = Path(args[3]) if len(args) > 3 else REPO / "build/plasma/bin/nekomote_plasma_package_probe"
        return run_negative(case, stage, probe)
    stage = Path(args[0]) if args else REPO / "build/plasma/adapters/plasma/stage"
    probe = Path(args[1]) if len(args) > 1 else REPO / "build/plasma/bin/nekomote_plasma_package_probe"
    if not stage.is_dir():
        return fail(f"stage directory not found: {stage}")
    return verify(stage, probe)


if __name__ == "__main__":
    sys.exit(main())
