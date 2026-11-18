#!/usr/bin/env python3
"""Audit standalone program packages against their declared Linux baseline."""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

from plasma_distribution import BASELINE, elf_info, version

ALMALINUX8_BASELINE = {"GLIBC": "2.28", "GLIBCXX": "3.4.25", "CXXABI": "1.3.11"}


def audit(stage, mode):
    binaries = {}
    if mode == "native":
        names = {"nekomote_player", "nekomote_import", "nekomote_audio_convert",
                 "nekomote_model_convert", "nekomote_png_recompress", "nekomote_abi_host"}
        baseline, limits = "almalinux8", ALMALINUX8_BASELINE
    elif mode == "layer-shell":
        names = {"nekomote_layer_shell"}
        baseline, limits = "debian12", BASELINE
    elif mode == "x11":
        names = {"nekomote_x11"}
        baseline, limits = "almalinux8", ALMALINUX8_BASELINE
    else:
        raise ValueError(f"unknown standalone package mode: {mode}")
    if {path.name for path in (stage / "bin").iterdir()} != names:
        raise ValueError("standalone package must contain exactly the requested programs")
    for name in sorted(names):
        binary = stage / "bin" / name
        info = elf_info(binary)
        if "libstdc++.so.6" not in info["needed"]:
            raise ValueError(f"C++ system runtime must be dynamically linked: {binary}")
        if any(name.startswith(("libQt", "libKF", "libPlasma")) for name in info["needed"]):
            raise ValueError(f"unexpected desktop/Qt dependency: {binary}")
        if any(library.startswith("libFLAC") for library in info["needed"]):
            raise ValueError(f"portable archives must statically link pinned FLAC: {binary}")
        for family, required in info["versions"].items():
            if version(required) > version(limits[family]):
                raise ValueError(f"{binary}: {family}_{required} exceeds {baseline} baseline")
        result = subprocess.run(["ldd", "-r", str(binary)], capture_output=True, text=True,
                                check=True, timeout=30)
        if re.search(r"not found|undefined symbol", result.stdout + result.stderr):
            raise ValueError(f"unresolved dependencies: {binary}\n{result.stdout}{result.stderr}")
        info["ldd"] = result.stdout
        binaries[name] = info
    return {"baseline": baseline, "limits": limits, "binaries": binaries,
            "scope": "ELF and dynamic-loader checks; no desktop/GPU/long-term acceptance"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    parser.add_argument("--mode", choices=("native", "layer-shell", "x11"), required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(audit(args.stage, args.mode), indent=2, sort_keys=True))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
