#!/usr/bin/env python3
"""Generate manifest.json for a staged Plasma deployment directory.

Records every file with sha256 and size, plus readelf RUNPATH/NEEDED for ELF
binaries. The manifest is consumed by the verifier and installer to detect
corruption, missing files, and build-tree RPATH leakage.
"""
import hashlib
import json
import subprocess
import sys
from pathlib import Path


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def elf_info(path: Path) -> dict:
    info = {}
    result = subprocess.run(
        ["readelf", "-d", str(path)], capture_output=True, text=True
    )
    needed = []
    runpath = ""
    for line in result.stdout.splitlines():
        stripped = line.strip()
        if "(NEEDED)" in stripped:
            needed.append(stripped.split("[")[1].split("]")[0])
        if "(RUNPATH)" in stripped:
            runpath = stripped.split("Library runpath: ")[1].strip()
        elif "(RPATH)" in stripped and not runpath:
            runpath = stripped.split("Library rpath: ")[1].strip()
    info["needed"] = needed
    info["runpath"] = runpath
    return info


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: plasma_stage_manifest.py <stage_dir>", file=sys.stderr)
        return 1
    stage = Path(sys.argv[1])
    if not stage.is_dir():
        print(f"FAIL: stage directory not found: {stage}", file=sys.stderr)
        return 1
    files = {}
    for p in sorted(stage.rglob("*")):
        if p.is_file() and p.name != "manifest.json" and not p.name.startswith("_"):
            rel = p.relative_to(stage).as_posix()
            entry = {"sha256": sha256(p), "size": p.stat().st_size}
            if p.suffix == ".so" or ".so." in p.name:
                entry["elf"] = elf_info(p)
            files[rel] = entry
    manifest = {
        "stage_dir": str(stage),
        "file_count": len(files),
        "files": files,
    }
    manifest_path = stage / "manifest.json"
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
    print(f"manifest: {len(files)} files written to {manifest_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
