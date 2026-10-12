#!/usr/bin/env python3
"""Audit/package a staged public-Qt Plasma plugin; never change desktop settings.

Requires Python 3.10+ and binutils. check-host only examines trusted local builds
with the system dynamic loader (ldd); it does not load the plugin into Plasma.
"""

import argparse
import gzip
import hashlib
import io
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path


ELFS = (
    "lib/libnekomote_plasma_qml.so",
    "qml/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so",
)
REQUIRED = ELFS + (
    "qml/NekoMote/Wallpaper/qmldir",
    "qml/NekoMote/Wallpaper/nekomote_plasma_qml.qmltypes",
    "share/plasma/wallpapers/org.nekomote.wallpaper/metadata.json",
    "share/plasma/wallpapers/org.nekomote.wallpaper/contents/ui/main.qml",
    "share/plasma/wallpapers/org.nekomote.wallpaper/contents/ui/config.qml",
    "share/plasma/wallpapers/org.nekomote.wallpaper/contents/config/main.xml",
)
BASELINE = {"GLIBC": "2.36", "GLIBCXX": "3.4.30", "CXXABI": "1.3.13"}
MACHINES = {"Advanced Micro Devices X86-64": "x86_64", "AArch64": "aarch64"}


def version(value):
    return tuple(int(part) for part in value.split("."))


def run(*args, env=None):
    result = subprocess.run(args, capture_output=True, text=True, env=env, timeout=30)
    if result.returncode:
        raise ValueError(f"{' '.join(args)}: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def digest(path):
    with path.open("rb") as source:
        hasher = hashlib.sha256()
        for chunk in iter(lambda: source.read(65536), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def stage_files(stage):
    paths = []
    for path in sorted(stage.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"stage symlink rejected: {path}")
        if path.is_file():
            paths.append(path)
    return paths


def check_manifest(stage):
    manifest = json.loads((stage / "manifest.json").read_text())
    files = manifest.get("files", {})
    actual = {str(path.relative_to(stage)) for path in stage_files(stage)
              if path.name not in {"manifest.json", "compatibility.json"}}
    if actual != set(files):
        raise ValueError("stage manifest does not describe exactly the staged files")
    for relative, info in files.items():
        path = Path(relative)
        if path.is_absolute() or ".." in path.parts:
            raise ValueError(f"unsafe manifest path: {relative}")
        if digest(stage / path) != info.get("sha256"):
            raise ValueError(f"stage hash mismatch: {relative}")


def elf_info(path):
    header = run("readelf", "-h", str(path))
    machine = re.search(r"Machine:\s*(.+)", header).group(1).strip()
    if machine not in MACHINES:
        raise ValueError(f"unsupported ELF architecture: {machine}")
    dynamic = run("readelf", "-d", str(path))
    needed = re.findall(r"\(NEEDED\).*\[(.+?)\]", dynamic)
    if any("/" in name for name in needed):
        raise ValueError(f"non-relocatable DT_NEEDED in {path}")
    paths = re.findall(r"\((?:RUNPATH|RPATH)\).*\[(.*?)\]", dynamic)
    for entry in [entry for group in paths for entry in group.split(":")]:
        if entry and not (entry == "$ORIGIN" or entry.startswith("$ORIGIN/")):
            raise ValueError(f"non-relative RPATH in {path}: {entry}")
    symbols = run("readelf", "--dyn-syms", "--wide", str(path))
    undefined = "\n".join(line for line in symbols.splitlines() if " UND " in line)
    # Qt explicitly excludes its private API from its binary compatibility promise.
    if "PRIVATE_API" in undefined:
        raise ValueError(f"Qt private API dependency in {path}")
    if "GLIBC_PRIVATE" in undefined:
        raise ValueError(f"glibc private API dependency in {path}")
    qt_symbols = re.findall(r"\bQt_(6\.\d+)\b", undefined)
    requirements = {}
    for family in BASELINE:
        matches = re.findall(rf"\b{family}_([0-9]+(?:\.[0-9]+)+)\b", undefined)
        if matches:
            requirements[family] = max(matches, key=version)
    return {"architecture": MACHINES[machine], "needed": needed,
            "runpath": paths, "versions": requirements, "sha256": digest(path),
            "qt_symbol_minimum": max(qt_symbols, key=version) if qt_symbols else "6.0"}


def audit(stage, qt_version, baseline=False):
    stage = stage.resolve()
    if version(qt_version)[0] != 6:
        raise ValueError("only Qt 6 / Plasma 6 is supported")
    check_manifest(stage)
    missing = [name for name in REQUIRED if not (stage / name).is_file()]
    if missing:
        raise ValueError(f"missing required plugin package files: {missing}")
    for path in stage_files(stage):
        if path.read_bytes()[:4] == b"\x7fELF" and str(path.relative_to(stage)) not in ELFS:
            raise ValueError(f"unexpected bundled ELF (Qt/drivers/runtime must stay host-owned): {path}")
    binaries = {name: elf_info(stage / name) for name in ELFS}
    for info in binaries.values():
        if version(qt_version)[:2] < version(info["qt_symbol_minimum"]):
            raise ValueError("declared build Qt version is below the required Qt symbol version")
    architectures = {info["architecture"] for info in binaries.values()}
    if len(architectures) != 1:
        raise ValueError("staged binaries have different architectures")
    if "$ORIGIN/../../../lib" not in binaries[ELFS[1]]["runpath"]:
        raise ValueError("QML plugin must resolve its native library with $ORIGIN/../../../lib")
    for info in binaries.values():
        if any(name.startswith(("libKF", "libPlasma")) for name in info["needed"]):
            raise ValueError("public QML module must not depend on KF/Plasma C++ ABI")
    requirements = {}
    for family in BASELINE:
        values = [info["versions"][family] for info in binaries.values()
                  if family in info["versions"]]
        if values:
            requirements[family] = max(values, key=version)
    failures = [f"{family}_{required} > {BASELINE[family]}"
                for family, required in requirements.items()
                if version(required) > version(BASELINE[family])]
    if version(qt_version) > version("6.4.2"):
        failures.append(f"Qt {qt_version} > 6.4.2")
    if baseline and failures:
        raise ValueError("Debian 12 baseline rejected: " + "; ".join(failures))
    return {"schema": 1, "architecture": architectures.pop(),
            "qt_build_version": qt_version, "plasma_major": 6,
            "requirements": requirements, "binaries": binaries,
            "baseline": {"name": "debian12", "passed": not failures,
                         "limits": BASELINE, "failures": failures},
            "scope": "ELF/Qt compatibility preflight; not desktop/GPU acceptance"}


def host_qt_version(shell):
    # Use the Qt library actually resolved by plasmashell, not an unrelated qmake.
    libraries = run("ldd", shell)
    match = re.search(r"libQt6Core\.so\.6\s+=>\s+(\S+)", libraries)
    if not match:
        raise ValueError("plasmashell does not resolve libQt6Core.so.6")
    target = Path(match.group(1)).resolve()
    match = re.search(r"libQt6Core\.so\.(6\.\d+\.\d+)$", target.name)
    if not match:
        raise ValueError(f"cannot infer plasmashell Qt version from {target}; use --host-qt-version")
    return match.group(1)


def check_host(stage, host_qt=None, shell=None):
    metadata = json.loads((stage / "compatibility.json").read_text())
    actual = audit(stage, metadata["qt_build_version"])
    if actual != metadata:
        raise ValueError("compatibility metadata differs from audited staged binaries")
    machine = platform.machine()
    if machine != metadata["architecture"]:
        raise ValueError(f"architecture mismatch: host {machine}, bundle {metadata['architecture']}")
    shell = shell or shutil.which("plasmashell")
    if not shell:
        raise ValueError("plasmashell not found (Plasma 6 is required)")
    shell_version = run(shell, "--version")
    if not re.search(r"plasmashell\s+6\.", shell_version):
        raise ValueError(f"Plasma 6 required: {shell_version.strip()}")
    host_qt = host_qt or host_qt_version(shell)
    if version(host_qt)[0] != 6 or version(host_qt)[:2] < version(metadata["qt_build_version"])[:2]:
        raise ValueError(f"host Qt {host_qt} cannot load plugin built with Qt {metadata['qt_build_version']}")
    env = dict(os.environ)
    env.pop("LD_LIBRARY_PATH", None)
    env.pop("LD_PRELOAD", None)
    for name in ELFS:
        result = run("ldd", "-r", str(stage / name), env=env)
        if re.search(r"not found|undefined symbol", result):
            raise ValueError(f"host dependency/symbol failure for {name}:\n{result}")
    return {"passed": True, "architecture": machine, "qt": host_qt,
            "plasma": shell_version.strip(), "gpu_validation": "not performed"}


def package(stage, qt_version, output, baseline):
    metadata = audit(stage, qt_version, baseline)
    extras = {
        "stage/compatibility.json": json.dumps(metadata, indent=2, sort_keys=True).encode() + b"\n",
    }
    sources = {f"stage/{path.relative_to(stage)}": path for path in stage_files(stage)
               if path.name != "compatibility.json"}
    for name in ("plasma_distribution.py", "plasma_deploy.py"):
        sources[f"tools/{name}"] = Path(__file__).parent / name
    sources["README.md"] = Path(__file__).parent.parent / "docs/plasma-distribution.md"
    with output.open("wb") as raw, gzip.GzipFile(fileobj=raw, mode="wb", mtime=0, filename="") as compressed:
        with tarfile.open(fileobj=compressed, mode="w") as archive:
            for name in sorted(set(sources) | set(extras)):
                data = extras[name] if name in extras else sources[name].read_bytes()
                info = tarfile.TarInfo(f"nekomote-plasma/{name}")
                info.size = len(data)
                info.mode = 0o644
                info.mtime = 0
                archive.addfile(info, io.BytesIO(data))
    return {"archive": str(output), "sha256": digest(output), "compatibility": metadata}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("audit", "package", "check-host"):
        command = commands.add_parser(name)
        command.add_argument("stage", type=Path)
        if name != "check-host":
            command.add_argument("--qt-version", required=True, help="actual build Qt version")
            command.add_argument("--baseline-debian12", action="store_true")
        if name == "package":
            command.add_argument("--output", required=True, type=Path)
        elif name == "audit":
            command.add_argument("--output", type=Path)
        else:
            command.add_argument("--host-qt-version", help="only if actual plasmashell Qt cannot be detected")
            command.add_argument("--plasmashell", help="explicit trusted host executable")
    args = parser.parse_args()
    try:
        if args.command == "audit":
            result = audit(args.stage, args.qt_version, args.baseline_debian12)
            if args.output:
                args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        elif args.command == "package":
            result = package(args.stage, args.qt_version, args.output, args.baseline_debian12)
        else:
            result = check_host(args.stage, args.host_qt_version, args.plasmashell)
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
