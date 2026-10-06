#!/usr/bin/env python3
"""批量导入测试：遍历原项目所有模型，自动处理外部纹理，尝试导入到临时目录。"""
import os, sys, subprocess, tempfile, shutil, json, re
from pathlib import Path
from concurrent.futures import ProcessPoolExecutor, as_completed

REFERENCE = Path("/run/media/chocola/DATA/Project/NekoWebShow/model_conversion")
IMPORT = Path(__file__).resolve().parent.parent / "build/release/bin/nekomote_import"
TMP_ROOT = Path(tempfile.mkdtemp(prefix="nekomote_import_test_"))

def find_models():
    models = []
    for char in sorted(os.listdir(REFERENCE)):
        char_dir = REFERENCE / char
        if not char_dir.is_dir():
            continue
        for model_dir_name in sorted(os.listdir(char_dir)):
            model_dir = char_dir / model_dir_name
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
            models.append({
                'char': char,
                'dir_name': model_dir_name,
                'json': str(model_dir / json_files[0]),
                'resx': str(model_dir / resx_files[0]),
                'src_dir': str(model_dir),
            })
    return models

def find_external_textures(model):
    """读取模型 JSON，找出 pixel==null 的纹理，匹配目录下的 PNG 文件。"""
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
        if not isinstance(texture, dict):
            continue
        if texture.get('pixel') is not None:
            continue
        # pixel is null → external texture
        # tex_name like "tex#000", match PNG containing "tex000"
        tex_num = re.sub(r'[^\d]', '', tex_name)
        candidates = [p for p in pngs if f'tex{tex_num}' in p.lower()]
        if candidates:
            mappings.append(f'{tex_name}={candidates[0]}')
    return mappings

def import_one(model):
    name = f"{model['char']}/{model['dir_name']}"
    out = TMP_ROOT / model['char'] / model['dir_name']
    out.parent.mkdir(parents=True, exist_ok=True)
    try:
        cmd = [str(IMPORT), model['json'], model['resx'],
               "--output", str(out), "--resource-root", model['src_dir']]
        tex_mappings = find_external_textures(model)
        for m in tex_mappings:
            cmd.extend(["--texture", m])
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        if proc.returncode == 0:
            return (name, 'OK', '')
        return (name, 'FAIL', proc.stderr.strip()[:200] or proc.stdout.strip()[-200:])
    except subprocess.TimeoutExpired:
        return (name, 'TIMEOUT', '')
    except Exception as e:
        return (name, 'ERROR', str(e)[:200])

def main():
    models = find_models()
    print(f"发现 {len(models)} 个可导入模型")
    print(f"临时目录: {TMP_ROOT}")
    print()
    results = {}
    ok = 0
    fail = 0
    with ProcessPoolExecutor(max_workers=4) as pool:
        futures = {pool.submit(import_one, m): m for m in models}
        for i, fut in enumerate(as_completed(futures)):
            name, status, msg = fut.result()
            results[name] = (status, msg)
            if status == 'OK':
                ok += 1
            else:
                fail += 1
            if (i + 1) % 20 == 0 or i + 1 == len(models):
                print(f"  进度: {i+1}/{len(models)}  OK={ok}  FAIL={fail}")
    print()
    print(f"=== 结果: OK={ok}  FAIL={fail}  总计={len(models)} ===")
    if fail > 0:
        print("\n失败模型:")
        for name in sorted(results):
            status, msg = results[name]
            if status != 'OK':
                print(f"  [{status}] {name}: {msg}")
    shutil.rmtree(TMP_ROOT, ignore_errors=True)

if __name__ == '__main__':
    main()
