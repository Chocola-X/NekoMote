#!/usr/bin/env python3
"""从源 catalog 逐项驱动事务导入，生成 195 行账本。

每项记录 catalog ID、角色/型号、source/resx/纹理来源及 hash；
新 bundle 先写到临时暂存目录，全部验证后一次发布到目标。
失败不留 partial 目录，进程退出非零。已有目标一律拒绝并提示使用新目标，
不凭文件存在推断当前 schema 有效。保护用户已有生成目录和其中额外文件。
"""
import argparse
import collections
import ctypes
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def publish_bundle(staging, target):
    """Publish on Linux atomically, without replacing even an empty existing target."""
    rename = ctypes.CDLL(None, use_errno=True).renameat2
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int,
                       ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    at_fdcwd = -100
    rename_noreplace = 1
    if rename(at_fdcwd, os.fsencode(staging), at_fdcwd, os.fsencode(target),
              rename_noreplace) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), str(target))


def build_index(reference_root):
    """索引所有解包模型 JSON，按 stem 去除 .pure 后缀分组。"""
    index = collections.defaultdict(list)
    for p in sorted((reference_root / 'model_conversion').rglob('*.json')):
        if not p.name.endswith('.resx.json'):
            index[p.stem.removesuffix('.pure')].append(p)
    return index


def resolve_candidates(index, name):
    """复用 audit_catalog 的 canonical 选择逻辑：优先父目录名等于 key 的候选。"""
    key = name.removesuffix('.pure')
    candidates = index.get(key, [])
    canonical = [p for p in candidates if p.parent.name == key]
    if len(canonical) == 1:
        return canonical
    return candidates


def find_external_textures(source, src_dir):
    """读取模型 JSON，找出 pixel==null 的纹理，精确匹配目录下的 PNG。"""
    document = json.loads(source.read_text())
    external = {}
    for texture, value in document.get('source', {}).items():
        if not isinstance(value, dict):
            continue
        tex = value.get('texture', {})
        if not isinstance(tex, dict) or tex.get('pixel') is not None:
            continue
        suffix = 'tex' + (texture.split('#')[-1] if '#' in texture else '000') + '.png'
        matches = sorted(src_dir.glob('*' + suffix))
        if len(matches) == 1:
            external[texture] = str(matches[0].relative_to(src_dir))
        else:
            external[texture] = None
    return external


def import_one(name, record, reference_root, importer, session_tool,
               presentation, output, temporary, runtime_auditor, runtime_steps):
    """事务导入单个模型：先写到暂存目录，验证后发布到目标。"""
    key = name.removesuffix('.pure')
    target = output / key
    row = {
        'model': name,
        'character': record.get('character'),
        'costume': record.get('costume'),
        'variants': record.get('variants', []),
        'config': record.get('config'),
        'status': {'structure': 'PENDING', 'cpu': 'PENDING',
                   'visual': 'PENDING', 'interaction': 'PENDING', 'soak': 'PENDING'},
        'errors': [],
    }

    if os.path.lexists(target):
        row['status']['structure'] = 'EXISTS_UNCHECKED'
        row['errors'].append('existing output preserved without certification; use a new --output')
        return row

    index = build_index(reference_root)
    candidates = resolve_candidates(index, name)
    alternatives = [str(p.relative_to(reference_root)) for p in candidates]

    if len(candidates) != 1:
        row['status']['structure'] = 'AMBIGUOUS' if candidates else 'MISSING'
        row['errors'].append('missing or ambiguous unpacked model')
        row['alternatives'] = alternatives
        return row

    source = candidates[0]
    resx = source.with_suffix('.resx.json')
    row['source'] = str(source.relative_to(reference_root))
    row['source_sha256'] = sha256(source)
    row['resx'] = str(resx.relative_to(reference_root)) if resx.is_file() else None
    row['resx_sha256'] = sha256(resx) if resx.is_file() else None

    psb = reference_root / 'data' / (key + '.pure.psb.zip')
    if psb.is_file():
        row['psb'] = str(psb.relative_to(reference_root))
        row['psb_sha256'] = sha256(psb)
    else:
        row['psb'] = None

    external = find_external_textures(source, source.parent)
    row['external_textures'] = external
    if any(v is None for v in external.values()):
        row['errors'].append('missing or ambiguous external texture')

    staging = temporary / key

    cmd = [str(importer), str(source), str(resx),
           '--output', str(staging), '--resource-root', str(source.parent)]
    for tex, path in external.items():
        if path is not None:
            cmd.extend(['--texture', f'{tex}={path}'])
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if proc.returncode != 0:
        row['status']['structure'] = 'FAIL'
        row['errors'].append(proc.stderr.strip()[:300] or proc.stdout.strip()[-300:])
        shutil.rmtree(staging, ignore_errors=True)
        return row

    if not (staging / 'model.nkm.json').is_file():
        row['status']['structure'] = 'FAIL'
        row['errors'].append('importer succeeded but model.nkm.json missing')
        shutil.rmtree(staging, ignore_errors=True)
        return row

    if session_tool is not None:
        cmd2 = ['python3', str(session_tool), '--reference-root', str(reference_root),
                '--model', name, '--output', str(staging / 'session.nks.json')]
        proc2 = subprocess.run(cmd2, capture_output=True, text=True, timeout=60)
        if proc2.returncode != 0:
            row['status']['structure'] = 'SESSION_FAIL'
            row['errors'].append(proc2.stderr.strip()[:300])
            shutil.rmtree(staging, ignore_errors=True)
            return row

    cutoff = presentation.get(name, {}).get('cutoffY')
    meta = {}
    if cutoff is not None:
        meta['cutoff_y'] = cutoff
    (staging / 'meta.json').write_text(json.dumps(meta, indent=2) + '\n')

    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        publish_bundle(staging, target)
    except (OSError, AttributeError) as error:
        row['status']['structure'] = 'FAIL'
        row['errors'].append(f'cannot publish bundle without replacement: {error}')
        return row
    row['status']['structure'] = 'OK'

    if runtime_auditor is not None:
        audit_output = temporary / (key + '.runtime.json')
        row['status']['cpu'] = run_cpu_audit(
            runtime_auditor, source, resx, external, audit_output, runtime_steps)

    return row


def run_cpu_audit(auditor, source, resx, external, audit_output, steps):
    cmd = [str(auditor), str(source), str(resx),
           '--output', str(audit_output),
           '--steps', str(steps)]
    for tex, path in external.items():
        if path is not None:
            cmd.extend(['--texture', f'{tex}={path}'])
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if proc.returncode != 0:
        return 'FAIL'
    return 'OK'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=pathlib.Path, required=True,
                        help='源仓库根目录（含 config/model-catalog.json 和 model_conversion/）')
    parser.add_argument('--importer', type=pathlib.Path, required=True,
                        help='nekomote_import 可执行文件')
    parser.add_argument('--output', type=pathlib.Path, required=True,
                        help='导入输出目录（如 assets/characters/）')
    parser.add_argument('--session-tool', type=pathlib.Path, default=None,
                        help='import_session.py 脚本（可选）')
    parser.add_argument('--presentation', type=pathlib.Path, default=None,
                        help='model-presentation.json（可选，用于 cutoffY）')
    parser.add_argument('--runtime-auditor', type=pathlib.Path, default=None,
                        help='runtime_auditor 可执行文件（可选，CPU 物理采样）')
    parser.add_argument('--runtime-steps', type=int, default=120)
    parser.add_argument('--report', type=pathlib.Path, default=None,
                        help='账本输出 JSON 路径（默认 stdout 摘要）')
    args = parser.parse_args()
    if not 1 <= args.runtime_steps <= 36000:
        parser.error('--runtime-steps requires 1..36000')

    root = args.reference_root.resolve()
    catalog_path = root / 'config/model-catalog.json'
    if not catalog_path.is_file():
        parser.error(f'catalog not found: {catalog_path}')
    catalog = json.loads(catalog_path.read_text())
    catalog_hash = hashlib.sha256(catalog_path.read_bytes()).hexdigest()

    presentation = {}
    if args.presentation is not None:
        presentation = json.loads(args.presentation.read_text())

    importer = args.importer.resolve()
    if not importer.is_file():
        parser.error(f'importer not found: {importer}')

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    rows = []
    fail_count = 0
    with tempfile.TemporaryDirectory(prefix='.nekomote-import-staging-', dir=output) as temporary:
        tmp = pathlib.Path(temporary)
        for i, (name, record) in enumerate(sorted(catalog.items()), 1):
            row = import_one(name, record, root, importer, args.session_tool,
                             presentation, output, tmp,
                             args.runtime_auditor, args.runtime_steps)
            rows.append(row)
            st = row['status']['structure']
            if st != 'OK' or row['status']['cpu'] == 'FAIL':
                fail_count += 1
            print(f'  [{i}/{len(catalog)}] {st:12s} {name}', flush=True)

    summary = {
        'catalog_records': len(catalog),
        'catalog_sha256': catalog_hash,
        'structure_ok': sum(1 for r in rows if r['status']['structure'] == 'OK'),
        'structure_existing_unchecked': sum(
            1 for r in rows if r['status']['structure'] == 'EXISTS_UNCHECKED'),
        'structure_fail': sum(1 for r in rows if r['status']['structure'] == 'FAIL'),
        'structure_missing': sum(1 for r in rows if r['status']['structure'] == 'MISSING'),
        'structure_ambiguous': sum(1 for r in rows if r['status']['structure'] == 'AMBIGUOUS'),
        'session_fail': sum(1 for r in rows if r['status']['structure'] == 'SESSION_FAIL'),
    }
    report = {'summary': summary, 'models': rows}

    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(summary, ensure_ascii=False, indent=2))

    if fail_count:
        print(f'\n{fail_count} 项失败或缺失，退出非零', file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
