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
import pathlib
import shutil
import subprocess
import sys
import zipfile

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
    """解码 PNG 为 RGBA8，同时计算垂直翻转版本，
    返回 (width, height, rgba_sha256, flipped_sha256, byte_count)。
    """
    if PilImage is None:
        raise RuntimeError('Pillow is required for PNG decoding')
    with PilImage.open(path) as source:
        if source.format != 'PNG':
            raise ValueError('resource is not a PNG image')
        img = source.convert('RGBA')
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
    if not isinstance(document, dict) or not isinstance(document.get('Resources'), dict):
        raise ValueError('resx Resources must be an object')
    resources = {}
    for key, value in document['Resources'].items():
        if not isinstance(value, str) or not value:
            raise ValueError(f'resx resource {key!r} must be a nonempty path')
        resources[key] = (resx_path.parent / value).resolve()
    return resources


def valid_upload(record):
    if not isinstance(record, dict):
        return False
    width, height, size = (record.get(k) for k in ('width', 'height', 'bytes'))
    sha = record.get('rgba_sha256')
    return (type(width) is int and type(height) is int and type(size) is int and
            width > 0 and height > 0 and size == width * height * 4 and
            isinstance(sha, str) and len(sha) == 64 and
            all(c in '0123456789abcdef' for c in sha))


def associate_atlas(png_records, upload_records):
    """按 rgba_sha256 关联 PNG 与浏览器上传。

    每条 png_record: {resx_key, path, sha256, width, height,
                      rgba_sha256, flipped_sha256, rgba_bytes, status}
    每条 upload_record: {width, height, bytes, rgba_sha256}

    匹配要求尺寸和 RGBA8 字节数相同，再检查正常或垂直翻转字节 SHA256。
    返回 (overall_status, match_details, errors)
    """
    errors = []
    matches = []
    matched_png = set()
    matched_upload = set()

    if not isinstance(upload_records, list) or any(not valid_upload(u) for u in upload_records):
        return 'INVALID_UPLOAD', [], ['atlas uploads require RGBA8 dimensions, bytes and SHA256']
    if any(p.get('status') != 'OK' for p in png_records):
        return 'PNG_ERROR', [], ['one or more PNG resources could not be decoded']

    for ui, upload in enumerate(upload_records):
        upload_hash = upload['rgba_sha256']
        found = False
        for pi, png in enumerate(png_records):
            if pi in matched_png:
                continue
            if (upload['width'] != png['width'] or upload['height'] != png['height'] or
                    upload['bytes'] != png['rgba_bytes']):
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
    cmd = [sys.executable, str(capture_tool),
           '--reference-root', str(reference_root),
           '--model', model,
           '--browser', str(browser),
           '--output', str(output_prefix),
           '--trace-atlas-hashes',
           '--steps', '0']
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError) as error:
        return None, {'command': cmd, 'reason': f'capture failed: {error}'}
    detail = {'command': cmd, 'returncode': proc.returncode,
              'stdout_tail': proc.stdout[-300:], 'stderr_tail': proc.stderr[-300:]}
    if proc.returncode != 0:
        return None, detail
    json_output = pathlib.Path(str(output_prefix) + '.json')
    if not json_output.is_file():
        detail['reason'] = 'capture output json missing'
        return None, detail
    png_output = pathlib.Path(str(output_prefix) + '.png')
    try:
        metadata_bytes = json_output.read_bytes()
        result = json.loads(metadata_bytes)
        if not isinstance(result, dict):
            raise ValueError('capture metadata must be an object')
        if result.get('model') != model or result.get('glError') != 0:
            raise ValueError('capture model mismatch or missing/nonzero WebGL status')
        if not isinstance(result.get('atlasUploadHashes'), list):
            raise ValueError('capture atlasUploadHashes must be an array')
        if not isinstance(result.get('source_hashes'), dict):
            raise ValueError('capture source hashes are missing')
        detail.update({'metadata_path': str(json_output),
                       'metadata_sha256': hashlib.sha256(metadata_bytes).hexdigest(),
                       'png_path': str(png_output), 'png_sha256': catalog_index.sha256(png_output),
                       'source_hashes': result['source_hashes'],
                       'renderer': result.get('renderer'), 'version': result.get('version'),
                       'glError': result['glError']})
    except (ValueError, OSError) as error:
        detail['reason'] = f'cannot parse capture output: {error}'
        return None, detail
    return result['atlasUploadHashes'], detail


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
        row['status'] = 'NO_PSB'
        row['errors'].append('source PSB archive missing')
        return row
    try:
        with zipfile.ZipFile(psb) as archive:
            entries = [p for p in archive.namelist() if p.endswith('.psb')]
            if len(entries) != 1:
                raise ValueError('expected exactly one PSB archive member')
            row['psb_member'] = entries[0]
            row['psb_payload_sha256'] = hashlib.sha256(archive.read(entries[0])).hexdigest()
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        row['status'] = 'PSB_ERROR'
        row['errors'].append(f'cannot read PSB archive: {error}')
        return row

    if not resx.is_file():
        row['status'] = 'NO_RESX'
        row['errors'].append('resx file missing')
        return row

    try:
        resources = parse_resx_resources(resx)
    except (ValueError, OSError) as error:
        row['status'] = 'ERROR'
        row['errors'].append(f'cannot parse resx: {error}')
        return row

    row['resource_mapping'] = 'resx'
    if not resources:
        row['resource_mapping'] = 'model_external_textures'
        try:
            external = catalog_index.find_external_textures(source, source.parent)
            if not external or any(path is None for path in external.values()):
                row['status'] = 'SOURCE_MAPPING_UNRESOLVED'
                row['errors'].append('empty resx resources; external texture mapping is absent or ambiguous')
                row['external_texture_candidates'] = external
                return row
            resources = {texture: (source.parent / path).resolve()
                         for texture, path in external.items()}
        except (OSError, ValueError, AttributeError, TypeError) as error:
            row['status'] = 'SOURCE_MAPPING_UNRESOLVED'
            row['errors'].append(f'cannot resolve external textures: {error}')
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
        except (OSError, RuntimeError, ValueError) as error:
            entry['status'] = 'PNG_DECODE_ERROR'
            row['errors'].append(f'PNG decode error for {resx_key}: {error}')
        png_records.append(entry)
    row['png_resources'] = png_records

    if any(p.get('status') != 'OK' for p in png_records):
        row['status'] = 'PNG_ERROR'
        return row

    capture_output = output_dir / (key + '.capture')
    atlas_uploads, capture_detail = run_capture(
        capture_tool, reference_root, key, browser, capture_output, timeout)
    row['commands'].append(capture_detail.get('command', []))
    row['capture_detail'] = {k: v for k, v in capture_detail.items() if k != 'command'}

    if atlas_uploads is None:
        row['status'] = 'CAPTURE_ERROR'
        row['atlas_uploads'] = []
        row['errors'].append(capture_detail.get('reason', 'capture failed'))
        return row

    row['atlas_uploads'] = atlas_uploads
    hashes = capture_detail.get('source_hashes', {})
    if (hashes.get('psb') != row['psb_payload_sha256'] or
            hashes.get('original_psb') != row['psb_payload_sha256']):
        row['status'] = 'CAPTURE_PROVENANCE_ERROR'
        row['errors'].append('capture PSB payload hashes do not match the selected source archive')
        return row
    for driver in ('FreeMoteDriver.js', 'emoteplayer.js'):
        expected_hash = tool_hash(reference_root / 'driver' / driver)
        if expected_hash is None or hashes.get(driver) != expected_hash:
            row['status'] = 'CAPTURE_PROVENANCE_ERROR'
            row['errors'].append(f'capture driver hash mismatch: {driver}')
            return row
    overall, matches, assoc_errors = associate_atlas(
        png_records, atlas_uploads)
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


def collect_provenance(capture_tool, browser, reference_root):
    capture_dependencies = ('capture.cjs', 'reference.html', 'trace.js', 'psb_fixture.py',
                            'package.json', 'package-lock.json')
    paths = {'audit_tool': pathlib.Path(__file__).resolve(), 'capture_tool': capture_tool,
             'catalog_index': pathlib.Path(catalog_index.__file__).resolve(),
             'python_binary': pathlib.Path(sys.executable).resolve(), 'browser_binary': browser}
    paths.update({name: capture_tool.parent / name for name in capture_dependencies})
    paths.update({name: reference_root / 'driver' / name
                  for name in ('FreeMoteDriver.js', 'emoteplayer.js')})
    node = shutil.which('node')
    if node:
        paths['node_binary'] = pathlib.Path(node).resolve()
    versions = {}
    for name, executable in (('browser', browser), ('node', node)):
        if executable:
            try:
                proc = subprocess.run([str(executable), '--version'], capture_output=True,
                                      text=True, timeout=10)
                versions[name] = {'returncode': proc.returncode, 'stdout': proc.stdout.strip(),
                                  'stderr': proc.stderr.strip()}
            except (OSError, subprocess.SubprocessError) as error:
                versions[name] = {'error': str(error)}
    return {'head_commit': head_commit(),
            'generated_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'tool_hashes': {name: tool_hash(path) for name, path in paths.items()},
            'tool_paths': {name: str(path) for name, path in paths.items()},
            'browser_path': str(browser), 'versions': versions,
            'python_version': sys.version, 'pillow_version': PilImage.__version__,
            'command': [sys.executable, str(pathlib.Path(__file__).resolve()), *sys.argv[1:]]}


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

    report_path = args.report.resolve()
    if report_path.exists():
        parser.error(f'report already exists, use a new --report: {report_path}')
    checkpoint = report_path.with_name(report_path.name + '.checkpoint')
    if checkpoint.exists():
        parser.error(f'checkpoint already exists, use a new --report: {checkpoint}')
    if report_path == output:
        parser.error('--report must not be the --output directory')

    index = catalog_index.build_index(root)
    capture_tool = args.capture_tool.resolve()
    browser = args.browser.resolve()
    if not capture_tool.is_file() or not browser.is_file():
        parser.error('capture tool and browser must be existing files')
    provenance = collect_provenance(capture_tool, browser, root)
    provenance['timeout'] = args.timeout
    required = ('capture_tool', 'browser_binary', 'FreeMoteDriver.js', 'emoteplayer.js',
                'capture.cjs', 'reference.html', 'trace.js', 'psb_fixture.py',
                'package.json', 'package-lock.json', 'node_binary')
    if any(provenance['tool_hashes'].get(key) is None for key in required):
        parser.error('required capture dependency or driver is missing')
    output.mkdir(parents=True)

    rows = []
    fail_count = 0
    total = len(catalog)
    for i, (name, record) in enumerate(sorted(catalog.items()), 1):
        try:
            row = audit_one(name, record, root, index, capture_tool, browser,
                            output, args.timeout)
        except (OSError, ValueError, TypeError, KeyError, zipfile.BadZipFile) as error:
            row = {'model': name, 'character': record.get('character'),
                   'costume': record.get('costume'), 'status': 'ERROR',
                   'commands': [], 'errors': [f'audit source/capture error: {error}']}
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
