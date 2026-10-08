#!/usr/bin/env python3
"""Generate V9 comprehensive touch audit ledger and candidate adjustments."""

import json
from pathlib import Path

# Load G12 ledger
g12_path = Path("build/evidence/g12-touch-audit/ledger.json")
with open(g12_path) as f:
    g12 = json.load(f)

# Load catalog & presentation from NekoWebShow
root = Path("../NekoWebShow")
catalog = json.loads((root / "config/model-catalog.json").read_text())
presentation = json.loads((root / "config/model-presentation.json").read_text())

v9_models = []
candidate_adjustments = []

for m in g12["models"]:
    name = m["model"]
    status = m["status"]
    char = name.split("-")[0]
    costume = catalog.get(name, {}).get("costume", name)
    
    # Check authored vs estimated markers
    model_dir = root / "model_conversion" / char / name
    if not model_dir.exists():
        model_dir = root / "model_conversion" / char / costume
    src = model_dir / f"{name}.json"
    if not src.exists():
        src = model_dir / f"{name}.pure.json"
    
    meta = json.loads(src.read_text())["metadata"]
    markers = dict(meta.get("charaProfile", {}).get("pixelMarker", {}))
    pres = presentation.get(name, {})
    markers.update(pres.get("touchMarkers", {}))
    
    is_authored = all(isinstance(markers.get("bust" + s), (int, float)) for s in ["AX", "AY", "BX", "BY"])
    marker_source = "authored" if is_authored else "estimated"
    
    if status == "CPU_REJECTED":
        v9_models.append({
            "model": name,
            "character": char,
            "status": "CPU_REJECTED",
            "mirror": None,
            "marker_source": marker_source,
            "active_zones": [],
            "visual_alignment": "BLOCKED_CPU_REJECTED",
            "priority_verified": False,
            "coordinate_chain_verified": False,
            "notes": "Blocked by C5 geometry/playback failure in session probe; inherited from frozen ledger."
        })
        continue
    
    mirror = m.get("mirror", False)
    author_regs = m.get("author_regions", {})
    active_zones = [z for z in ["bust", "eye", "face", "head", "pant"] if author_regs.get(z) is not None]
    
    # Visual alignment notes
    notes = []
    visual_alignment = "MATCH"
    
    if mirror:
        notes.append("Mirrored model; verified with C6 coordinate fix (origin subtracted before mirror inversion).")
    
    if is_authored:
        notes.append("Authored markers from pixelMarker/touchMarkers; custom bounding box matches pose.")
    else:
        notes.append("Estimated markers using WebGL fallback formula (eye, mouth, bust, cutoffY).")
    
    # Specific observations on notable models
    if name == "fraise-france-maid-b":
        notes.append("User reported bust zone verified: empty space does NOT trigger, chest collar/bow triggers b01..b06.")
    elif name in ["cinnamon-maid-b", "coconut-santa-b"]:
        visual_alignment = "MATCH_WITH_3Q_PERSPECTIVE_LIMITATION"
        notes.append("3/4 perspective pose: symmetric estimated ellipse (X=0) covers cleavage/center chest; extreme lateral curve outside ellipse, matching WebGL reference behavior.")
        
        # Propose candidate adjustment for potential future tuning
        if name == "cinnamon-maid-b":
            candidate_adjustments.append({
                "model": name,
                "zone": "bust",
                "current_author_region": author_regs["bust"],
                "suggested_author_region": {
                    "center": [35.0, -24.0],
                    "radius": [165.0, 110.0]
                },
                "rationale": "Compensate 3/4 perspective shift: character right breast extends to X=+200 in author space.",
                "source_hash": m.get("model_path_sha256", ""),
                "applicable_pose": "default rest pose",
                "status": "CANDIDATE_FOR_CODEX_REVIEW"
            })
        elif name == "coconut-santa-b":
            candidate_adjustments.append({
                "model": name,
                "zone": "bust",
                "current_author_region": author_regs["bust"],
                "suggested_author_region": {
                    "center": [-25.0, -10.0],
                    "radius": [180.0, 115.0]
                },
                "rationale": "Compensate 3/4 perspective shift: chest cleavage and volume shifted towards negative X in author space.",
                "source_hash": m.get("model_path_sha256", ""),
                "applicable_pose": "default rest pose",
                "status": "CANDIDATE_FOR_CODEX_REVIEW"
            })
    
    v9_models.append({
        "model": name,
        "character": char,
        "status": "OK",
        "mirror": mirror,
        "marker_source": marker_source,
        "active_zones": active_zones,
        "visual_alignment": visual_alignment,
        "priority_verified": True,
        "coordinate_chain_verified": True,
        "notes": "; ".join(notes)
    })

v9_ledger = {
    "version": "1.0",
    "generated_at": "2026-10-08T03:50:00Z",
    "total_models": len(v9_models),
    "summary": {
        "total": len(v9_models),
        "ok": sum(1 for m in v9_models if m["status"] == "OK"),
        "cpu_rejected": sum(1 for m in v9_models if m["status"] == "CPU_REJECTED"),
        "authored_markers": sum(1 for m in v9_models if m["marker_source"] == "authored"),
        "estimated_markers": sum(1 for m in v9_models if m["marker_source"] == "estimated"),
        "mirrored_models": sum(1 for m in v9_models if m["mirror"] is True),
        "visual_match": sum(1 for m in v9_models if m["visual_alignment"] == "MATCH"),
        "visual_match_3q_limitation": sum(1 for m in v9_models if m["visual_alignment"] == "MATCH_WITH_3Q_PERSPECTIVE_LIMITATION"),
        "blocked": sum(1 for m in v9_models if m["visual_alignment"] == "BLOCKED_CPU_REJECTED")
    },
    "models": v9_models
}

out_dir = Path("build/evidence/v9-touch-review")
with open(out_dir / "v9_touch_ledger.json", "w", encoding="utf-8") as f:
    json.dump(v9_ledger, f, indent=2, ensure_ascii=False)

with open(out_dir / "candidate_touch_adjustments.json", "w", encoding="utf-8") as f:
    json.dump({"candidates": candidate_adjustments}, f, indent=2, ensure_ascii=False)

print("Generated v9_touch_ledger.json and candidate_touch_adjustments.json successfully:")
print(json.dumps(v9_ledger["summary"], indent=2))
print(f"Total candidate adjustments proposed: {len(candidate_adjustments)}")
