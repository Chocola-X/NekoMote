#!/usr/bin/env python3
"""批量导入测试：从源 catalog 逐项驱动，尝试导入到临时目录，不发布到 assets/。

验证导入器能处理所有 catalog 记录。失败退出非零。
"""
import argparse
import collections
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile


def build_index(reference_root):
    index = collections.defaultdict(list)
    for p in sorted((reference_root / 'model_conversion').rglob('*.json')):
        if not p.name.endswith('.resx.json'):
            index[p.stem.removesuffix('.pure')].append(p)
    return index


def resolve_candidates(index, name):
    key = name.removesuffix('.pure')
    candidates = index.get(key, [])
    canonical = [p for p in candidates if p.parent.name == key]
    if len(canonical) == 1:
        return canonical
    return candidates


def find_external_textures(source, src_dir):
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=pathlib.Path, required=True,
                        help='源仓库根目录（含 config/model-catalog.json 和 model_conversion/）')
    parser.add_argument('--importer', type=pathlib.Path, required=True,
                        help='nekomote_import 可执行文件')
    parser.add_argument('--report', type=pathlib.Path, default=None,
                        help='结果报告 JSON 路径（可选）')
    args = parser.parse_args()

    root = args.reference_root.resolve()
    catalog_path = root / 'config/model-catalog.json'
    if not catalog_path.is_file():
        parser.error(f'catalog not found: {catalog_path}')
    catalog = json.loads(catalog_path.read_text())

    importer = args.importer.resolve()
    if not importer.is_file():
        parser.error(f'importer not found: {importer}')

    index = build_index(root)
    rows = []
    ok = fail = missing = 0

    with tempfile.TemporaryDirectory(prefix='nekomote_import_test_') as tmp_str:
        tmp = pathlib.Path(tmp_str)
        for i, (name, record) in enumerate(sorted(catalog.items()), 1):
            candidates = resolve_candidates(index, name)
            row = {'model': name, 'status': 'PENDING', 'errors': []}

            if len(candidates) != 1:
                row['status'] = 'AMBIGUOUS' if candidates else 'MISSING'
                missing += 1
                rows.append(row)
                print(f'  [{i}/{len(catalog)}] {row["status"]:12s} {name}', flush=True)
                continue

            source = candidates[0]
            resx = source.with_suffix('.resx.json')
            external = find_external_textures(source, source.parent)
            out = tmp / name.removesuffix('.pure')

            cmd = [str(importer), str(source), str(resx),
                   '--output', str(out), '--resource-root', str(source.parent)]
            for tex, path in external.items():
                if path is not None:
                    cmd.extend(['--texture', f'{tex}={path}'])

            try:
                proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
                if proc.returncode == 0 and (out / 'model.nkm.json').is_file():
                    row['status'] = 'OK'
                    ok += 1
                else:
                    row['status'] = 'FAIL'
                    row['errors'].append(proc.stderr.strip()[:300] or proc.stdout.strip()[-300:])
                    fail += 1
            except subprocess.TimeoutExpired:
                row['status'] = 'TIMEOUT'
                fail += 1
            except Exception as e:
                row['status'] = 'ERROR'
                row['errors'].append(str(e)[:300])
                fail += 1

            rows.append(row)
            print(f'  [{i}/{len(catalog)}] {row["status"]:12s} {name}', flush=True)

    summary = {
        'catalog_records': len(catalog),
        'ok': ok,
        'fail': fail,
        'missing_or_ambiguous': missing,
    }
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({'summary': summary, 'models': rows},
                                          ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(summary, ensure_ascii=False, indent=2))

    if fail:
        sys.exit(1)


if __name__ == '__main__':
    main()
