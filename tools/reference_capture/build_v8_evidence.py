#!/usr/bin/env python3
"""Independent visual, geometric, topological, and scalar verification suite for V8.

Covers:
- fraise-france-maid-b: default open eye, closed eye, head_LR ±30, body_LR ±30,
  head_slant 30, body_slant 30, combined multi-axis pose, 60-step continuous physics.
- chocola-date-a: full-idle24 and full-jump24 motion clip isolation, pupil & highlight boundaries.
- fraise-maid-a: active add_mask participation, head_lr30.
- Counterexamples: force-negative30 and force-half-positive30 physics scalar precision limits.
"""

import argparse
import fcntl
import hashlib
import json
import math
import os
import pathlib
import subprocess
import sys
from datetime import datetime, timezone
from PIL import Image, ImageChops

ROOT = pathlib.Path(__file__).resolve().parents[2]
LOCK_FILE = ROOT / 'build' / 'gpu-validation.lock'

# Binaries
BIN_PREVIEW = ROOT / 'build' / 'release' / 'bin' / 'nekomote_preview'
BIN_POSE = ROOT / 'build' / 'release' / 'bin' / 'nekomote_pose'
BIN_PLAYBACK = ROOT / 'build' / 'release' / 'bin' / 'nekomote_playback_probe'
BIN_RENDER = ROOT / 'build' / 'release' / 'bin' / 'nekomote_frame_render'

# Assets
ASSET_FRAISE_FRANCE = ROOT / 'assets' / 'characters' / 'fraise-france-maid-b' / 'model.nkm.json'
ASSET_CHOCOLA = ROOT / 'assets' / 'characters' / 'chocola-date-a' / 'model.nkm.json'
ASSET_FRAISE_MAID_A = ROOT / 'assets' / 'characters' / 'fraise-maid-a' / 'model.nkm.json'

sys.path.insert(0, str(ROOT / 'tools' / 'reference_capture'))
from compare_playback_frame import compare as compare_playback_frame


def sha256_file(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def new_output_directory(path):
    """Only a new directory may receive evidence, including empty existing directories."""
    path = pathlib.Path(path).resolve()
    path.mkdir(parents=True, exist_ok=False)
    return path


def artifact_path(path):
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def asset_input_hashes(asset):
    data = json.loads(asset.read_text())
    files = [asset] + [(asset.parent / texture['path']).resolve()
                       for texture in data['model']['textures']]
    return {str(path): sha256_file(path) for path in files}


def finite_number(value):
    try:
        return type(value) in (int, float) and math.isfinite(value)
    except OverflowError:
        return False


def pose_geometry_passed(data):
    counts = ('missing_reference_triangles', 'unmatched_reference_vertices',
              'unused_native_triangles', 'unused_native_vertices')
    error = data.get('frame_max_error_px')
    return bool(finite_number(error) and 0 <= error <= .001 and
                type(data.get('frame_error')) is int and data['frame_error'] == 0 and
                data.get('missing_primitives') == [] and
                type(data.get('frame_vertices')) is int and data['frame_vertices'] > 0 and
                type(data.get('frame_indices')) is int and data['frame_indices'] > 0 and
                all(type(data.get(key)) is int and data[key] == 0 for key in counts))


# These two public SDK GetVariable observations do not expose the active head_LR
# differential contribution. Geometry still checks it; all other scalar keys remain gated.
PUBLIC_SCALAR_SKIPS = {
    'fraise-france-maid-b_direction-diff': {'head_LR'},
    'fraise-france-maid-b_source-timeline60': {'head_LR'},
}
PUBLIC_SKIP_REASON = ('Public SDK GetVariable head_LR retains the base value and omits the '
                      'active differential contribution; geometry remains strictly checked.')


def compare_scalar_samples(reference, native, case_name):
    """Validate all observations; compare identical states after updates 1..steps."""
    report = {'status': 'FAIL', 'passed': False, 'samples': 0, 'max_error': None,
              'threshold': 1e-5, 'skipped': [],
              'initialization_boundary': {
                  'step': 0, 'compared': False,
                  'reason': ('Public SDK GetVariable cache has not been refreshed by the first '
                             'Update; native step 0 already reflects configured inputs. '
                             'Both step 0 records are structurally and numerically validated.')
              }}
    try:
        source_samples = reference.get('variableSamples')
        native_samples = native.get('samples')
        if not isinstance(source_samples, list) or not source_samples:
            raise ValueError('reference variableSamples must be nonempty')
        if not isinstance(native_samples, list) or len(native_samples) != len(source_samples):
            raise ValueError('native/reference sample counts disagree')
        steps = reference.get('steps')
        if (type(steps) is not int or steps < 1 or type(native.get('steps')) is not int or
                native['steps'] != steps):
            raise ValueError('native/reference steps must agree and be positive')
        if len(source_samples) != steps + 1:
            raise ValueError('observations must include every step 0..steps')
        observed_keys = None
        errors = []
        for step, (source, target) in enumerate(zip(source_samples, native_samples)):
            if (not isinstance(source, dict) or not isinstance(target, dict) or
                    type(source.get('step')) is not int or source['step'] != step or
                    type(target.get('step')) is not int or target['step'] != step):
                raise ValueError('native/reference step sequence must be exactly 0..steps')
            a, b = source.get('values'), target.get('values')
            if not isinstance(a, dict) or not a or not isinstance(b, dict) or not b:
                raise ValueError('each step requires nonempty variable values')
            if observed_keys is None:
                observed_keys = set(a)
                # sampleVariables is the SDK capture's Boolean enumeration switch;
                # observeVariables is the explicit list of additional requested keys.
                requested = reference.get('observeVariables', [])
                if not isinstance(requested, list) or any(
                        not isinstance(key, str) or key not in observed_keys for key in requested):
                    raise ValueError('requested scalar observations are absent from reference')
            elif set(a) != observed_keys:
                raise ValueError('reference variable keys must remain identical across steps')
            compared = 0
            for key, value in a.items():
                if not isinstance(key, str) or not key or not finite_number(value):
                    raise ValueError('reference variable names/values must be valid and finite')
                if key not in b or not finite_number(b[key]):
                    raise ValueError(f'native variable {key!r} is absent or nonfinite')
                if step == 0:
                    continue
                if key in PUBLIC_SCALAR_SKIPS.get(case_name, set()):
                    report['skipped'].append({'step': step, 'variable': key,
                                              'reason': PUBLIC_SKIP_REASON})
                    continue
                error = abs(value - b[key])
                if not math.isfinite(error):
                    raise ValueError('scalar subtraction produced a nonfinite error')
                errors.append(error)
                compared += 1
            if step > 0 and not compared:
                raise ValueError('each step needs at least one comparable scalar observation')
        report['samples'] = len(errors)
        report['max_error'] = max(errors)
        report['passed'] = report['max_error'] <= report['threshold']
        report['status'] = 'PASS' if report['passed'] else 'FAIL'
    except ValueError as error:
        report['error'] = str(error)
    return report


def finalize_case(case):
    scalar = case['scalar']
    scalar_ok = (scalar.get('status') == 'NOT_APPLICABLE' and case['type'] == 'static' or
                 scalar.get('status') == 'PASS' and scalar.get('passed') is True)
    case['passed'] = bool(case['geometry']['passed'] and scalar_ok and case['image']['passed'])
    reproduced = bool(case['is_counterexample'] and case['geometry']['passed'] and
                      case['image']['passed'] and scalar.get('status') == 'FAIL' and
                      finite_number(scalar.get('max_error')) and scalar['max_error'] > 1e-5)
    case['expected_failure_reproduced'] = reproduced
    case['verdict'] = ('DOCUMENTED_COUNTEREXAMPLE' if reproduced else
                       'PASS' if case['passed'] else 'FAIL')
    return case


def suite_acceptance(results):
    return bool(results) and all(case['passed'] for case in results)


def tool_metadata():
    directory = pathlib.Path(__file__).resolve().parent
    dependencies = ('build_v8_evidence.py', 'compare_playback_frame.py', 'capture.py',
                    'capture.cjs', 'reference.html', 'trace.js', 'psb_fixture.py',
                    'package.json', 'package-lock.json')
    return {
        'commit': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'],
                                          text=True).strip(),
        'working_tree_dirty': bool(subprocess.check_output(
            ['git', '-C', str(ROOT), 'status', '--porcelain'], text=True).strip()),
        'dependencies_sha256': {name: sha256_file(directory / name) for name in dependencies},
        'python': sys.version,
        'commands': [],
    }


def run_command(cmd, log_path: pathlib.Path, commands):
    commands.append({'argv': [str(c) for c in cmd], 'log': artifact_path(log_path)})
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open('w') as f:
        res = subprocess.run([str(c) for c in cmd], stdout=f, stderr=subprocess.STDOUT)
    if res.returncode != 0:
        log_content = log_path.read_text()
        raise RuntimeError(f"Command failed (rc={res.returncode}): {' '.join(str(c) for c in cmd)}\nLog:\n{log_content}")


def compare_images_detailed(ref_path: pathlib.Path, nat_path: pathlib.Path, out_base: pathlib.Path, tolerance: int = 2):
    r = Image.open(ref_path).convert('RGBA')
    n = Image.open(nat_path).convert('RGBA')
    if r.size != n.size or r.getchannel('A').getbbox() is None or n.getchannel('A').getbbox() is None:
        raise ValueError('reference/native dimensions disagree or image is empty')
    
    diff = ImageChops.difference(r, n)
    maximum = [int(high) for _, high in diff.getextrema()]
    alpha_error = int(ImageChops.difference(r.getchannel('A'), n.getchannel('A')).getextrema()[1])
    
    visual_diffs = []
    for bg in ((0, 0, 0, 255), (255, 255, 255, 255)):
        background = Image.new('RGBA', r.size, bg)
        visual_diffs.append(ImageChops.difference(Image.alpha_composite(background, r), Image.alpha_composite(background, n)))
    
    visual_maxima = [[int(high) for _, high in image.getextrema()][:3] for image in visual_diffs]
    
    diff_data_black = [max(v[:3]) for v in visual_diffs[0].getdata()]
    diff_data_white = [max(v[:3]) for v in visual_diffs[1].getdata()]
    alpha_diff_data = list(ImageChops.difference(r.getchannel('A'), n.getchannel('A')).getdata())
    
    errors = sum(max(a, b, c) > tolerance for a, b, c in zip(diff_data_black, diff_data_white, alpha_diff_data))
    
    report = {
        'reference_sha256': sha256_file(ref_path),
        'native_sha256': sha256_file(nat_path),
        'size': list(r.size),
        'raw_max_rgba': maximum,
        'alpha_max_error': alpha_error,
        'composed_max_rgb_black_white': visual_maxima,
        'tolerance': tolerance,
        'pixels_over_tolerance': errors,
        'reference_bbox': list(r.getbbox()),
        'native_bbox': list(n.getbbox()),
        'passed': errors == 0
    }
    
    out_base.parent.mkdir(parents=True, exist_ok=True)
    json_path = out_base.with_suffix('.json')
    json_path.write_text(json.dumps(report, indent=2) + '\n')
    
    # Generate full 3-panel side-by-side
    bbox = r.getbbox()
    box = (max(0, bbox[0]-8), max(0, bbox[1]-8), min(r.width, bbox[2]+8), min(r.height, bbox[3]+8))
    crops = [image.crop(box) for image in (r, n)]
    bg_gray = Image.new('RGBA', crops[0].size, (80, 80, 80, 255))
    panels = [Image.alpha_composite(bg_gray, crop).convert('RGB') for crop in crops]
    panels.append(visual_diffs[0].crop(box).convert('RGB').point(lambda v: min(255, v * 16)))
    
    sbs = Image.new('RGB', (panels[0].width * 3, panels[0].height))
    for i, panel in enumerate(panels):
        sbs.paste(panel, (i * panel.width, 0))
    if sbs.height < 128:
        sbs = sbs.resize((sbs.width * 4, sbs.height * 4), Image.Resampling.NEAREST)
    sbs.save(out_base.with_suffix('.png'))
    
    return report


def generate_region_crops(ref_path: pathlib.Path, nat_path: pathlib.Path, out_dir: pathlib.Path, case_name: str, boxes: dict):
    r = Image.open(ref_path).convert('RGBA')
    n = Image.open(nat_path).convert('RGBA')
    diff_rgb = ImageChops.difference(r, n).convert('RGB')
    diff_amp = diff_rgb.point(lambda v: min(255, v * 16))
    
    crop_records = {}
    bg_check = Image.new('RGBA', (32, 32), (180, 180, 180, 255))
    # Checkerboard pattern for alpha inspection
    for bx in range(0, 16):
        for by in range(0, 16):
            bg_check.putpixel((bx, by), (220, 220, 220, 255))
            bg_check.putpixel((bx+16, by+16), (220, 220, 220, 255))
    
    for region_name, box in boxes.items():
        r_crop = r.crop(box)
        n_crop = n.crop(box)
        d_crop = diff_amp.crop(box)
        
        # Save individual ref and native crops
        ref_crop_file = out_dir / f"{case_name}_crop_{region_name}_ref.png"
        nat_crop_file = out_dir / f"{case_name}_crop_{region_name}_nat.png"
        diff_crop_file = out_dir / f"{case_name}_crop_{region_name}_diff16x.png"
        sbs_crop_file = out_dir / f"{case_name}_crop_{region_name}_sbs.png"
        
        r_crop.save(ref_crop_file)
        n_crop.save(nat_crop_file)
        d_crop.save(diff_crop_file)
        
        # Make a 3-panel composite (Ref on gray | Native on gray | 16x diff)
        bg = Image.new('RGBA', r_crop.size, (60, 60, 60, 255))
        r_comp = Image.alpha_composite(bg, r_crop).convert('RGB')
        n_comp = Image.alpha_composite(bg, n_crop).convert('RGB')
        
        panel_w = r_crop.width
        panel_h = r_crop.height
        sbs = Image.new('RGB', (panel_w * 3, panel_h))
        sbs.paste(r_comp, (0, 0))
        sbs.paste(n_comp, (panel_w, 0))
        sbs.paste(d_crop, (panel_w * 2, 0))
        sbs.save(sbs_crop_file)
        
        crop_records[region_name] = {
            'box': list(box),
            'ref_crop': artifact_path(ref_crop_file),
            'ref_crop_sha256': sha256_file(ref_crop_file),
            'nat_crop': artifact_path(nat_crop_file),
            'nat_crop_sha256': sha256_file(nat_crop_file),
            'diff_crop_sha256': sha256_file(diff_crop_file),
            'sbs_crop': artifact_path(sbs_crop_file),
            'sbs_crop_sha256': sha256_file(sbs_crop_file),
        }
    return crop_records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cases', nargs='*', help='Optional subset of cases to run')
    parser.add_argument('--output', type=pathlib.Path, required=True,
                        help='New evidence directory; existing directories are rejected')
    parser.add_argument('--browser', type=pathlib.Path,
                        help='Required when selected cases need new reference captures')
    args = parser.parse_args()

    try:
        EVIDENCE_DIR = new_output_directory(args.output)
    except FileExistsError:
        parser.error('--output must be a new directory; existing evidence cannot be overwritten')
    metadata = tool_metadata()

    LOCK_FILE.parent.mkdir(parents=True, exist_ok=True)
    lock_fd = os.open(str(LOCK_FILE), os.O_CREAT | os.O_RDWR)
    try:
        fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        print("ERROR: Could not acquire GPU validation lock! Another process is holding build/gpu-validation.lock.")
        sys.exit(1)

    print("=== Acquiring GPU Validation Lock: SUCCESS ===")
    crops_dir = EVIDENCE_DIR / 'crops'
    crops_dir.mkdir()

    if args.browser is not None:
        metadata['browser'] = {'path': str(args.browser.resolve()),
                               'sha256': sha256_file(args.browser)}

    # 1. Audit Tool & Binary Hashes
    binary_hashes = {
        'nekomote_preview': sha256_file(BIN_PREVIEW),
        'nekomote_pose': sha256_file(BIN_POSE),
        'nekomote_playback_probe': sha256_file(BIN_PLAYBACK),
        'nekomote_frame_render': sha256_file(BIN_RENDER),
    }
    asset_hashes = {
        'fraise-france-maid-b': sha256_file(ASSET_FRAISE_FRANCE),
        'chocola-date-a': sha256_file(ASSET_CHOCOLA),
        'fraise-maid-a': sha256_file(ASSET_FRAISE_MAID_A),
    }
    asset_inputs = {str(asset): asset_input_hashes(asset) for asset in
                    (ASSET_FRAISE_FRANCE, ASSET_CHOCOLA, ASSET_FRAISE_MAID_A)}

    # Define crop bounding boxes
    FRAISE_BOXES = {
        'eye_stencil': (230, 480, 530, 630),
        'contour': (250, 600, 550, 800),
        'ear_edges': (150, 200, 650, 480),
        'apron_torso': (260, 780, 540, 1060),
    }
    CHOCOLA_BOXES = {
        'eye_pupil_highlight': (250, 450, 550, 620),
        'head_contour': (250, 400, 550, 700),
        'ear_ribbon': (150, 180, 650, 450),
    }
    FRAISE_MAID_A_BOXES = {
        'add_mask_eye': (230, 480, 530, 630),
        'contour': (250, 600, 550, 800),
        'ear_edges': (150, 200, 650, 480),
    }

    # 2. Case Definitions
    # (name, type, asset, ref_json, ref_png, ref_trace, inputs_or_vars, boxes, is_counterexample)
    v7_dir = ROOT / 'build' / 'evidence' / 'v7'
    v6_dir = ROOT / 'build' / 'evidence' / 'v6'
    c5_dir = ROOT / 'build' / 'evidence' / 'c5-mirror-mask-2026-10-07'
    p6_dir = ROOT / 'build' / 'evidence' / 'p6-chain-binding'

    cases = [
        # --- Group 1: Fraise France Maid B (Mirrored Parameters & Optional Mask) ---
        {
            'name': 'fraise-france-maid-b_eye0_default',
            'type': 'static',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': v7_dir / 'fraise-france-maid-b_eye0_default.json',
            'ref_png': v7_dir / 'fraise-france-maid-b_eye0_default.png',
            'ref_trace': v7_dir / 'fraise-france-maid-b_eye0_default.trace.json',
            'variables': {'face_eye_open': 0},
            'origin': [400, 940],
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'Default open eye state; verifies absent optional add_mask is gracefully resolved without error 8 and eye stencil (shirome) clips pupil and highlight.'
        },
        {
            'name': 'fraise-france-maid-b_eye10_neutral',
            'type': 'static',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': v7_dir / 'fraise-france-maid-b_eye10_neutral.json',
            'ref_png': v7_dir / 'fraise-france-maid-b_eye10_neutral.png',
            'ref_trace': v7_dir / 'fraise-france-maid-b_eye10_neutral.trace.json',
            'variables': {'face_eye_open': 10},
            'origin': [400, 940],
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'Closed eye state; verifies eye closure interpolation and head baseline contour.'
        },
        {
            'name': 'fraise-france-maid-b_eye10_head_lr30',
            'type': 'static',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': v7_dir / 'fraise-france-maid-b_eye10_head_lr30.json',
            'ref_png': v7_dir / 'fraise-france-maid-b_eye10_head_lr30.png',
            'ref_trace': v7_dir / 'fraise-france-maid-b_eye10_head_lr30.trace.json',
            'variables': {'face_eye_open': 10, 'head_LR': 30},
            'origin': [400, 940],
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'Positive head_LR (+30); verifies schema 6 automatic mirror inversion matches WebGL without manual negation.'
        },
        {
            'name': 'fraise-france-maid-b_eye10_head_lr_minus30',
            'type': 'static',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': v7_dir / 'fraise-france-maid-b_eye10_head_lr_minus30.json',
            'ref_png': v7_dir / 'fraise-france-maid-b_eye10_head_lr_minus30.png',
            'ref_trace': v7_dir / 'fraise-france-maid-b_eye10_head_lr_minus30.trace.json',
            'variables': {'face_eye_open': 10, 'head_LR': -30},
            'origin': [400, 940],
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'Negative head_LR (-30); verifies reverse head turn symmetry and mirror inversion.'
        },
        {
            'name': 'fraise-france-maid-b_eye10_body_lr30',
            'type': 'static',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': v7_dir / 'fraise-france-maid-b_eye10_body_lr30.json',
            'ref_png': v7_dir / 'fraise-france-maid-b_eye10_body_lr30.png',
            'ref_trace': v7_dir / 'fraise-france-maid-b_eye10_body_lr30.trace.json',
            'variables': {'face_eye_open': 10, 'body_LR': 30},
            'origin': [400, 940],
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'Positive body_LR (+30); verifies torso rotation mirror inversion.'
        },
        {
            'name': 'fraise-france-maid-b_eye10_body_lr_minus30',
            'type': 'static',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': v7_dir / 'fraise-france-maid-b_eye10_body_lr_minus30.json',
            'ref_png': v7_dir / 'fraise-france-maid-b_eye10_body_lr_minus30.png',
            'ref_trace': v7_dir / 'fraise-france-maid-b_eye10_body_lr_minus30.trace.json',
            'variables': {'face_eye_open': 10, 'body_LR': -30},
            'origin': [400, 940],
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'Negative body_LR (-30); verifies torso reverse turn symmetry.'
        },
        {
            'name': 'fraise-france-maid-b_body_slant30',
            'type': 'static',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': v6_dir / 'fraise-france-maid-b_body_slant30.json',
            'ref_png': v6_dir / 'fraise-france-maid-b_body_slant30.png',
            'ref_trace': v6_dir / 'fraise-france-maid-b_body_slant30.trace.json',
            'variables': {'body_slant': 30},
            'origin': [400, 940],
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'Slant parameter body_slant (+30); verifies slant substring mirror inversion.'
        },
        {
            'name': 'fraise-france-maid-b_head_slant30',
            'type': 'static',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': EVIDENCE_DIR / 'fraise-france-maid-b_head_slant30.json',
            'ref_png': EVIDENCE_DIR / 'fraise-france-maid-b_head_slant30.png',
            'ref_trace': EVIDENCE_DIR / 'fraise-france-maid-b_head_slant30.trace.json',
            'variables': {'head_slant': 30},
            'origin': [400, 940],
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'Slant parameter head_slant (+30); verifies head lean mirror inversion.'
        },
        {
            'name': 'fraise-france-maid-b_combined_pose',
            'type': 'static',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': EVIDENCE_DIR / 'fraise-france-maid-b_combined_pose.json',
            'ref_png': EVIDENCE_DIR / 'fraise-france-maid-b_combined_pose.png',
            'ref_trace': EVIDENCE_DIR / 'fraise-france-maid-b_combined_pose.trace.json',
            'variables': {'face_eye_open': 0, 'head_LR': 30, 'head_UD': 30, 'body_LR': -15},
            'origin': [400, 940],
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'Combined multi-axis deformation (head_LR 30, head_UD 30, body_LR -15, open eyes).'
        },
        {
            'name': 'fraise-france-maid-b_source-physics60',
            'type': 'playback',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': c5_dir / 'source-physics60.json',
            'ref_png': c5_dir / 'source-physics60.png',
            'ref_trace': c5_dir / 'source-physics60.trace.json',
            'input_json': c5_dir / 'physics60-input.json',
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': '60-step continuous timeline & physics integration with mirrored anchors.'
        },

        # --- Group 2: Chocola Date A (Motion Clip Group Independence) ---
        {
            'name': 'chocola-date-a_full-idle24',
            'type': 'playback',
            'asset': ASSET_CHOCOLA,
            'model': 'chocola-date-a',
            'ref_json': p6_dir / 'full-idle24.json',
            'ref_png': p6_dir / 'full-idle24.png',
            'ref_trace': p6_dir / 'full-idle24.trace.json',
            'input_json': c5_dir / 'regression-accepted' / 'base' / 'full-idle24-inputs.json',
            'boxes': CHOCOLA_BOXES,
            'is_counterexample': False,
            'desc': '24-step idle motion; verifies pupil and independent highlight boundaries remain isolated.'
        },
        {
            'name': 'chocola-date-a_full-jump24',
            'type': 'playback',
            'asset': ASSET_CHOCOLA,
            'model': 'chocola-date-a',
            'ref_json': p6_dir / 'full-jump24.json',
            'ref_png': p6_dir / 'full-jump24.png',
            'ref_trace': p6_dir / 'full-jump24.trace.json',
            'input_json': c5_dir / 'regression-accepted' / 'base' / 'full-jump24-inputs.json',
            'boxes': CHOCOLA_BOXES,
            'is_counterexample': False,
            'desc': '24-step jump motion (ピョン); verifies motion clip isolation across motions sharing the same stencil.'
        },

        # --- Group 3: Fraise Maid A (Active Mask Participation) ---
        {
            'name': 'fraise-maid-a_base',
            'type': 'static',
            'asset': ASSET_FRAISE_MAID_A,
            'model': 'fraise-maid-a',
            'ref_json': v6_dir / 'fraise-maid-a_base.json',
            'ref_png': v6_dir / 'fraise-maid-a_base.png',
            'ref_trace': v6_dir / 'fraise-maid-a_base.trace.json',
            'variables': {},
            'origin': [400, 940],
            'boxes': FRAISE_MAID_A_BOXES,
            'is_counterexample': False,
            'desc': 'Base state; verifies add_mask exists in local scope, actively participates in stencil masking, and is not deleted by name.'
        },
        {
            'name': 'fraise-maid-a_head_lr30',
            'type': 'static',
            'asset': ASSET_FRAISE_MAID_A,
            'model': 'fraise-maid-a',
            'ref_json': v6_dir / 'fraise-maid-a_head_lr30.json',
            'ref_png': v6_dir / 'fraise-maid-a_head_lr30.png',
            'ref_trace': v6_dir / 'fraise-maid-a_head_lr30.trace.json',
            'variables': {'head_LR': 30},
            'origin': [400, 940],
            'boxes': FRAISE_MAID_A_BOXES,
            'is_counterexample': False,
            'desc': 'Head turn on non-mirrored model; verifies add_mask tracking and deformation.'
        },

        # --- Group 4: Dynamic Direction & Force Controls ---
        {
            'name': 'fraise-france-maid-b_direction-diff',
            'type': 'playback',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': c5_dir / 'direction-diff.json',
            'ref_png': c5_dir / 'direction-diff.png',
            'ref_trace': c5_dir / 'direction-diff.trace.json',
            'input_json': c5_dir / 'direction-diff-input.json',
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'External difference timeline trajectory.'
        },
        {
            'name': 'fraise-france-maid-b_direction-add',
            'type': 'playback',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': c5_dir / 'direction-add.json',
            'ref_png': c5_dir / 'direction-add.png',
            'ref_trace': c5_dir / 'direction-add.trace.json',
            'input_json': c5_dir / 'direction-add-input.json',
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'Directional additive timeline deformation.'
        },
        {
            'name': 'fraise-france-maid-b_direction-main',
            'type': 'playback',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': c5_dir / 'direction-main.json',
            'ref_png': c5_dir / 'direction-main.png',
            'ref_trace': c5_dir / 'direction-main.trace.json',
            'input_json': c5_dir / 'direction-main-input.json',
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'Directional main timeline deformation.'
        },
        {
            'name': 'fraise-france-maid-b_source-timeline60',
            'type': 'playback',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': c5_dir / 'source-timeline60.json',
            'ref_png': c5_dir / 'source-timeline60.png',
            'ref_trace': c5_dir / 'source-timeline60.trace.json',
            'input_json': c5_dir / 'timeline60-input.json',
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'Full timeline sequence with differences and playback.'
        },
        {
            'name': 'fraise-france-maid-b_force-positive30',
            'type': 'playback',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': c5_dir / 'force-positive30.json',
            'ref_png': c5_dir / 'force-positive30.png',
            'ref_trace': c5_dir / 'force-positive30.trace.json',
            'input_json': c5_dir / 'force-positive30-origin-input.json',
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'Positive external force (+3, -1) with scale 0.8 and origin (50, 350).'
        },
        {
            'name': 'fraise-france-maid-b_force-half-negative30',
            'type': 'playback',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': c5_dir / 'force-half-negative30.json',
            'ref_png': c5_dir / 'force-half-negative30.png',
            'ref_trace': c5_dir / 'force-half-negative30.trace.json',
            'input_json': c5_dir / 'force-half-negative30-origin-input.json',
            'boxes': FRAISE_BOXES,
            'is_counterexample': False,
            'desc': 'Negative external force (-3, 1) with scale 0.5 and origin (50, 350).'
        },

        # --- Group 5: Documented Physics Precision Counterexamples (反例) ---
        {
            'name': 'fraise-france-maid-b_force-negative30',
            'type': 'playback',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': c5_dir / 'force-negative30.json',
            'ref_png': c5_dir / 'force-negative30.png',
            'ref_trace': c5_dir / 'force-negative30.trace.json',
            'input_json': c5_dir / 'force-negative30-origin-input.json',
            'boxes': FRAISE_BOXES,
            'is_counterexample': True,
            'counterexample_reason': 'hair_UD_front scalar max error 1.66893e-05 at step 3 exceeds 1e-05 threshold; geometry and pixels pass.',
            'desc': 'Counterexample: external force (-3, 1) at scale 0.8. Demonstrates scalar precision limit in hair_UD_front.'
        },
        {
            'name': 'fraise-france-maid-b_force-half-positive30',
            'type': 'playback',
            'asset': ASSET_FRAISE_FRANCE,
            'model': 'fraise-france-maid-b',
            'ref_json': c5_dir / 'force-half-positive30.json',
            'ref_png': c5_dir / 'force-half-positive30.png',
            'ref_trace': c5_dir / 'force-half-positive30.trace.json',
            'input_json': c5_dir / 'force-half-positive30-origin-input.json',
            'boxes': FRAISE_BOXES,
            'is_counterexample': True,
            'counterexample_reason': 'hair_UD_front scalar max error 1.14441e-05 at step 26 exceeds 1e-05 threshold; geometry and pixels pass.',
            'desc': 'Counterexample: external force (+3, -1) at scale 0.5. Demonstrates scalar precision limit in hair_UD_front.'
        },
    ]

    if args.cases is not None:
        requested = set(args.cases)
        unknown = requested - {case['name'] for case in cases}
        if not requested or unknown:
            parser.error('--cases requires known nonempty case names: ' + ', '.join(sorted(unknown)))
        cases = [case for case in cases if case['name'] in requested]

    # Pre-capture any references that are defined to live directly in EVIDENCE_DIR
    for c in cases:
        if c['ref_json'].parent == EVIDENCE_DIR and not c['ref_png'].exists():
            if args.browser is None:
                parser.error('--browser is required for fresh WebGL references')
            print(f"[*] Capturing WebGL reference for {c['name']}...")
            cap_cmd = [
                'python3', str(ROOT / 'tools' / 'reference_capture' / 'capture.py'),
                '--reference-root', '../NekoWebShow',
                '--model', c['model'],
                '--browser', str(args.browser.resolve()),
                '--width', 800, '--height', 1080, '--x', 0, '--y', 400,
                '--variables', json.dumps(c['variables']),
                '--trace',
                '--output', str(EVIDENCE_DIR / c['name'])
            ]
            run_command(cap_cmd, EVIDENCE_DIR / f"{c['name']}_capture.log", metadata['commands'])

    # Run evaluations
    results = []

    for c in cases:
        name = c['name']
        print(f"\n---> Evaluating Case: {name} (type={c['type']}, is_counterexample={c['is_counterexample']})")
        case_res = {
            'case': name,
            'type': c['type'],
            'model': c['model'],
            'description': c['desc'],
            'is_counterexample': c['is_counterexample'],
            'counterexample_reason': c.get('counterexample_reason', None),
            'inputs': ({'variables': c['variables'], 'origin': c['origin'], 'scale': 1,
                        'viewport': [800, 1080]} if 'variables' in c else
                       json.loads(pathlib.Path(c['input_json']).read_text())),
            'reference_hashes': {
                'png_sha256': sha256_file(c['ref_png']),
                'trace_sha256': sha256_file(c['ref_trace']),
                'metadata_sha256': sha256_file(c['ref_json']),
            }
        }

        case_res['input_hashes'] = dict(asset_inputs[str(c['asset'])])
        if 'input_json' in c:
            case_res['input_hashes'][str(c['input_json'])] = sha256_file(c['input_json'])
        else:
            case_res['authored_variables_sha256'] = hashlib.sha256(
                json.dumps(c['variables'], sort_keys=True, allow_nan=False).encode()).hexdigest()
        case_res['reference_files'] = {key: str(c[key]) for key in
                                       ('ref_json', 'ref_png', 'ref_trace')}

        native_png = EVIDENCE_DIR / f"{name}_native.png"
        geo_json = EVIDENCE_DIR / f"{name}_geo.json"
        comp_base = EVIDENCE_DIR / f"{name}_compare"

        if c['type'] == 'static':
            # 1. Pose geometry check
            pose_cmd = [
                str(BIN_POSE),
                '--asset', str(c['asset']),
                '--sample-trace', str(c['ref_trace']),
                '--variables', json.dumps(c['variables']),
                '--output', str(geo_json)
            ]
            run_command(pose_cmd, EVIDENCE_DIR / f"{name}_pose.log", metadata['commands'])
            g_data = json.load(geo_json.open())
            geo_passed = pose_geometry_passed(g_data)
            case_res['geometry'] = {
                'passed': geo_passed,
                'frame_max_error_px': g_data['frame_max_error_px'],
                'missing_reference_triangles': g_data['missing_reference_triangles'],
                'unmatched_reference_vertices': g_data['unmatched_reference_vertices'],
                'unused_native_triangles': g_data['unused_native_triangles'],
                'unused_native_vertices': g_data['unused_native_vertices'],
                'missing_primitives': g_data['missing_primitives'],
                'frame_error': g_data['frame_error']
            }
            case_res['scalar'] = {'status': 'NOT_APPLICABLE', 'passed': None,
                                  'samples': 0, 'max_error': None,
                                  'reason': 'Static pose has no dynamic scalar observations'}

            # 2. Preview Render
            prev_cmd = [
                str(BIN_PREVIEW),
                '--geometry-diagnostic',
                '--asset', str(c['asset']),
                '--origin', c['origin'][0], c['origin'][1],
                '--hidden',
                '--screenshot', str(native_png)
            ]
            for k, v in c['variables'].items():
                prev_cmd += ['--variable', k, str(v)]
            run_command(prev_cmd, EVIDENCE_DIR / f"{name}_preview.log", metadata['commands'])

        else:
            # Playback Dynamic Evaluation
            native_json = EVIDENCE_DIR / f"{name}_native.json"
            play_cmd = [
                str(BIN_PLAYBACK),
                str(c['asset']),
                str(c['input_json']),
                str(native_json)
            ]
            run_command(play_cmd, EVIDENCE_DIR / f"{name}_playback.log", metadata['commands'])
            n_data = json.load(native_json.open())
            trace_data = json.load(c['ref_trace'].open())
            g_comp = compare_playback_frame(n_data, trace_data)
            geo_json.write_text(json.dumps(g_comp, indent=2))
            geo_passed = bool(g_comp['passed'] and n_data['frame_vertices'] > 0 and
                              n_data['frame_indices'] > 0 and g_comp['commands'] and
                              all(command['samples'] > 0 for command in g_comp['commands']))
            case_res['geometry'] = {
                'passed': geo_passed,
                'frame_max_error_px': g_comp['frame_max_error_px'],
                'missing_reference_triangles': g_comp['missing_reference_triangles'],
                'unmatched_reference_vertices': g_comp['unmatched_reference_vertices'],
                'unused_native_triangles': g_comp['unused_native_triangles'],
                'unused_native_vertices': g_comp['unused_native_vertices'],
            }

            # Render Frame
            rend_cmd = [
                str(BIN_RENDER),
                str(c['asset']),
                str(native_json),
                str(native_png)
            ]
            run_command(rend_cmd, EVIDENCE_DIR / f"{name}_render.log", metadata['commands'])

            # Public observations must cover identical, nonempty step sequences.
            ref_meta = json.loads(c['ref_json'].read_text())
            case_res['scalar'] = compare_scalar_samples(ref_meta, n_data, name)

        # 3. Image Comparison
        im_report = compare_images_detailed(c['ref_png'], native_png, comp_base, tolerance=2)
        case_res['image'] = im_report
        case_res['native_hashes'] = {
            'png_sha256': sha256_file(native_png),
            'geo_sha256': sha256_file(geo_json),
        }
        if c['type'] == 'playback':
            case_res['native_hashes']['samples_sha256'] = sha256_file(native_json)

        # 4. Regional Crops & Side-by-Side Images
        crop_data = generate_region_crops(c['ref_png'], native_png, crops_dir, name, c['boxes'])
        case_res['crops'] = crop_data

        finalize_case(case_res)
        scalar_max = case_res['scalar']['max_error']
        print(f"    Verdict: {case_res['verdict']} | "
              f"GeoMax: {case_res['geometry']['frame_max_error_px']:.6e}px | "
              f"ScalarMax: {scalar_max!r} | "
              f"PixOverTol: {case_res['image']['pixels_over_tolerance']}")
        results.append(case_res)

    # 3. Write Complete Ledger
    ledger = {
        'generated_at': datetime.now(timezone.utc).isoformat(),
        'context': 'docs/agent-workboard.md V8',
        'overall_acceptance': suite_acceptance(results),
        'tool_metadata': metadata,
        'thresholds': {
            'geometry_px': 0.001,
            'topology': 'all matched',
            'scalar': 1e-05,
            'image_tolerance': 2,
            'pixels_over_tolerance': 0
        },
        'binary_hashes': binary_hashes,
        'asset_hashes': asset_hashes,
        'total_cases': len(results),
        'expected_failures_reproduced': sum(1 for r in results if r['expected_failure_reproduced']),
        'counterexamples_count': sum(1 for r in results if r['is_counterexample']),
        'standard_cases_passed': sum(1 for r in results if not r['is_counterexample'] and r['passed']),
        'standard_cases_failed': sum(1 for r in results if not r['is_counterexample'] and not r['passed']),
        'cases': results
    }

    ledger_path = EVIDENCE_DIR / 'v8_ledger.json'
    ledger_path.write_text(json.dumps(ledger, indent=2) + '\n')
    print(f"\n[+] Ledger written to {ledger_path}")
    print(f"[+] Summary: {ledger['standard_cases_passed']}/{len(results) - ledger['counterexamples_count']} standard cases PASSED. {ledger['expected_failures_reproduced']}/{ledger['counterexamples_count']} expected failures reproduced; acceptance={ledger['overall_acceptance']}.")

    os.close(lock_fd)
    return 0 if ledger['overall_acceptance'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
