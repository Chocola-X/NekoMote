#!/usr/bin/env python3
"""Write the project license and consolidated third-party notices for CI packages."""

import argparse
from pathlib import Path
import textwrap
import xml.etree.ElementTree as ET


def write_licenses(source, build, stage, mode):
    dependencies = (
        ("nlohmann_json", "LICENSE.MIT"), ("fast_float", "LICENSE-MIT"),
        ("stb", "LICENSE"), ("glad", "LICENSE"),
        ("flac", "COPYING.Xiph"),
    )
    notices = ["Third-party notices for components included in NekoMote.\n"
               "The project license is provided separately in LICENSE.\n"]
    for dependency, filename in dependencies:
        notices.append(f"=== {dependency} ===\n\n" +
                       (build / "_deps" / f"{dependency}-src" / filename).read_text())
    if mode == "native20":
        notices.append("=== GLFW ===\n\n" + (build / "_deps/glfw-src/LICENSE.md").read_text())
    for name in ("Apache-2.0", "CC0-1.0"):
        notices.append(f"=== GLAD generated code: {name} ===\n\n" +
                       (source / f"tools/licenses/{name}.txt").read_text())
    if mode == "layer-shell":
        for name in ("wlr-layer-shell-unstable-v1", "xdg-shell"):
            xml = source / f"adapters/layer_shell/protocols/{name}.xml"
            notice = ET.parse(xml).getroot().findtext("copyright")
            if not notice or not notice.strip():
                raise ValueError(f"missing protocol copyright: {xml}")
            notices.append(f"=== {name} ===\n\n" + textwrap.dedent(notice).strip())
    # Read every input before writing a package, so missing notices fail closed.
    project_license = (source / "LICENSE").read_bytes()
    stage.mkdir(parents=True, exist_ok=True)
    (stage / "LICENSE").write_bytes(project_license)
    (stage / "THIRD_PARTY_NOTICES").write_text("\n\n".join(notices) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("build", type=Path)
    parser.add_argument("stage", type=Path)
    parser.add_argument("mode", choices=("native20", "plasma5", "plasma6", "layer-shell"))
    args = parser.parse_args()
    write_licenses(args.source, args.build, args.stage, args.mode)
