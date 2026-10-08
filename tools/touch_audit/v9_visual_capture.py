#!/usr/bin/env python3
"""V9: Representative models capture and crop script."""

import os
import subprocess
import sys
from pathlib import Path
from PIL import Image

REPRESENTATIVE_MODELS = [
    # fraise
    ("fraise-france-maid-b", True, "estimated"),
    ("fraise-maid-a", False, "estimated"),
    ("fraise-casual-b", True, "estimated"),
    ("fraise-swimsuit-b", True, "estimated"),
    # chocola
    ("chocola-date-a", False, "estimated"),
    ("chocola-casual", False, "authored"),
    ("chocola-maid", False, "authored"),
    # vanilla
    ("vanilla-date-a", False, "estimated"),
    ("vanilla-casual", False, "authored"),
    ("vanilla-maid", False, "authored"),
    # azuki
    ("azuki-casual", False, "authored"),
    ("azuki-santa", False, "estimated"),
    # maple
    ("maple-casual", False, "authored"),
    ("maple-maid", False, "authored"),
    # cinnamon
    ("cinnamon-casual", False, "authored"),
    ("cinnamon-maid-b", False, "estimated"),
    # coconut
    ("coconut-casual", False, "authored"),
    ("coconut-santa-b", False, "estimated"),
    # milk
    ("milk-summer", False, "estimated"),
    ("milk-winter", False, "estimated"),
]

OUT_DIR = Path("build/evidence/v9-touch-review")
CAPTURES_DIR = OUT_DIR / "captures"
CROPS_DIR = OUT_DIR / "crops"
CAPTURES_DIR.mkdir(parents=True, exist_ok=True)
CROPS_DIR.mkdir(parents=True, exist_ok=True)

PLAYER = Path("build/release/bin/nekomote_player")

def run_cmd(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        print(f"Error running {' '.join(cmd)}: {p.stderr}")
        return False
    return True

def capture_model(model_name, mirror, marker_type):
    model_path = Path(f"assets/characters/{model_name}/model.nkm.json")
    session_path = Path(f"assets/characters/{model_name}/session.nks.json")
    if not model_path.exists() or not session_path.exists():
        print(f"Skipping {model_name}: missing asset")
        return

    clean_png = CAPTURES_DIR / f"{model_name}_clean.png"
    overlay_png = CAPTURES_DIR / f"{model_name}_overlay.png"
    idle60_overlay_png = CAPTURES_DIR / f"{model_name}_idle60_overlay.png"

    # Clean capture
    cmd_clean = [
        "flock", "-n", "build/gpu-validation.lock",
        str(PLAYER),
        "--model", str(model_path),
        "--session", str(session_path),
        "--width", "800", "--height", "1080",
        "--origin-x", "0", "--origin-y", "400",
        "--scale", "1",
        "--steps", "0",
        "--mute", "--transparent",
        "--capture", str(clean_png)
    ]
    run_cmd(cmd_clean)

    # Overlay capture
    cmd_overlay = [
        "flock", "-n", "build/gpu-validation.lock",
        str(PLAYER),
        "--model", str(model_path),
        "--session", str(session_path),
        "--width", "800", "--height", "1080",
        "--origin-x", "0", "--origin-y", "400",
        "--scale", "1",
        "--steps", "0",
        "--mute", "--transparent",
        "--show-touch-regions",
        "--capture", str(overlay_png)
    ]
    run_cmd(cmd_overlay)

    # Idle step 60 capture
    cmd_idle = [
        "flock", "-n", "build/gpu-validation.lock",
        str(PLAYER),
        "--model", str(model_path),
        "--session", str(session_path),
        "--width", "800", "--height", "1080",
        "--origin-x", "0", "--origin-y", "400",
        "--scale", "1",
        "--steps", "60",
        "--mute", "--transparent",
        "--show-touch-regions",
        "--capture", str(idle60_overlay_png)
    ]
    run_cmd(cmd_idle)

    # Crop head/face and bust for overlay
    if overlay_png.exists():
        im = Image.open(overlay_png)
        # Head/face crop: (150, 150, 650, 650)
        im_head = im.crop((150, 150, 650, 650))
        im_head.save(CROPS_DIR / f"{model_name}_head_crop.png")

        # Bust crop: (150, 650, 650, 1050)
        im_bust = im.crop((150, 650, 650, 1050))
        im_bust.save(CROPS_DIR / f"{model_name}_bust_crop.png")

    print(f"Captured and cropped {model_name} (mirror={mirror}, {marker_type})")

def main():
    for name, mirror, marker_type in REPRESENTATIVE_MODELS:
        capture_model(name, mirror, marker_type)

if __name__ == "__main__":
    main()
