#!/usr/bin/env python3
"""Audit/package a staged public-Qt Plasma plugin; never change desktop settings.

Requires Python 3.8+ and binutils. Runtime installation and host preflight
are owned by the standalone installer.py shipped in the release wrapper.
"""

import argparse
import gzip
import hashlib
import io
import json
import re
import subprocess
import sys
import tarfile
import tempfile
import zipfile
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


def package_kpackage(stage, qt_version, output, baseline=False, ubuntu20=False,
                     package_version="0.0.0"):
    """Export the audited build as one native, relocatable Plasma/Wallpaper ZIP."""
    if not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", package_version):
        raise ValueError("package version must be a numeric major.minor.patch")
    metadata = audit(stage, qt_version, baseline, ubuntu20)
    root = stage / "share/plasma/wallpapers/uk.nekopara.nekomote"
    files = {str(p.relative_to(root)): p.read_bytes() for p in root.rglob("*") if p.is_file()}
    for name in ("contents/ui/main.qml", "contents/ui/config.qml"):
        text = files[name].decode("utf-8")
        text, count = re.subn(r'^import NekoMote\.Wallpaper( 1\.0)?$',
                             lambda match: 'import "NekoMote/Wallpaper"' + (match[1] or ""),
                             text, flags=re.MULTILINE)
        if count != 1:
            raise ValueError("expected exactly one native module import in " + name)
        files[name] = text.encode("utf-8")
    module = stage / "qml/NekoMote/Wallpaper"
    for path in module.iterdir():
        if path.is_file():
            files["contents/ui/NekoMote/Wallpaper/" + path.name] = path.read_bytes()
    # Directory imports use a filesystem URI on Qt5 and on the Qt6.4 baseline.
    # Named-module/resource redirects belong to the source/distro stage only.
    name = "contents/ui/NekoMote/Wallpaper/qmldir"
    files[name] = re.sub(rb"(?m)^(?:module|prefer) [^\n]*(?:\n|$)", b"", files[name])
    # Preserve the audited $ORIGIN/../../../lib relationship, without ELF edits.
    files["contents/lib/libnekomote_plasma_qml.so"] = (stage / ELFS[0]).read_bytes()
    plugin_metadata = json.loads(files["metadata.json"])
    if (plugin_metadata.get("KPackageStructure") != "Plasma/Wallpaper" or
            plugin_metadata.get("KPlugin", {}).get("Id") != "uk.nekopara.nekomote"):
        raise ValueError("stage wallpaper identity/type mismatch")
    plugin_metadata["KPlugin"]["Version"] = package_version
    files["metadata.json"] = (json.dumps(plugin_metadata, indent=2, ensure_ascii=False) + "\n").encode()
    if "metadata.desktop" in files:
        files["metadata.desktop"] = re.sub(
            rb"(?m)^X-KDE-PluginInfo-Version=.*$",
            b"X-KDE-PluginInfo-Version=" + package_version.encode(), files["metadata.desktop"])
    for name in ("LICENSE", "THIRD_PARTY_NOTICES"):
        files["contents/" + name] = (stage / name).read_bytes()
    files["contents/compatibility.json"] = (json.dumps(metadata, indent=2, sort_keys=True) + "\n").encode()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    return {"archive": str(output), "sha256": digest(output), "compatibility": metadata}


def package_bundle(stage, qt_version, output, baseline=False, ubuntu20=False,
                   package_version="0.0.0"):
    """One release wrapper, containing exactly the Store payload and installer."""
    with tempfile.TemporaryDirectory(prefix="nekomote-bundle-") as temporary:
        payload = Path(temporary) / "wallpaper.zip"
        result = package_kpackage(stage, qt_version, payload, baseline, ubuntu20, package_version)
        metadata = result["compatibility"]
        name = f"nekomote-plasma{metadata['plasma_major']}-{metadata['architecture']}"
        zip_name = name + ".wallpaper.zip"
        files = {
            zip_name: payload.read_bytes(),
            zip_name + ".sha256": (result["sha256"] + "  " + zip_name + "\n").encode(),
            "installer.py": (Path(__file__).parent / "installer.py").read_bytes(),
        }
        with output.open("wb") as raw, gzip.GzipFile(fileobj=raw, mode="wb", mtime=0, filename="") as compressed:
            with tarfile.open(fileobj=compressed, mode="w") as archive:
                for relative, data in sorted(files.items()):
                    info = tarfile.TarInfo(name + "/" + relative)
                    info.size = len(data)
                    info.mode = 0o755 if relative == "installer.py" else 0o644
                    archive.addfile(info, io.BytesIO(data))
    return {"archive": str(output), "sha256": digest(output), "compatibility": metadata}


def verify_bundle(path, package_version):
    match = re.fullmatch(r"nekomote-plasma([56])-(x86_64|aarch64)\.tar\.gz", path.name)
    if not match:
        raise ValueError("unexpected Plasma bundle filename")
    name = path.name[:-7]
    zip_name = name + ".wallpaper.zip"
    expected = {name + "/" + entry for entry in (zip_name, zip_name + ".sha256", "installer.py")}
    with tarfile.open(path, "r:gz") as archive:
        members = archive.getmembers()
        if len(members) != 3 or {m.name for m in members} != expected or any(not m.isfile() for m in members):
            raise ValueError("bundle must contain exactly the wallpaper ZIP, checksum and installer")
        payload = archive.extractfile(name + "/" + zip_name).read()
        checksum = archive.extractfile(name + "/" + zip_name + ".sha256").read().decode().split()
        if checksum != [hashlib.sha256(payload).hexdigest(), zip_name]:
            raise ValueError("inner wallpaper checksum mismatch")
        if not archive.extractfile(name + "/installer.py").read():
            raise ValueError("empty installer")
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        plugin = json.loads(archive.read("metadata.json"))
        metadata = json.loads(archive.read("contents/compatibility.json"))
        if (plugin.get("KPackageStructure") != "Plasma/Wallpaper" or
                plugin.get("KPlugin", {}).get("Id") != "uk.nekopara.nekomote" or
                plugin["KPlugin"].get("Version") != package_version):
            raise ValueError("wallpaper identity/version does not match release")
        if metadata.get("plasma_major") != int(match[1]) or metadata.get("architecture") != match[2]:
            raise ValueError("wallpaper platform does not match bundle name")
        for source, destination in zip(ELFS, ("contents/lib/libnekomote_plasma_qml.so",
                "contents/ui/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so")):
            if hashlib.sha256(archive.read(destination)).hexdigest() != metadata["binaries"][source]["sha256"]:
                raise ValueError("wallpaper binary hash mismatch")
    return {"passed": True, "archive": str(path), "payload_sha256": checksum[0]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    verifier = commands.add_parser("verify-bundle")
    verifier.add_argument("archive", type=Path)
    verifier.add_argument("--package-version", required=True)
    for name in ("audit", "bundle", "kpackage"):
        command = commands.add_parser(name)
        command.add_argument("stage", type=Path)
        command.add_argument("--qt-version", required=True, help="actual build Qt version")
        baselines = command.add_mutually_exclusive_group()
        baselines.add_argument("--baseline-debian12", action="store_true")
        baselines.add_argument("--baseline-ubuntu20", action="store_true")
        if name in ("bundle", "kpackage"):
            command.add_argument("--output", required=True, type=Path)
            command.add_argument("--package-version", required=True)
        else:
            command.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "verify-bundle":
            result = verify_bundle(args.archive, args.package_version)
        elif args.command == "audit":
            result = audit(args.stage, args.qt_version, args.baseline_debian12, args.baseline_ubuntu20)
            if args.output:
                args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        else:
            exporter = package_bundle if args.command == "bundle" else package_kpackage
            result = exporter(args.stage, args.qt_version, args.output,
                              args.baseline_debian12, args.baseline_ubuntu20, args.package_version)
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError, tarfile.TarError, zipfile.BadZipFile,
            subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
