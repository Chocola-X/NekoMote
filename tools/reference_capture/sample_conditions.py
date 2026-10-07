#!/usr/bin/env python3
"""
tools/reference_capture/sample_conditions.py

Performs controlled condition sampling of WebGL reference player draw calls
for bm=19 (fraise-maid-a) and attr=2 (fraise-france-maid-b) across:
- Baseline (steps=0, opacity=1.0)
- Opacity variation (opacity=0.5)
- Independent pose variations (head_LR, head_UD, head_slant, body_LR, body_UD, body_slant, act_sp)
- Fixed step progression (steps=30)
- Unrelated parameter invariance tests (face parameters for ribbon, head parameters for torso)

Records for each selected draw call:
- colorMask
- blendEquationSeparate
- FBO binding & attachment
- shader sources & uniforms
- effective opacity
- vertex transformation & bounding box
- primitive geometry
- command line, coordinates, seed, step count, source hashes
"""

import argparse
import fcntl
import hashlib
import json
import os
import pathlib
import subprocess
import sys
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent

def sha256_file(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

BM19_CASES = [
    {
        "id": "fraise-maid-a_base",
        "model": "fraise-maid-a",
        "description": "Baseline initial state (steps=0, opacity=1.0, default pose)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {}
    },
    {
        "id": "fraise-maid-a_opacity05",
        "model": "fraise-maid-a",
        "description": "Global opacity variation (opacity=0.5)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 0.5,
        "steps": 0, "variables": {}
    },
    {
        "id": "fraise-maid-a_head_lr30",
        "model": "fraise-maid-a",
        "description": "Head yaw right (head_LR = 30)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {"head_LR": 30}
    },
    {
        "id": "fraise-maid-a_head_lr_minus30",
        "model": "fraise-maid-a",
        "description": "Head yaw left (head_LR = -30)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {"head_LR": -30}
    },
    {
        "id": "fraise-maid-a_head_ud30",
        "model": "fraise-maid-a",
        "description": "Head pitch down (head_UD = 30)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {"head_UD": 30}
    },
    {
        "id": "fraise-maid-a_head_slant30",
        "model": "fraise-maid-a",
        "description": "Head roll right (head_slant = 30)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {"head_slant": 30}
    },
    {
        "id": "fraise-maid-a_body_lr30",
        "model": "fraise-maid-a",
        "description": "Body yaw right (body_LR = 30)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {"body_LR": 30}
    },
    {
        "id": "fraise-maid-a_act_sp30",
        "model": "fraise-maid-a",
        "description": "Special action parameter (act_sp = 30)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {"act_sp": 30}
    },
    {
        "id": "fraise-maid-a_step30",
        "model": "fraise-maid-a",
        "description": "Fixed step progression (steps=30, default update)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 30, "variables": {}
    },
    {
        "id": "fraise-maid-a_face_invariance",
        "model": "fraise-maid-a",
        "description": "Facial parameter variation limitation test (eye/mouth/eyebrow)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {"face_eye_open": 10, "face_mouth": 30, "face_eyebrow": 20}
    }
]

ATTR2_CASES = [
    {
        "id": "fraise-france-maid-b_base",
        "model": "fraise-france-maid-b",
        "description": "Baseline initial state (steps=0, opacity=1.0, default pose)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {}
    },
    {
        "id": "fraise-france-maid-b_opacity05",
        "model": "fraise-france-maid-b",
        "description": "Global opacity variation (opacity=0.5)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 0.5,
        "steps": 0, "variables": {}
    },
    {
        "id": "fraise-france-maid-b_body_lr30",
        "model": "fraise-france-maid-b",
        "description": "Torso yaw right (body_LR = 30)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {"body_LR": 30}
    },
    {
        "id": "fraise-france-maid-b_body_lr_minus30",
        "model": "fraise-france-maid-b",
        "description": "Torso yaw left (body_LR = -30)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {"body_LR": -30}
    },
    {
        "id": "fraise-france-maid-b_body_ud30",
        "model": "fraise-france-maid-b",
        "description": "Torso pitch down (body_UD = 30)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {"body_UD": 30}
    },
    {
        "id": "fraise-france-maid-b_body_slant30",
        "model": "fraise-france-maid-b",
        "description": "Torso roll right (body_slant = 30)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {"body_slant": 30}
    },
    {
        "id": "fraise-france-maid-b_act_sp30",
        "model": "fraise-france-maid-b",
        "description": "Special action parameter (act_sp = 30)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {"act_sp": 30}
    },
    {
        "id": "fraise-france-maid-b_step30",
        "model": "fraise-france-maid-b",
        "description": "Fixed step progression (steps=30, default update)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 30, "variables": {}
    },
    {
        "id": "fraise-france-maid-b_head_invariance",
        "model": "fraise-france-maid-b",
        "description": "Head parameter variation limitation test (head_LR/head_UD on torso decoration)",
        "width": 800, "height": 1080, "x": 0, "y": 400, "scale": 1.0, "opacity": 1.0,
        "steps": 0, "variables": {"head_LR": 30, "head_UD": 30, "face_eye_open": 10}
    }
]

def find_target_draw(traces, target_kind):
    """
    Finds the target draw call matching the icon's UV coordinates.
    - bm19: icon 0105 in fraise-maid-a (left: 1758, top: 1395, width: 298, height: 124, atlas 4096x2048)
      U in [1750, 2060], V in [525, 660]
    - attr2: icon 0029 in fraise-france-maid-b (left: 4080, top: 184, width: 11, height: 11, atlas 4096x4096)
      U in [4075, 4095], V in [3895, 3915]
    """
    matches = []
    for idx, d in enumerate(traces):
        if not d.get('framebuffer'):
            continue
        attrs = d.get('attributes', {})
        if 'a_texCoord' not in attrs:
            continue
        uv_data = attrs['a_texCoord'].get('data', [])
        if not uv_data:
            continue
        u_vals = [pt[0] for pt in uv_data]
        v_vals = [pt[1] for pt in uv_data]
        min_u, max_u = min(u_vals), max(u_vals)
        min_v, max_v = min(v_vals), max(v_vals)

        if target_kind == 'bm19':
            # U in 1750..2060, V in 525..660
            if abs(min_u - 1758) < 5 and abs(max_u - 2056) < 5 and abs(min_v - 529) < 5 and abs(max_v - 653) < 5:
                matches.append((idx, d, (min_u, max_u, min_v, max_v)))
        elif target_kind == 'attr2':
            # U in 4080..4091, V in 3901..3912
            if abs(min_u - 4080) < 5 and abs(max_u - 4091) < 5 and abs(min_v - 3901) < 5 and abs(max_v - 3912) < 5:
                matches.append((idx, d, (min_u, max_u, min_v, max_v)))

    return matches

def run_capture(case, out_dir: pathlib.Path, ref_root: pathlib.Path, browser: pathlib.Path):
    out_stem = out_dir / case['id']
    cmd = [
        sys.executable, str(HERE / 'capture.py'),
        '--reference-root', str(ref_root),
        '--model', case['model'],
        '--browser', str(browser),
        '--output', str(out_stem),
        '--width', str(case['width']),
        '--height', str(case['height']),
        '--x', str(case['x']),
        '--y', str(case['y']),
        '--scale', str(case['scale']),
        '--opacity', str(case['opacity']),
        '--steps', str(case['steps']),
        '--trace'
    ]
    if case.get('variables'):
        cmd.extend(['--variables', json.dumps(case['variables'])])

    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Capture failed for {case['id']}: {res.stderr}\n{res.stdout}")
    return out_stem, cmd

def analyze_draw(idx, d, target_kind, png_path: pathlib.Path, crop_out: pathlib.Path):
    pos_data = d['attributes']['a_pos']['data']
    x_vals = [p[0] for p in pos_data]
    y_vals = [p[1] for p in pos_data]
    z_vals = [p[2] for p in pos_data]
    min_x, max_x = min(x_vals), max(x_vals)
    min_y, max_y = min(y_vals), max(y_vals)
    min_z, max_z = min(z_vals), max(z_vals)

    uv_data = d['attributes']['a_texCoord']['data']
    u_vals = [p[0] for p in uv_data]
    v_vals = [p[1] for p in uv_data]

    color_data = d['attributes'].get('a_color', {}).get('data', [])

    # Screen crop: compute bounding box in screen pixels via MVP projection
    mvp = d.get('uniforms', {}).get('u_mvpMat')
    screen_pts = []
    if mvp and len(mvp) == 16:
        for p in pos_data:
            clip_x = mvp[0]*p[0] + mvp[4]*p[1] + mvp[8]*p[2] + mvp[12]
            clip_y = mvp[1]*p[0] + mvp[5]*p[1] + mvp[9]*p[2] + mvp[13]
            clip_w = mvp[3]*p[0] + mvp[7]*p[1] + mvp[11]*p[2] + mvp[15]
            ndc_x = clip_x / clip_w
            ndc_y = clip_y / clip_w
            sx = (ndc_x + 1.0) * 0.5 * 800.0
            sy = (1.0 - ndc_y) * 0.5 * 1080.0
            screen_pts.append((sx, sy))

    min_sx = min((pt[0] for pt in screen_pts), default=0.0)
    max_sx = max((pt[0] for pt in screen_pts), default=0.0)
    min_sy = min((pt[1] for pt in screen_pts), default=0.0)
    max_sy = max((pt[1] for pt in screen_pts), default=0.0)

    crop_info = None
    if png_path.exists() and screen_pts:
        im = Image.open(png_path)
        pad = 25
        c_left = max(0, int(min_sx - pad))
        c_right = min(im.width, int(max_sx + pad))
        c_top = max(0, int(min_sy - pad))
        c_bottom = min(im.height, int(max_sy + pad))
        if c_right > c_left and c_bottom > c_top:
            crop_im = im.crop((c_left, c_top, c_right, c_bottom))
            crop_im.save(crop_out)
            crop_info = {
                "path": str(crop_out.relative_to(REPO_ROOT)),
                "rect": [c_left, c_top, c_right, c_bottom],
                "width": c_right - c_left,
                "height": c_bottom - c_top
            }

    blend_mode_names = {
        0: "GL_ZERO", 1: "GL_ONE", 768: "GL_SRC_COLOR", 769: "GL_ONE_MINUS_SRC_COLOR",
        770: "GL_SRC_ALPHA", 771: "GL_ONE_MINUS_SRC_ALPHA", 772: "GL_DST_ALPHA",
        773: "GL_ONE_MINUS_DST_ALPHA", 774: "GL_DST_COLOR", 775: "GL_ONE_MINUS_DST_COLOR"
    }
    eq_names = {32774: "GL_FUNC_ADD", 32778: "GL_FUNC_SUBTRACT", 32779: "GL_FUNC_REVERSE_SUBTRACT"}

    blend = d.get('blend', [])
    blend_readable = [blend_mode_names.get(v, str(v)) for v in blend] if blend else []

    eq = d.get('blendEquation', [])
    eq_readable = [eq_names.get(v, str(v)) for v in eq] if eq else []

    record = {
        "target": target_kind,
        "draw_index": idx,
        "indices_count": d.get('count', 0),
        "vertices_count": len(pos_data),
        "primitive_mode": d.get('mode', 4),  # 4 == gl.TRIANGLES
        "color_mask": d.get('colorMask') or d.get('glState', {}).get('colorMask', []),
        "blend_equation_separate": {
            "values": eq,
            "readable": eq_readable
        },
        "blend_func_separate": {
            "values": blend,
            "readable": blend_readable
        },
        "blend_color": d.get('blendColor', []),
        "fbo": {
            "bound": d.get('framebuffer', False),
            "framebuffer_id": d.get('glState', {}).get('framebufferId', 0),
            "attachment": d.get('glState', {}).get('attachment', 0)
        },
        "shaders": {
            "vertex_shader": d['shaders'][0] if len(d.get('shaders', [])) > 0 else "",
            "fragment_shader": d['shaders'][1] if len(d.get('shaders', [])) > 1 else ""
        },
        "uniforms": d.get('uniforms', {}),
        "geometry": {
            "pos_bounds": {
                "min_x": round(min_x, 4), "max_x": round(max_x, 4),
                "min_y": round(min_y, 4), "max_y": round(max_y, 4),
                "min_z": round(min_z, 4), "max_z": round(max_z, 4),
                "width": round(max_x - min_x, 4),
                "height": round(max_y - min_y, 4)
            },
            "screen_bounds": {
                "min_x": round(min_sx, 2), "max_x": round(max_sx, 2),
                "min_y": round(min_sy, 2), "max_y": round(max_sy, 2),
                "width": round(max_sx - min_sx, 2),
                "height": round(max_sy - min_sy, 2)
            },
            "uv_bounds": {
                "min_u": round(min(u_vals), 4), "max_u": round(max(u_vals), 4),
                "min_v": round(min(v_vals), 4), "max_v": round(max(v_vals), 4)
            },
            "vertex_sample_first": pos_data[0] if pos_data else [],
            "vertex_sample_last": pos_data[-1] if pos_data else [],
            "color_sample": color_data[0] if color_data else []
        },
        "crop": crop_info
    }
    return record

def main():
    parser = argparse.ArgumentParser(description="Sample conditions for bm19 and attr2")
    parser.add_argument('--output-dir', type=pathlib.Path, default=REPO_ROOT / 'build' / 'evidence' / 'v6')
    parser.add_argument('--reference-root', type=pathlib.Path, default=REPO_ROOT.parent / 'NekoWebShow')
    parser.add_argument('--browser', type=pathlib.Path,
                        default=pathlib.Path('/home/chocola/.cache/ms-playwright/chromium-1194/chrome-linux/chrome'))
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    lock_file = REPO_ROOT / 'build' / 'gpu-validation.lock'
    lock_file.parent.mkdir(parents=True, exist_ok=True)

    print("Locking GPU validation lock...")
    with open(lock_file, 'w') as lock_fd:
        fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        print("Acquired lock. Recording tool hashes...")

        # Record tools and source hashes
        tool_hashes = {
            "trace.js": sha256_file(HERE / 'trace.js'),
            "capture.py": sha256_file(HERE / 'capture.py'),
            "capture.cjs": sha256_file(HERE / 'capture.cjs'),
            "reference.html": sha256_file(HERE / 'reference.html'),
            "browser": sha256_file(args.browser),
            "fraise-maid-a.pure.psb.zip": sha256_file(args.reference_root / 'data' / 'fraise-maid-a.pure.psb.zip'),
            "fraise-france-maid-b.pure.psb.zip": sha256_file(args.reference_root / 'data' / 'fraise-france-maid-b.pure.psb.zip'),
            "FreeMoteDriver.js": sha256_file(args.reference_root / 'driver' / 'FreeMoteDriver.js'),
            "emoteplayer.js": sha256_file(args.reference_root / 'driver' / 'emoteplayer.js')
        }
        (args.output_dir / 'tool_hashes.json').write_text(json.dumps(tool_hashes, indent=2))
        print("Tool hashes saved.")

        # 1. Run BM19 cases
        print("\n=== Sampling bm=19 cases (fraise-maid-a) ===")
        bm19_results = []
        for case in BM19_CASES:
            print(f"Running {case['id']}...")
            out_stem, cmd = run_capture(case, args.output_dir, args.reference_root, args.browser)
            trace_path = out_stem.with_suffix('.trace.json')
            png_path = out_stem.with_suffix('.png')
            crop_path = args.output_dir / f"crop_{case['id']}.png"

            traces = json.loads(trace_path.read_text())
            matches = find_target_draw(traces, 'bm19')
            if not matches:
                print(f"  WARNING: target bm19 draw not found in {case['id']}!")
                continue
            idx, draw_obj, uv_bounds = matches[0]
            print(f"  Found bm19 at draw #{idx} (count={draw_obj['count']}, uv={uv_bounds})")
            rec = analyze_draw(idx, draw_obj, 'bm19', png_path, crop_path)
            rec['case_id'] = case['id']
            rec['description'] = case['description']
            rec['command'] = " ".join(cmd)
            rec['png_sha256'] = sha256_file(png_path)
            rec['trace_sha256'] = sha256_file(trace_path)
            bm19_results.append(rec)

        (args.output_dir / 'bm19_condition_sampling.json').write_text(json.dumps(bm19_results, indent=2))
        print(f"Saved {len(bm19_results)} bm19 condition records.")

        # 2. Run ATTR2 cases
        print("\n=== Sampling attr=2 cases (fraise-france-maid-b) ===")
        attr2_results = []
        for case in ATTR2_CASES:
            print(f"Running {case['id']}...")
            out_stem, cmd = run_capture(case, args.output_dir, args.reference_root, args.browser)
            trace_path = out_stem.with_suffix('.trace.json')
            png_path = out_stem.with_suffix('.png')
            crop_path = args.output_dir / f"crop_{case['id']}.png"

            traces = json.loads(trace_path.read_text())
            matches = find_target_draw(traces, 'attr2')
            if not matches:
                print(f"  WARNING: target attr2 draw not found in {case['id']}!")
                continue
            idx, draw_obj, uv_bounds = matches[0]
            print(f"  Found attr2 at draw #{idx} (count={draw_obj['count']}, uv={uv_bounds})")
            rec = analyze_draw(idx, draw_obj, 'attr2', png_path, crop_path)
            rec['case_id'] = case['id']
            rec['description'] = case['description']
            rec['command'] = " ".join(cmd)
            rec['png_sha256'] = sha256_file(png_path)
            rec['trace_sha256'] = sha256_file(trace_path)
            attr2_results.append(rec)

        (args.output_dir / 'attr2_condition_sampling.json').write_text(json.dumps(attr2_results, indent=2))
        print(f"Saved {len(attr2_results)} attr2 condition records.")

    print("\nCompleted condition sampling successfully.")

if __name__ == '__main__':
    main()
