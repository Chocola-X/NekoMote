#!/usr/bin/env python3
"""Capture a selected native touch evidence set; capture success is not visual acceptance."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

REPRESENTATIVE_MODELS = [
    "fraise-france-maid-b", "fraise-maid-a", "fraise-casual-b", "fraise-swimsuit-b",
    "chocola-date-a", "chocola-casual", "chocola-maid", "vanilla-date-a",
    "vanilla-casual", "vanilla-maid", "azuki-casual", "azuki-santa", "maple-casual",
    "maple-maid", "cinnamon-casual", "cinnamon-maid-b", "coconut-casual",
    "coconut-santa-b", "milk-summer", "milk-winter",
]


def file_hash(path: Path) -> str | None:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def capture_model(name: str, assets: Path, player: Path, output: Path, lock: Path,
                  timeout: float, runner=subprocess.run) -> dict:
    model = assets / "characters" / name / "model.nkm.json"
    session = assets / "characters" / name / "session.nks.json"
    row = {"model": name, "model_path": str(model.resolve()), "model_sha256": file_hash(model),
           "session_path": str(session.resolve()), "session_sha256": file_hash(session),
           "status": "CAPTURED", "visual_alignment": "UNREVIEWED", "captures": []}
    if not model.is_file() or not session.is_file():
        row.update(status="MISSING_ASSET", errors=["model or session missing"])
        return row
    try:
        textures = json.loads(model.read_text()).get("model", {}).get("textures", [])
        texture_paths = [model.parent / texture["path"] for texture in textures]
        row["textures"] = [{"path": str(path.resolve()), "sha256": file_hash(path)}
                           for path in texture_paths]
        if any(item["sha256"] is None for item in row["textures"]):
            row.update(status="MISSING_ASSET", errors=["texture missing"])
            return row
    except (ValueError, OSError, KeyError, TypeError) as error:
        row.update(status="INVALID_ASSET", errors=[str(error)])
        return row
    for suffix, steps, overlay in (("clean", 0, False), ("overlay", 0, True),
                                    ("idle60_overlay", 60, True)):
        path = output / "captures" / f"{name}_{suffix}.png"
        command = ["flock", str(lock), str(player), "--model", str(model), "--session", str(session),
                   "--width", "800", "--height", "1080", "--origin-x", "0", "--origin-y", "400",
                   "--scale", "1", "--steps", str(steps), "--mute", "--transparent", "--no-save"]
        if overlay:
            command.append("--show-touch-regions")
        command += ["--capture", str(path)]
        result = {"condition": suffix, "steps": steps, "origin": [0, 400], "scale": 1,
                  "command": command, "returncode": None, "path": str(path.resolve()),
                  "sha256": None, "status": "FAILED", "errors": []}
        log = output / "logs" / f"{name}_{suffix}.log"
        try:
            process = runner(command, capture_output=True, text=True, timeout=timeout)
            result["returncode"] = process.returncode
            log.write_text(process.stdout + "\n" + process.stderr)
            if process.returncode != 0:
                result["errors"].append("capture command failed")
            elif not path.is_file():
                result["errors"].append("capture PNG missing")
            else:
                with Image.open(path) as png:
                    size = png.size
                    png.verify()
                if size != (800, 1080):
                    result["errors"].append("capture dimensions differ from requested target")
                else:
                    result.update(status="CAPTURED", sha256=file_hash(path), dimensions=list(size))
        except (subprocess.TimeoutExpired, OSError, ValueError) as error:
            result["errors"].append(str(error))
            log.write_text(str(error) + "\n")
        result.update(log=str(log.resolve()), log_sha256=file_hash(log))
        result["observed_file_sha256"] = file_hash(path)
        row["captures"].append(result)
    successful = sum(c["status"] == "CAPTURED" for c in row["captures"])
    row["status"] = "CAPTURED" if successful == 3 else "PARTIAL" if successful else "FAILED"
    resources = [(model, row["model_sha256"]), (session, row["session_sha256"])]
    resources += [(Path(item["path"]), item["sha256"]) for item in row["textures"]]
    if any(file_hash(path) != expected for path, expected in resources):
        row.update(status="FAILED", errors=["asset changed during capture"])
    # Derive crops only from successful current capture, never from a stale file.
    overlay_capture = row["captures"][1]
    if overlay_capture["status"] == "CAPTURED":
        row["crops"] = []
        with Image.open(overlay_capture["path"]) as png:
            for zone, box in (("head", (150, 150, 650, 650)), ("bust", (150, 650, 650, 1050))):
                crop = output / "crops" / f"{name}_{zone}_crop.png"
                png.crop(box).save(crop)
                row["crops"].append({"zone": zone, "box": list(box), "path": str(crop.resolve()),
                                     "sha256": file_hash(crop), "source_sha256": overlay_capture["sha256"]})
    return row


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--player", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--models", nargs="+", default=REPRESENTATIVE_MODELS)
    parser.add_argument("--lock", type=Path, default=Path("build/gpu-validation.lock"))
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output already exists; use a new evidence directory")
    if not args.player.is_file():
        parser.error("player binary missing")
    if args.timeout <= 0 or not args.timeout < float("inf"):
        parser.error("timeout must be finite and positive")
    if len(set(args.models)) != len(args.models) or any(
        not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", name) for name in args.models
    ):
        parser.error("model names invalid or duplicated")
    for directory in ("captures", "crops", "logs"):
        (args.output / directory).mkdir(parents=True)
    args.lock.parent.mkdir(parents=True, exist_ok=True)
    manifest = {"task": "V9 capture", "player_path": str(args.player.resolve()),
                "player_sha256": file_hash(args.player), "tool_sha256": file_hash(Path(__file__)),
                "command": [sys.executable, *sys.argv], "python_version": sys.version,
                "scope": "Native screenshots only; no visual review, hit acceptance, or WebGL comparison.",
                "models": [], "complete": False}
    destination = args.output / "capture_manifest.json"
    for name in args.models:
        row = capture_model(name, args.assets, args.player, args.output, args.lock, args.timeout)
        if file_hash(args.player) != manifest["player_sha256"]:
            row.update(status="FAILED", errors=["player binary changed during capture"])
        manifest["models"].append(row)
        destination.write_text(json.dumps(manifest, indent=2) + "\n")
    manifest["complete"] = True
    manifest["passed"] = all(row["status"] == "CAPTURED" for row in manifest["models"])
    destination.write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"requested": len(args.models), "passed": manifest["passed"]}))
    return 0 if manifest["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
