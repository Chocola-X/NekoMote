#!/usr/bin/env python3
"""Audit the three Ubuntu 20.04 executables and resolved system dependencies."""
import json
import re
import subprocess
import sys
from pathlib import Path

from plasma_distribution import UBUNTU20_BASELINE, elf_info, version


def audit(stage):
    binaries = {}
    names = {"nekomote_player", "nekomote_import", "nekomote_abi_host"}
    if {path.name for path in (stage / "bin").iterdir()} != names:
        raise ValueError("native package must contain exactly the three requested programs")
    for name in sorted(names):
        binary = stage / "bin" / name
        info = elf_info(binary)
        if "libstdc++.so.6" not in info["needed"]:
            raise ValueError(f"C++ system runtime must be dynamically linked: {binary}")
        if any(name.startswith(("libQt", "libKF", "libPlasma")) for name in info["needed"]):
            raise ValueError(f"unexpected desktop/Qt dependency: {binary}")
        for family, required in info["versions"].items():
            if version(required) > version(UBUNTU20_BASELINE[family]):
                raise ValueError(f"{binary}: {family}_{required} exceeds Ubuntu20 baseline")
        result = subprocess.run(["ldd", "-r", str(binary)], capture_output=True, text=True,
                                check=True, timeout=30)
        if re.search(r"not found|undefined symbol", result.stdout + result.stderr):
            raise ValueError(f"unresolved dependencies: {binary}\n{result.stdout}{result.stderr}")
        info["ldd"] = result.stdout
        binaries[name] = info
    return {"baseline": "ubuntu20", "limits": UBUNTU20_BASELINE, "binaries": binaries,
            "scope": "ELF and dynamic-loader checks; no desktop/GPU/long-term acceptance"}


if __name__ == "__main__":
    try:
        print(json.dumps(audit(Path(sys.argv[1])), indent=2, sort_keys=True))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
