#!/usr/bin/env python3
"""Audit/package a staged public-Qt Plasma plugin; never change desktop settings.

Requires Python 3.8+ and binutils. check-host only examines trusted local builds
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
    "share/plasma/wallpapers/uk.nekopara.nekomote/metadata.json",
    "share/plasma/wallpapers/uk.nekopara.nekomote/contents/ui/main.qml",
    "share/plasma/wallpapers/uk.nekopara.nekomote/contents/ui/config.qml",
    "share/plasma/wallpapers/uk.nekopara.nekomote/contents/config/main.xml",
)
UBUNTU20_BASELINE = {"GLIBC": "2.31", "GLIBCXX": "3.4.28", "CXXABI": "1.3.12"}
BASELINE = {"GLIBC": "2.36", "GLIBCXX": "3.4.30", "CXXABI": "1.3.13"}
MACHINES = {"Advanced Micro Devices X86-64": "x86_64", "AArch64": "aarch64"}
# Some Qt5 builds version-tag the public nested Renderer exports as PRIVATE_API.
# Its Q_QUICK_EXPORT interface is identical in upstream 5.12/5.15 headers.
# Accept only these public exports, not arbitrary Qt5 private implementation APIs.
QT5_PUBLIC_RENDERER_EXPORTS = {
    "_ZN23QQuickFramebufferObject8RendererC2Ev",
    "_ZN23QQuickFramebufferObject8RendererD2Ev",
    "_ZNK23QQuickFramebufferObject8Renderer17framebufferObjectEv",
    "_ZTIN23QQuickFramebufferObject8RendererE",
    "_ZN23QQuickFramebufferObject8Renderer23createFramebufferObjectERK5QSize",
}


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
    public_qt5_exports = []
    for line in undefined.splitlines():
        if "PRIVATE_API" not in line:
            continue
        symbol = re.search(r"(\S+)@Qt_5_PRIVATE_API\b", line)
        if ("libQt5Quick.so.5" not in needed or not symbol or
                symbol.group(1) not in QT5_PUBLIC_RENDERER_EXPORTS):
            raise ValueError(f"Qt private API dependency in {path}")
        public_qt5_exports.append(symbol.group(1))
    if "GLIBC_PRIVATE" in undefined:
        raise ValueError(f"glibc private API dependency in {path}")
    qt_symbols = re.findall(r"\bQt_([56]\.\d+)\b", undefined)
    requirements = {}
    for family in BASELINE:
        matches = re.findall(rf"\b{family}_([0-9]+(?:\.[0-9]+)+)\b", undefined)
        if matches:
            requirements[family] = max(matches, key=version)
    return {"architecture": MACHINES[machine], "needed": needed,
            "runpath": paths, "versions": requirements, "sha256": digest(path),
            "qt_symbol_minimum": max(qt_symbols, key=version) if qt_symbols else "0.0",
            "public_qt5_renderer_exports": sorted(public_qt5_exports)}


def audit(stage, qt_version, baseline=False, ubuntu20=False):
    stage = stage.resolve()
    major = version(qt_version)[0]
    if major not in (5, 6):
        raise ValueError("only Qt 5 / Plasma 5 and Qt 6 / Plasma 6 are supported")
    if (baseline and major != 6) or (ubuntu20 and major != 5) or (baseline and ubuntu20):
        raise ValueError("baseline must match the Qt/Plasma major")
    limits = UBUNTU20_BASELINE if major == 5 else BASELINE
    qt_limit = "5.12.8" if major == 5 else "6.4.2"
    baseline_name = "ubuntu20" if major == 5 else "debian12"
    check_manifest(stage)
    required_files = list(REQUIRED)
    if major == 5:
        required_files.remove("qml/NekoMote/Wallpaper/nekomote_plasma_qml.qmltypes")
        required_files.append("share/plasma/wallpapers/uk.nekopara.nekomote/metadata.desktop")
    missing = [name for name in required_files if not (stage / name).is_file()]
    if missing:
        raise ValueError(f"missing required plugin package files: {missing}")
    for path in stage_files(stage):
        if path.read_bytes()[:4] == b"\x7fELF" and str(path.relative_to(stage)) not in ELFS:
            raise ValueError(f"unexpected bundled ELF (Qt/drivers/runtime must stay host-owned): {path}")
    binaries = {name: elf_info(stage / name) for name in ELFS}
    for info in binaries.values():
        if any(name.startswith(f"libQt{11 - major}") for name in info["needed"]):
            raise ValueError("binary Qt major does not match the declared build Qt")
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
    failures = [f"{family}_{required} > {limits[family]}"
                for family, required in requirements.items()
                if version(required) > version(limits[family])]
    if version(qt_version) > version(qt_limit):
        failures.append(f"Qt {qt_version} > {qt_limit}")
    if (baseline or ubuntu20) and failures:
        raise ValueError(f"{baseline_name} baseline rejected: " + "; ".join(failures))
    return {"schema": 1, "architecture": architectures.pop(),
            "qt_build_version": qt_version, "plasma_major": major,
            "requirements": requirements, "binaries": binaries,
            "baseline": {"name": baseline_name, "passed": not failures,
                         "limits": limits, "failures": failures},
            "scope": "ELF/Qt compatibility preflight; not desktop/GPU acceptance"}


def host_qt_version(shell, major=6):
    # Use the Qt library actually resolved by plasmashell, not an unrelated qmake.
    libraries = run("ldd", shell)
    match = re.search(rf"libQt{major}Core\.so\.{major}\s+=>\s+(\S+)", libraries)
    if not match:
        raise ValueError(f"plasmashell does not resolve libQt{major}Core.so.{major}")
    target = Path(match.group(1)).resolve()
    match = re.search(rf"libQt{major}Core\.so\.({major}\.\d+\.\d+)$", target.name)
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
    major = metadata["plasma_major"]
    shell = shell or shutil.which("plasmashell")
    if not shell:
        raise ValueError(f"plasmashell not found (Plasma {major} is required)")
    shell_version = run(shell, "--version")
    if not re.search(rf"plasmashell\s+{major}\.", shell_version):
        raise ValueError(f"Plasma {major} required: {shell_version.strip()}")
    host_qt = host_qt or host_qt_version(shell, major)
    if version(host_qt)[0] != major or version(host_qt)[:2] < version(metadata["qt_build_version"])[:2]:
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


def package(stage, qt_version, output, baseline, ubuntu20=False):
    metadata = audit(stage, qt_version, baseline, ubuntu20)
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
            baselines = command.add_mutually_exclusive_group()
            baselines.add_argument("--baseline-debian12", action="store_true")
            baselines.add_argument("--baseline-ubuntu20", action="store_true")
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
            result = audit(args.stage, args.qt_version, args.baseline_debian12, args.baseline_ubuntu20)
            if args.output:
                args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        elif args.command == "package":
            result = package(args.stage, args.qt_version, args.output, args.baseline_debian12, args.baseline_ubuntu20)
        else:
            result = check_host(args.stage, args.host_qt_version, args.plasmashell)
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
