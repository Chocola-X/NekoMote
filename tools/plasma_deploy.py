#!/usr/bin/env python3
"""Complete Plasma wallpaper deployment tool for NekoMote.

Installs all components (KPackage + QML module + native library) to a prefix
rooted at XDG_DATA_HOME. Records every file with sha256 for verification and
clean uninstall. Rejects unsafe prefixes. Does NOT touch system paths, real
desktop settings, or shell processes.

Usage:
  plasma_deploy.py --install   [--stage DIR] [--prefix DIR]
  plasma_deploy.py --uninstall [--prefix DIR]
  plasma_deploy.py --status    [--prefix DIR]
  plasma_deploy.py --verify    [--prefix DIR]

Default stage:  build/plasma/adapters/plasma/stage
Default prefix: $XDG_DATA_HOME (or ~/.local/share if unset)
"""
import hashlib
import json
import os
import shutil
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent
DEFAULT_STAGE = REPO / "build/plasma/adapters/plasma/stage"
RECORD_NAME = ".install-record.json"
ENV_NAME = "env.sh"

UNSAFE_PREFIXES = {
    "/", "/usr", "/usr/local", "/etc", "/var", "/opt",
    "/bin", "/sbin", "/lib", "/lib64", "/boot", "/dev",
    "/proc", "/sys", "/root",
}

NEKOMOTE_COMPONENTS = [
    "nekomote/lib/libnekomote_plasma_qml.so",
    "nekomote/qml/NekoMote/Wallpaper/qmldir",
    "nekomote/qml/NekoMote/Wallpaper/nekomote_plasma_qml.qmltypes",
    "nekomote/qml/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so",
    "plasma/wallpapers/org.nekomote.wallpaper/metadata.json",
    "plasma/wallpapers/org.nekomote.wallpaper/contents/ui/main.qml",
    "plasma/wallpapers/org.nekomote.wallpaper/contents/ui/config.qml",
    "plasma/wallpapers/org.nekomote.wallpaper/contents/config/main.xml",
    "plasma/plasmoids/org.nekomote.validation/metadata.json",
    "plasma/plasmoids/org.nekomote.validation/contents/ui/main.qml",
]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def default_prefix() -> Path:
    xdg = os.environ.get("XDG_DATA_HOME")
    if xdg:
        return Path(xdg)
    return Path.home() / ".local" / "share"


def validate_prefix(prefix: Path) -> None:
    if not prefix.is_absolute():
        sys.exit(f"FAIL: prefix must be absolute: {prefix}")
    resolved = str(prefix.resolve())
    if resolved in UNSAFE_PREFIXES:
        sys.exit(f"FAIL: unsafe prefix rejected: {prefix} (resolves to {resolved})")
    for unsafe in UNSAFE_PREFIXES:
        if resolved.startswith(unsafe + "/"):
            sys.exit(f"FAIL: prefix under unsafe path {unsafe}: {prefix}")
    if not prefix.parent.exists():
        sys.exit(f"FAIL: prefix parent does not exist: {prefix.parent}")


def stage_to_prefix_map(stage: Path) -> list[tuple[Path, Path]]:
    """Return list of (stage_path, relative_dest) for all installable files."""
    mapping = [
        (stage / "lib/libnekomote_plasma_qml.so",
         "nekomote/lib/libnekomote_plasma_qml.so"),
        (stage / "qml/NekoMote/Wallpaper/qmldir",
         "nekomote/qml/NekoMote/Wallpaper/qmldir"),
        (stage / "qml/NekoMote/Wallpaper/nekomote_plasma_qml.qmltypes",
         "nekomote/qml/NekoMote/Wallpaper/nekomote_plasma_qml.qmltypes"),
        (stage / "qml/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so",
         "nekomote/qml/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so"),
    ]
    for p in sorted((stage / "share/plasma/wallpapers/org.nekomote.wallpaper").rglob("*")):
        if p.is_file():
            rel = p.relative_to(stage / "share")
            mapping.append((p, str(rel)))
    for p in sorted((stage / "share/plasma/plasmoids/org.nekomote.validation").rglob("*")):
        if p.is_file():
            rel = p.relative_to(stage / "share")
            mapping.append((p, str(rel)))
    return mapping


def write_env_script(prefix: Path) -> None:
    env_path = prefix / "nekomote" / ENV_NAME
    qml_path = prefix / "nekomote" / "qml"
    content = f"""# NekoMote Plasma wallpaper environment.
# Source this file to make the QML module discoverable:
#   source {env_path}
export QML_IMPORT_PATH="{qml_path}:${{QML_IMPORT_PATH:-}}"
"""
    env_path.write_text(content)


def do_install(stage: Path, prefix: Path) -> int:
    validate_prefix(prefix)
    if not stage.is_dir():
        sys.exit(f"FAIL: stage directory not found: {stage}\n"
                 f"  run: cmake --build build/plasma --target nekomote_plasma_stage")
    stage_manifest = stage / "manifest.json"
    if not stage_manifest.is_file():
        sys.exit(f"FAIL: stage manifest not found: {stage_manifest}")
    mapping = stage_to_prefix_map(stage)
    missing = [str(s) for s, _ in mapping if not s.is_file()]
    if missing:
        sys.exit(f"FAIL: missing stage files: {missing}")
    prefix.mkdir(parents=True, exist_ok=True)
    record: dict = {
        "install_time": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "stage_dir": str(stage),
        "prefix": str(prefix),
        "stage_manifest_sha256": sha256(stage_manifest),
        "files": {},
    }
    for src, rel in mapping:
        dst = prefix / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        record["files"][rel] = {"sha256": sha256(dst), "size": dst.stat().st_size}
    write_env_script(prefix)
    record["files"][f"nekomote/{ENV_NAME}"] = {
        "sha256": sha256(prefix / "nekomote" / ENV_NAME),
        "size": (prefix / "nekomote" / ENV_NAME).stat().st_size,
    }
    record_path = prefix / "nekomote" / RECORD_NAME
    record_path.write_text(json.dumps(record, indent=2, sort_keys=True))
    print(f"install: {len(record['files'])} files to {prefix}")
    print(f"  KPackage:  {prefix}/plasma/wallpapers/org.nekomote.wallpaper/")
    print(f"  Plasmoid:   {prefix}/plasma/plasmoids/org.nekomote.validation/")
    print(f"  Native lib: {prefix}/nekomote/lib/libnekomote_plasma_qml.so")
    print(f"  QML module: {prefix}/nekomote/qml/NekoMote/Wallpaper/")
    print(f"  Env script: {prefix}/nekomote/{ENV_NAME}")
    print(f"  Record:     {record_path}")
    return 0


def do_uninstall(prefix: Path) -> int:
    validate_prefix(prefix)
    record_path = prefix / "nekomote" / RECORD_NAME
    if not record_path.is_file():
        print(f"uninstall: no install record at {record_path}")
        return 0
    record = json.loads(record_path.read_text())
    removed = 0
    for rel in record.get("files", {}):
        p = prefix / rel
        if p.is_file():
            p.unlink()
            removed += 1
    for rel in sorted(record.get("files", {}), reverse=True):
        d = (prefix / rel).parent
        while d != prefix and d.exists() and not any(d.iterdir()):
            d.rmdir()
            d = d.parent
    record_path.unlink()
    print(f"uninstall: removed {removed} files from {prefix}")
    return 0


def do_status(prefix: Path) -> int:
    record_path = prefix / "nekomote" / RECORD_NAME
    if not record_path.is_file():
        print(f"status: not installed (no record at {record_path})")
        return 1
    record = json.loads(record_path.read_text())
    print(f"status: installed at {prefix}")
    print(f"  install_time: {record.get('install_time', '?')}")
    print(f"  stage_dir:    {record.get('stage_dir', '?')}")
    print(f"  files:        {len(record.get('files', {}))}")
    missing = []
    for rel in record.get("files", {}):
        if not (prefix / rel).is_file():
            missing.append(rel)
    if missing:
        print(f"  MISSING: {missing}")
        return 1
    print("  all files present")
    return 0


def do_verify(prefix: Path) -> int:
    record_path = prefix / "nekomote" / RECORD_NAME
    if not record_path.is_file():
        sys.exit(f"verify: no install record at {record_path}")
    record = json.loads(record_path.read_text())
    failures = []
    for rel, info in record.get("files", {}).items():
        p = prefix / rel
        if not p.is_file():
            failures.append(f"missing: {rel}")
            continue
        actual = sha256(p)
        if actual != info["sha256"]:
            failures.append(f"hash mismatch: {rel}")
    if failures:
        for f in failures:
            print(f"FAIL: {f}", file=sys.stderr)
        return 1
    print(f"verify: {len(record['files'])} files OK at {prefix}")
    return 0


def main() -> int:
    stage = Path(os.environ.get("NEKOMOTE_STAGE", DEFAULT_STAGE))
    prefix = Path(os.environ.get("NEKOMOTE_PREFIX", "")) if os.environ.get("NEKOMOTE_PREFIX") else default_prefix()
    action = None
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("--install", "--uninstall", "--status", "--verify"):
            action = a[2:]
        elif a == "--stage":
            stage = Path(args[i + 1]); i += 1
        elif a == "--prefix":
            prefix = Path(args[i + 1]); i += 1
        else:
            sys.exit(f"unknown option: {a}")
        i += 1
    if not action:
        sys.exit(__doc__)
    if action == "install":
        return do_install(stage, prefix)
    if action == "uninstall":
        return do_uninstall(prefix)
    if action == "status":
        return do_status(prefix)
    if action == "verify":
        return do_verify(prefix)
    return 1


if __name__ == "__main__":
    sys.exit(main())
