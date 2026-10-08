#!/usr/bin/env python3
"""195 项原始 PSB 图集与解包 PNG 来源审计。

对每个 catalog 模型：
1. 读取 resx 的 Resources，定位全部解包 PNG
2. 用 Pillow 解码每个 PNG 为 RGBA8，记录尺寸/字节数/SHA256
3. 用 capture.py --trace-atlas-hashes 在浏览器 texImage2D 上传边界
   记录实际 RGBA8 图集的尺寸/字节数/SHA256
4. 按 rgba_sha256 关联每个上传与每个 resx 资源

账本保留 PSB/JSON/resx/PNG/工具/浏览器/binary hash、提交、命令、时间、
逐项状态与失败详情。已有输出拒绝覆盖，失败退出非零，不伪造零误差。
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).parent / '..' / 'import_emote'))
import catalog_index

try:
    from PIL import Image as PilImage
except ImportError:
    PilImage = None


def head_commit():
    try:
        proc = subprocess.run(
            ['git', 'rev-parse', 'HEAD'], capture_output=True, text=True, timeout=10,
            cwd=pathlib.Path(__file__).resolve().parents[2])
        if proc.returncode == 0:
            return proc.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        pass
    return None


def tool_hash(path):
    if path is None:
        return None
    resolved = pathlib.Path(path)
    if not resolved.is_file():
        return None
    try:
        return catalog_index.sha256(resolved)
    except OSError:
        return None


def decode_png_rgba8(path):
    """解码 PNG 为 RGBA8 字节，返回 (width, height, data, sha256_hex)。

    同时计算垂直翻转版本（OpenGL 纹理坐标原点在左下角），
    返回 (width, height, rgba_sha256, flipped_sha256, byte_count)。
    """
    if PilImage is None:
        raise RuntimeError('Pillow is required for PNG decoding')
    img = PilImage.open(path).convert('RGBA')
    width, height = img.width, img.height
    data = img.tobytes()
    sha = hashlib.sha256(data).hexdigest()
    row = width * 4
    flipped = b''.join(data[y * row:(y + 1) * row] for y in range(height - 1, -1, -1))
    flipped_sha = hashlib.sha256(flipped).hexdigest()
    return width, height, sha, flipped_sha, len(data)


def parse_resx_resources(resx_path):
    """读取 resx 的 Resources 字段，返回 dict[key -> absolute png path]。"""
    document = json.loads(resx_path.read_text())
    resources = {}
    for key, value in document.get('Resources', {}).items():
        resources[key] = (resx_path.parent / value).resolve()
    return resources


def associate_atlas(png_records, upload_records):
    """按 rgba_sha256 关联 PNG 与浏览器上传。

    每条 png_record: {resx_key, path, sha256, width, height,
                      rgba_sha256, flipped_sha256, rgba_bytes, status}
    每条 upload_record: {width, height, bytes, rgba_sha256}

    匹配规则：upload.rgba_sha256 == png.rgba_sha256
              或 upload.rgba_sha256 == png.flipped_sha256
    返回 (overall_status, match_details, errors)
    """
    errors = []
    matches = []
    matched_png = set()
    matched_upload = set()

    for ui, upload in enumerate(upload_records):
        upload_hash = upload['rgba_sha256']
        found = False
        for pi, png in enumerate(png_records):
            if pi in matched_png:
                continue
            if upload_hash == png['rgba_sha256']:
                matches.append({'png_index': pi, 'upload_index': ui,
                                'rgba_sha256': upload_hash,
                                'orientation': 'normal', 'status': 'MATCH'})
                matched_png.add(pi)
                matched_upload.add(ui)
                found = True
                break
            if upload_hash == png['flipped_sha256']:
                matches.append({'png_index': pi, 'upload_index': ui,
                                'rgba_sha256': upload_hash,
                                'orientation': 'flipped', 'status': 'MATCH'})
                matched_png.add(pi)
                matched_upload.add(ui)
                found = True
                break
        if not found:
            matches.append({'png_index': None, 'upload_index': ui,
                            'rgba_sha256': upload_hash,
                            'orientation': None, 'status': 'EXTRA_UPLOAD'})

    for pi, png in enumerate(png_records):
        if pi not in matched_png:
            matches.append({'png_index': pi, 'upload_index': None,
                            'rgba_sha256': png['rgba_sha256'],
                            'orientation': None, 'status': 'MISSING_UPLOAD'})

    if not png_records and not upload_records:
        return 'EMPTY', matches, ['no PNG resources and no atlas uploads']
    if not png_records:
        return 'NO_PNG', matches, ['resx has no PNG resources']
    if not upload_records:
        return 'NO_UPLOAD', matches, ['browser produced no atlas uploads']

    unmatched_png = len(png_records) - len(matched_png)
    unmatched_upload = len(upload_records) - len(matched_upload)

    if unmatched_png == 0 and unmatched_upload == 0:
        return 'MATCH', matches, errors
    if unmatched_png > 0 and unmatched_upload > 0:
        return 'MISMATCH', matches, errors
    if unmatched_png > 0:
        return 'MISSING_UPLOAD', matches, errors
    return 'EXTRA_UPLOAD', matches, errors


def run_capture(capture_tool, reference_root, model, browser, output_prefix, timeout):
    """运行 capture.py --trace-atlas-hashes，返回 (atlas_uploads, detail)。"""
    cmd = ['python3', str(capture_tool),
           '--reference-root', str(reference_root),
           '--model', model,
           '--browser', str(browser),
           '--output', str(output_prefix),
           '--trace-atlas-hashes',
           '--steps', '0']
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, {'command': cmd, 'reason': 'capture timeout'}
    detail = {'command': cmd, 'returncode': proc.returncode,
              'stdout_tail': proc.stdout[-300:], 'stderr_tail': proc.stderr[-300:]}
    if proc.returncode != 0:
        return None, detail
    json_output = pathlib.Path(str(output_prefix) + '.json')
    if not json_output.is_file():
        detail['reason'] = 'capture output json missing'
        return None, detail
    try:
        result = json.loads(json_output.read_text())
    except (json.JSONDecodeError, OSError) as error:
        detail['reason'] = f'cannot parse capture output: {error}'
        return None, detail
    return result.get('atlasUploadHashes', []), detail


def audit_one(name, record, reference_root, index, capture_tool, browser,
              output_dir, timeout):
    """审计单个模型的图集来源。"""
    row = {
        'model': name,
        'character': record.get('character'),
        'costume': record.get('costume'),
        'status': 'PENDING',
        'errors': [],
        'commands': [],
    }
    try:
        catalog_index.validate_model_name(name)
    except ValueError as error:
        row['status'] = 'ERROR'
        row['errors'].append(str(error))
        return row

    key = name.removesuffix('.pure')
    candidates = catalog_index.resolve_candidates(index, name)
    if len(candidates) != 1:
        row['status'] = 'AMBIGUOUS' if candidates else 'MISSING'
        row['errors'].append('missing or ambiguous unpacked model')
        row['alternatives'] = [catalog_index.relative_to(p, reference_root)
                               for p in candidates]
        return row

    source = candidates[0]
    resx = source.with_suffix('.resx.json')
    row['source'] = catalog_index.relative_to(source, reference_root)
    row['source_sha256'] = catalog_index.sha256(source)
    row['resx'] = catalog_index.relative_to(resx, reference_root) if resx.is_file() else None
    row['resx_sha256'] = catalog_index.sha256(resx) if resx.is_file() else None

    psb = reference_root / 'data' / (key + '.pure.psb.zip')
    if psb.is_file():
        row['psb'] = catalog_index.relative_to(psb, reference_root)
        row['psb_sha256'] = catalog_index.sha256(psb)
    else:
        row['psb'] = None

    if not resx.is_file():
        row['status'] = 'NO_RESX'
        row['errors'].append('resx file missing')
        return row

    try:
        resources = parse_resx_resources(resx)
    except (json.JSONDecodeError, OSError) as error:
        row['status'] = 'ERROR'
        row['errors'].append(f'cannot parse resx: {error}')
        return row

    png_records = []
    for resx_key in sorted(resources):
        png_path = resources[resx_key]
        entry = {'resx_key': resx_key,
                 'path': catalog_index.relative_to(png_path, reference_root)}
        if not png_path.is_file():
            entry['status'] = 'PNG_MISSING'
            row['errors'].append(f'PNG missing for resx key {resx_key}')
            png_records.append(entry)
            continue
        entry['sha256'] = catalog_index.sha256(png_path)
        try:
            width, height, sha, flipped_sha, byte_count = decode_png_rgba8(png_path)
            entry.update({'width': width, 'height': height,
                          'rgba_sha256': sha, 'flipped_sha256': flipped_sha,
                          'rgba_bytes': byte_count, 'status': 'OK'})
        except (OSError, RuntimeError) as error:
            entry['status'] = 'PNG_DECODE_ERROR'
            row['errors'].append(f'PNG decode error for {resx_key}: {error}')
        png_records.append(entry)
    row['png_resources'] = png_records

    if not png_records:
        row['status'] = 'NO_PNG'
        row['errors'].append('resx has no PNG resources')
        return row

    capture_output = output_dir / (key + '.capture')
    atlas_uploads, capture_detail = run_capture(
        capture_tool, reference_root, name, browser, capture_output, timeout)
    row['commands'].append(capture_detail.get('command', []))
    row['capture_detail'] = {k: v for k, v in capture_detail.items() if k != 'command'}

    if atlas_uploads is None:
        row['status'] = 'CAPTURE_ERROR'
        row['atlas_uploads'] = []
        row['errors'].append(capture_detail.get('reason', 'capture failed'))
        return row

    row['atlas_uploads'] = atlas_uploads
    overall, matches, assoc_errors = associate_atlas(
        [p for p in png_records if p.get('status') == 'OK'], atlas_uploads)
    row['status'] = overall
    row['matches'] = matches
    row['errors'].extend(assoc_errors)
    return row


def build_summary(rows):
    summary = {'total': len(rows)}
    for r in rows:
        st = r['status']
        summary[st] = summary.get(st, 0) + 1
    return summary


def save_report(report, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + '.tmp')
    tmp.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    tmp.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=pathlib.Path, required=True,
                        help='源仓库根目录（含 config/model-catalog.json）')
    parser.add_argument('--capture-tool', type=pathlib.Path, required=True,
                        help='capture.py 脚本路径')
    parser.add_argument('--browser', type=pathlib.Path, required=True,
                        help='Chromium 可执行文件路径')
    parser.add_argument('--output', type=pathlib.Path, required=True,
                        help='审计输出目录（存放 capture 中间文件）')
    parser.add_argument('--report', type=pathlib.Path, required=True,
                        help='账本输出 JSON 路径')
    parser.add_argument('--timeout', type=int, default=60,
                        help='单个模型捕获超时秒数（默认 60）')
    args = parser.parse_args()
    if not 1 <= args.timeout <= 600:
        parser.error('--timeout requires 1..600')

    if PilImage is None:
        parser.error('Pillow is required: pip install Pillow')

    root = args.reference_root.resolve()
    try:
        catalog, catalog_hash = catalog_index.load_catalog(root)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    output = args.output.resolve()
    if output.exists():
        parser.error(f'output already exists, use a new --output: {output}')
    output.mkdir(parents=True)

    report_path = args.report.resolve()
    if report_path.exists():
        parser.error(f'report already exists, use a new --report: {report_path}')

    index = catalog_index.build_index(root)
    capture_tool = args.capture_tool.resolve()
    browser = args.browser.resolve()

    provenance = {
        'head_commit': head_commit(),
        'generated_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'tool_hashes': {
            'capture_tool': tool_hash(capture_tool),
            'audit_tool': catalog_index.sha256(pathlib.Path(__file__)),
        },
        'browser_path': str(browser),
        'timeout': args.timeout,
    }

    rows = []
    fail_count = 0
    total = len(catalog)
    for i, (name, record) in enumerate(sorted(catalog.items()), 1):
        row = audit_one(name, record, root, index, capture_tool, browser,
                        output, args.timeout)
        rows.append(row)
        st = row['status']
        if st != 'MATCH':
            fail_count += 1
        print(f'  [{i}/{total}] {st:16s} {name}', flush=True)
        checkpoint = report_path.with_name(report_path.name + '.checkpoint')
        save_report({'complete': False, 'catalog_sha256': catalog_hash,
                     **provenance, 'summary': build_summary(rows), 'models': rows},
                    checkpoint)

    summary = build_summary(rows)
    report = {'complete': True, 'catalog_records': total,
              'catalog_sha256': catalog_hash, **provenance,
              'summary': summary, 'models': rows}
    save_report(report, report_path)
    checkpoint = report_path.with_name(report_path.name + '.checkpoint')
    if checkpoint.exists():
        checkpoint.unlink()
    print(json.dumps(summary, ensure_ascii=False, indent=2))

    if fail_count:
        print(f'\n{fail_count} 项未匹配，退出非零', file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
