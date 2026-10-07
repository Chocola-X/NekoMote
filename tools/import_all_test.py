#!/usr/bin/env python3
"""批量导入测试：从源 catalog 逐项驱动，尝试导入到临时目录，不发布到 assets/。

验证导入器能处理所有 catalog 记录。失败退出非零。
账本保留命令、节点、原因；超时和异常显式记录，不静默跳过。
"""
import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).parent / 'import_emote'))
import catalog_index


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
    try:
        catalog, catalog_hash = catalog_index.load_catalog(root)
    except (FileNotFoundError, json.JSONDecodeError) as error:
        parser.error(str(error))

    importer = args.importer.resolve()
    if not importer.is_file():
        parser.error(f'importer not found: {importer}')

    index = catalog_index.build_index(root)
    rows = []
    ok = fail = missing = 0

    with tempfile.TemporaryDirectory(prefix='nekomote_import_test_') as tmp_str:
        tmp = pathlib.Path(tmp_str)
        total = len(catalog)
        for i, (name, record) in enumerate(sorted(catalog.items()), 1):
            candidates = catalog_index.resolve_candidates(index, name)
            row = {'model': name, 'status': 'PENDING', 'errors': [],
                   'commands': [], 'nodes': []}

            if len(candidates) != 1:
                row['status'] = 'AMBIGUOUS' if candidates else 'MISSING'
                row['alternatives'] = [catalog_index.relative_to(p, root) for p in candidates]
                missing += 1
                rows.append(row)
                print(f'  [{i}/{total}] {row["status"]:12s} {name}', flush=True)
                continue

            source = candidates[0]
            resx = source.with_suffix('.resx.json')
            external = catalog_index.find_external_textures(source, source.parent)
            out = tmp / name.removesuffix('.pure')
            row['nodes'].append(catalog_index.relative_to(source, root))

            cmd = [str(importer), str(source), str(resx),
                   '--output', str(out), '--resource-root', str(source.parent)]
            for tex, path in external.items():
                if path is not None:
                    cmd.extend(['--texture', f'{tex}={path}'])
            row['commands'].append(cmd)

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
                row['errors'].append('importer timeout after 120s')
                fail += 1
            except Exception as e:
                row['status'] = 'ERROR'
                row['errors'].append(str(e)[:300])
                fail += 1

            rows.append(row)
            print(f'  [{i}/{total}] {row["status"]:12s} {name}', flush=True)

    summary = {
        'catalog_records': total,
        'catalog_sha256': catalog_hash,
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
