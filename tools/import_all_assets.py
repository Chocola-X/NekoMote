#!/usr/bin/env python3
"""批量导入所有模型到 assets/characters/，含 session 和 meta.json。"""
import os, sys, subprocess, json, re, shutil
from pathlib import Path
from concurrent.futures import ProcessPoolExecutor, as_completed

ROOT = Path(__file__).resolve().parent.parent
REFERENCE = Path("/run/media/chocola/DATA/Project/NekoWebShow")
MODEL_BASE = REFERENCE / "model_conversion"
IMPORT = ROOT / "build/release/bin/nekomote_import"
SESSION_TOOL = ROOT / "tools/import_session/import_session.py"
OUTPUT = ROOT / "assets/characters"
PRESENTATION = REFERENCE / "config/model-presentation.json"

with open(PRESENTATION) as f:
    PRESENTATION_DATA = json.load(f)

def find_models():
    models = []
    for char in sorted(os.listdir(MODEL_BASE)):
        char_dir = MODEL_BASE / char
        if not char_dir.is_dir():
            continue
        for dir_name in sorted(os.listdir(char_dir)):
            model_dir = char_dir / dir_name
            if not model_dir.is_dir():
                continue
            json_files = [f for f in os.listdir(model_dir)
                          if f.endswith('.json') and 'resx' not in f and 'pure' not in f]
            if not json_files:
                json_files = [f for f in os.listdir(model_dir)
                              if f.endswith('.json') and 'resx' not in f]
            resx_files = [f for f in os.listdir(model_dir)
                          if f.endswith('.resx.json') and 'pure' not in f]
            if not resx_files:
                resx_files = [f for f in os.listdir(model_dir)
                              if f.endswith('.resx.json')]
            if not json_files or not resx_files:
                continue
            json_name = json_files[0].replace('.json', '').replace('.pure', '')
            models.append({
                'char': char,
                'dir_name': dir_name,
                'model_name': json_name,
                'json': str(model_dir / json_files[0]),
                'resx': str(model_dir / resx_files[0]),
                'src_dir': str(model_dir),
            })
    return models

def find_external_textures(model):
    with open(model['json']) as f:
        d = json.load(f)
    source = d.get('source', {})
    if not isinstance(source, dict):
        return []
    pngs = [p for p in os.listdir(model['src_dir']) if p.lower().endswith('.png')]
    mappings = []
    for tex_name, tex_data in source.items():
        if not isinstance(tex_data, dict):
            continue
        texture = tex_data.get('texture', {})
        if not isinstance(texture, dict) or texture.get('pixel') is not None:
            continue
        tex_num = re.sub(r'[^\d]', '', tex_name)
        candidates = [p for p in pngs if f'tex{tex_num}' in p.lower()]
        if candidates:
            mappings.append(f'{tex_name}={candidates[0]}')
    return mappings

def import_one(model):
    name = model['model_name']
    out = OUTPUT / name
    if out.exists():
        return (name, 'SKIP', 'already exists')
    try:
        cmd = [str(IMPORT), model['json'], model['resx'],
               "--output", str(out), "--resource-root", model['src_dir']]
        for m in find_external_textures(model):
            cmd.extend(["--texture", m])
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        if proc.returncode != 0:
            return (name, 'IMPORT_FAIL', proc.stderr.strip()[:150])
        proc2 = subprocess.run(
            ["python3", str(SESSION_TOOL), "--reference-root", str(REFERENCE),
             "--model", model['model_name'], "--output", str(out / "session.nks.json")],
            capture_output=True, text=True, timeout=30
        )
        if proc2.returncode != 0:
            return (name, 'SESSION_FAIL', proc2.stderr.strip()[:150])
        cutoff = PRESENTATION_DATA.get(model['model_name'], {}).get('cutoffY')
        meta = {}
        if cutoff is not None:
            meta['cutoff_y'] = cutoff
        with open(out / "meta.json", 'w') as f:
            json.dump(meta, f, indent=2)
        return (name, 'OK', '')
    except Exception as e:
        return (name, 'ERROR', str(e)[:150])

def main():
    models = find_models()
    print(f"发现 {len(models)} 个模型，导入到 {OUTPUT}")
    results = {}
    ok = skip = fail = 0
    with ProcessPoolExecutor(max_workers=4) as pool:
        futures = {pool.submit(import_one, m): m for m in models}
        for i, fut in enumerate(as_completed(futures)):
            name, status, msg = fut.result()
            results[name] = (status, msg)
            if status == 'OK': ok += 1
            elif status == 'SKIP': skip += 1
            else: fail += 1
            if (i + 1) % 25 == 0 or i + 1 == len(models):
                print(f"  进度: {i+1}/{len(models)}  OK={ok}  SKIP={skip}  FAIL={fail}")
    print(f"\n=== 结果: OK={ok}  SKIP={skip}  FAIL={fail}  总计={len(models)} ===")
    if fail:
        print("\n失败:")
        for name in sorted(results):
            s, m = results[name]
            if s not in ('OK', 'SKIP'):
                print(f"  [{s}] {name}: {m}")

if __name__ == '__main__':
    main()
