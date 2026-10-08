#!/usr/bin/env python3
"""G12: 195 项触摸诊断输入与覆盖账本.

对每个模型生成确定 seed、步数、origin、scale 的输入，通过
nekomote_session_probe 输出实际 touch_regions 并检查命中样本。
可选调用 nekomote_player --show-touch-regions --capture 生成覆盖图。

坐标约定：session_probe 的 touch 输入和 touch_regions 输出均为
中心原点输出像素、Y 向下。PNG 标注点转输入时减去半个画幅，不做镜像。
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ZONE_ORDER = ["bust", "eye", "face", "head", "pant"]
ZONE_COLORS = {
    "bust": "red",
    "eye": "yellow",
    "face": "green",
    "head": "cyan",
    "pant": "purple",
}
CONDITIONS = [
    {
        "name": "default_idle",
        "steps": 0,
        "origin": [0, 400],
        "scale": 1.0,
        "touch_step": 0,
    },
    {
        "name": "idle_120",
        "steps": 120,
        "origin": [0, 400],
        "scale": 1.0,
        "touch_step": 120,
    },
    {
        "name": "moved",
        "steps": 0,
        "origin": [80, 400],
        "scale": 1.0,
        "touch_step": 0,
    },
    {
        "name": "scaled_half",
        "steps": 0,
        "origin": [0, 400],
        "scale": 0.5,
        "touch_step": 0,
    },
]
RANDOM_SEED = 1
REACTION_SEED = 1
OUTSIDE_POINT = [99999.0, 99999.0]


def sha256_file(path: Path) -> str | None:
    if not path.exists():
        return None
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def git_head(repo: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=repo, text=True
        ).strip()
    except Exception:
        return "unknown"


def git_dirty(repo: Path) -> bool:
    try:
        out = subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=repo, text=True
        ).strip()
        return bool(out)
    except Exception:
        return True


def load_session(path: Path) -> dict | None:
    if not path.exists():
        return None
    with open(path) as f:
        d = json.load(f)
    return d.get("session", {})


def author_regions(session: dict) -> dict[str, dict | None]:
    inter = session.get("interaction", {}) or {}
    regions = inter.get("regions", {}) or {}
    result = {}
    for z in ZONE_ORDER:
        r = regions.get(z)
        result[z] = r if r is not None else None
    return result


def zone_reactions(session: dict) -> dict[str, list[str]]:
    result = {z: [] for z in ZONE_ORDER}
    for r in session.get("reactions", []):
        label = r.get("label", "")
        for z in r.get("zones", []):
            if z in result:
                result[z].append(label)
    return result


def run_probe(
    probe: str, model: Path, session: Path, request: dict, output: Path
) -> dict | None:
    input_file = output.with_suffix(".input.json")
    with open(input_file, "w") as f:
        json.dump(request, f)
    try:
        subprocess.run(
            [probe, str(model), str(session), str(input_file), str(output)],
            check=True,
            capture_output=True,
            text=True,
            timeout=30,
        )
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return None
    if not output.exists():
        return None
    with open(output) as f:
        return json.load(f)


def probe_regions(
    probe: str, model: Path, session: Path, cond: dict, tmp: Path
) -> dict | None:
    request = {
        "steps": 0,
        "random_seed": RANDOM_SEED,
        "reaction_seed": REACTION_SEED,
        "origin": cond["origin"],
        "scale": cond["scale"],
    }
    out = tmp / "regions.json"
    result = run_probe(probe, model, session, request, out)
    if result is None:
        return None
    return result.get("touch_regions", {})


def probe_touch(
    probe: str,
    model: Path,
    session: Path,
    cond: dict,
    point: list[float],
    tmp: Path,
    tag: str,
) -> dict | None:
    request = {
        "steps": cond["steps"],
        "random_seed": RANDOM_SEED,
        "reaction_seed": REACTION_SEED,
        "origin": cond["origin"],
        "scale": cond["scale"],
        "events": [
            {"step": cond["touch_step"], "kind": "touch", "point": point}
        ],
    }
    out = tmp / f"touch_{tag}.json"
    result = run_probe(probe, model, session, request, out)
    if result is None:
        return None
    samples = result.get("samples", [])
    idx = cond["touch_step"]
    if idx >= len(samples):
        idx = len(samples) - 1
    sample = samples[idx] if samples else {}
    return {
        "reaction": sample.get("reaction", ""),
        "phase": sample.get("phase", 0),
    }


def inside_point(center: list, radius: list) -> list[float]:
    return [
        center[0] + radius[0] * 0.3,
        center[1] + radius[1] * 0.3,
    ]


def outside_point_for_zone(center: list, radius: list) -> list[float]:
    return [
        center[0] + radius[0] * 1.5 + 10.0,
        center[1],
    ]


def find_zone_of_reaction(
    reaction_label: str, zone_reacts: dict[str, list[str]]
) -> str | None:
    if not reaction_label:
        return None
    for z in ZONE_ORDER:
        if reaction_label in zone_reacts.get(z, []):
            return z
    return None


def audit_condition(
    probe: str,
    model: Path,
    session_path: Path,
    cond: dict,
    zone_reacts: dict[str, list[str]],
    tmp: Path,
) -> dict:
    cond_result = {
        "name": cond["name"],
        "params": {
            "steps": cond["steps"],
            "origin": cond["origin"],
            "scale": cond["scale"],
            "random_seed": RANDOM_SEED,
            "reaction_seed": REACTION_SEED,
        },
        "projected_regions": {},
        "zone_tests": {},
        "outside_test": None,
        "errors": [],
    }

    regions = probe_regions(probe, model, session_path, cond, tmp)
    if regions is None:
        cond_result["errors"].append("failed to probe regions")
        return cond_result

    for z in ZONE_ORDER:
        r = regions.get(z)
        if r is not None:
            cond_result["projected_regions"][z] = r
        else:
            cond_result["projected_regions"][z] = None

    for z in ZONE_ORDER:
        r = regions.get(z)
        has_reactions = len(zone_reacts.get(z, [])) > 0
        if r is None or not has_reactions:
            cond_result["zone_tests"][z] = {
                "has_region": r is not None,
                "has_reactions": has_reactions,
                "reaction_count": len(zone_reacts.get(z, [])),
                "skipped": True,
            }
            continue

        center = r["center"]
        radius = r["radius"]

        center_hit = probe_touch(
            probe, model, session_path, cond, center, tmp, f"{z}_center"
        )
        inside_pt = inside_point(center, radius)
        inside_hit = probe_touch(
            probe, model, session_path, cond, inside_pt, tmp, f"{z}_inside"
        )

        center_zone = None
        inside_zone = None
        if center_hit:
            center_zone = find_zone_of_reaction(
                center_hit["reaction"], zone_reacts
            )
        if inside_hit:
            inside_zone = find_zone_of_reaction(
                inside_hit["reaction"], zone_reacts
            )

        cond_result["zone_tests"][z] = {
            "has_region": True,
            "has_reactions": True,
            "reaction_count": len(zone_reacts.get(z, [])),
            "projected_center": center,
            "projected_radius": radius,
            "center_test": {
                "point": center,
                "reaction": center_hit["reaction"] if center_hit else None,
                "hit_zone": center_zone,
                "expected_zone": z,
                "pass": center_zone is not None,
            },
            "inside_test": {
                "point": inside_pt,
                "reaction": inside_hit["reaction"] if inside_hit else None,
                "hit_zone": inside_zone,
                "expected_zone": z,
                "pass": inside_zone is not None,
            },
        }

    outside_hit = probe_touch(
        probe, model, session_path, cond, OUTSIDE_POINT, tmp, "outside"
    )
    outside_reaction = outside_hit["reaction"] if outside_hit else None
    cond_result["outside_test"] = {
        "point": OUTSIDE_POINT,
        "reaction": outside_reaction,
        "pass": not outside_reaction,
    }

    return cond_result


def capture_overlay(
    player: str,
    model: Path,
    session: Path,
    cond: dict,
    output_png: Path,
    width: int = 800,
    height: int = 1080,
) -> bool:
    cmd = [
        player,
        "--model",
        str(model),
        "--session",
        str(session),
        "--width",
        str(width),
        "--height",
        str(height),
        "--origin-x",
        str(cond["origin"][0]),
        "--origin-y",
        str(cond["origin"][1]),
        "--scale",
        str(cond["scale"]),
        "--show-touch-regions",
        "--transparent",
        "--mute",
        "--no-save",
        "--steps",
        str(cond["steps"]),
        "--capture",
        str(output_png),
    ]
    try:
        subprocess.run(cmd, check=True, capture_output=True, timeout=30)
        return output_png.exists()
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return False


def audit_model(
    probe: str,
    player: str | None,
    assets: Path,
    model_name: str,
    output_dir: Path,
    do_capture: bool,
) -> dict:
    model_dir = assets / "characters" / model_name
    model_path = model_dir / "model.nkm.json"
    session_path = model_dir / "session.nks.json"

    entry = {
        "model": model_name,
        "model_path": str(model_path.relative_to(assets)),
        "session_path": str(session_path.relative_to(assets)),
        "status": "OK",
        "mirror": None,
        "author_regions": {},
        "zone_reactions": {},
        "conditions": [],
        "errors": [],
    }

    if not model_path.exists():
        entry["status"] = "MISSING_MODEL"
        entry["errors"].append("model.nkm.json not found")
        return entry
    if not session_path.exists():
        entry["status"] = "MISSING_SESSION"
        entry["errors"].append("session.nks.json not found")
        return entry

    session_data = load_session(session_path)
    if session_data is None:
        entry["status"] = "INVALID_SESSION"
        entry["errors"].append("cannot read session data")
        return entry

    entry["author_regions"] = author_regions(session_data)
    zr = zone_reactions(session_data)
    entry["zone_reactions"] = {z: len(v) for z, v in zr.items()}

    active_zones = [
        z
        for z in ZONE_ORDER
        if entry["author_regions"][z] is not None and len(zr.get(z, [])) > 0
    ]
    if not active_zones:
        entry["status"] = "NO_ACTIVE_ZONES"
        entry["errors"].append("no zones with both region and reactions")
        return entry

    tmp = output_dir / "_tmp" / model_name
    tmp.mkdir(parents=True, exist_ok=True)

    first_cond = CONDITIONS[0]
    first_regions = probe_regions(
        probe, model_path, session_path, first_cond, tmp
    )
    if first_regions is None:
        entry["status"] = "CPU_REJECTED"
        entry["errors"].append(
            "session_probe failed to compile or start; CPU evaluation rejected"
        )
        shutil.rmtree(tmp, ignore_errors=True)
        return entry

    probe_file = tmp / "regions.json"
    if probe_file.exists():
        with open(probe_file) as f:
            entry["mirror"] = json.load(f).get("presentation_mirror")

    for cond in CONDITIONS:
        cr = audit_condition(
            probe, model_path, session_path, cond, zr, tmp
        )
        entry["conditions"].append(cr)
        if cr.get("errors"):
            entry["errors"].extend(
                f"{cond['name']}: {e}" for e in cr["errors"]
            )
        if cr.get("outside_test") and not cr["outside_test"]["pass"]:
            entry["errors"].append(
                f"{cond['name']}: outside point triggered reaction"
            )

    any_fail = False
    for cr in entry["conditions"]:
        for z in ZONE_ORDER:
            zt = cr.get("zone_tests", {}).get(z, {})
            if zt.get("skipped"):
                continue
            ct = zt.get("center_test", {})
            it = zt.get("inside_test", {})
            if not ct.get("pass") or not it.get("pass"):
                any_fail = True
    if any_fail and entry["status"] == "OK":
        entry["status"] = "FAIL"

    if do_capture and player:
        cap_dir = output_dir / "captures" / model_name
        cap_dir.mkdir(parents=True, exist_ok=True)
        for cond in CONDITIONS:
            png = cap_dir / f"{cond['name']}.png"
            ok = capture_overlay(
                player, model_path, session_path, cond, png
            )
            for cr in entry["conditions"]:
                if cr["name"] == cond["name"]:
                    cr["capture"] = str(png.name) if ok else None
                    cr["capture_ok"] = ok

    shutil.rmtree(tmp, ignore_errors=True)
    return entry


def main() -> int:
    ap = argparse.ArgumentParser(
        description="G12 touch diagnostic audit for 195 catalog models"
    )
    ap.add_argument("--assets", required=True, help="assets root directory")
    ap.add_argument("--probe", required=True, help="nekomote_session_probe binary")
    ap.add_argument("--player", help="nekomote_player binary (for captures)")
    ap.add_argument("--output", required=True, help="output directory")
    ap.add_argument("--model", help="audit only this model")
    ap.add_argument("--capture", action="store_true", help="generate overlay PNGs")
    ap.add_argument("--report", help="ledger JSON path (default: output/ledger.json)")
    args = ap.parse_args()

    assets = Path(args.assets).resolve()
    probe = Path(args.probe).resolve()
    player = Path(args.player).resolve() if args.player else None
    output_dir = Path(args.output).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    report_path = Path(args.report) if args.report else output_dir / "ledger.json"

    chars_dir = assets / "characters"
    if args.model:
        model_names = [args.model]
    else:
        model_names = sorted(
            d.name
            for d in chars_dir.iterdir()
            if d.is_dir() and (d / "session.nks.json").exists()
        )

    print(f"auditing {len(model_names)} model(s)...", file=sys.stderr)

    entries = []
    t0 = time.time()
    for i, name in enumerate(model_names):
        entry = audit_model(
            str(probe),
            str(player) if player else None,
            assets,
            name,
            output_dir,
            args.capture,
        )
        entries.append(entry)
        elapsed = time.time() - t0
        print(
            f"  [{i+1}/{len(model_names)}] {name}: {entry['status']} ({elapsed:.1f}s)",
            file=sys.stderr,
        )

    status_counts = {}
    for e in entries:
        s = e["status"]
        status_counts[s] = status_counts.get(s, 0) + 1

    repo = Path(__file__).resolve().parent.parent.parent
    tool_hash = sha256_file(Path(__file__).resolve())
    probe_hash = sha256_file(probe)
    player_hash = sha256_file(player) if player else None
    catalog_path = Path(
        "/run/media/chocola/DATA/Project/NekoWebShow/config/model-catalog.json"
    )
    catalog_hash = sha256_file(catalog_path)

    ledger = {
        "task": "G12",
        "generated_at": datetime.datetime.now(
            datetime.timezone.utc
        ).isoformat(),
        "head_commit": git_head(repo),
        "dirty": git_dirty(repo),
        "tool": str(Path(__file__).resolve()),
        "tool_sha256": tool_hash,
        "probe_binary": str(probe),
        "probe_sha256": probe_hash,
        "player_binary": str(player) if player else None,
        "player_sha256": player_hash,
        "catalog_sha256": catalog_hash,
        "conditions": [c["name"] for c in CONDITIONS],
        "random_seed": RANDOM_SEED,
        "reaction_seed": REACTION_SEED,
        "summary": {
            "total": len(entries),
            "status_counts": status_counts,
        },
        "models": entries,
    }

    with open(report_path, "w") as f:
        json.dump(ledger, f, indent=2, ensure_ascii=False)

    print(f"\nledger: {report_path}", file=sys.stderr)
    print(f"summary: {json.dumps(ledger['summary'], indent=2)}", file=sys.stderr)

    has_fail = any(e["status"] == "FAIL" for e in entries)
    return 1 if has_fail else 0


if __name__ == "__main__":
    sys.exit(main())
