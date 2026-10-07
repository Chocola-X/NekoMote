#!/usr/bin/env python3
"""从源 catalog 逐项驱动事务导入，生成 195 行账本。

每项记录 catalog ID、角色/型号、source/resx/纹理来源及 hash、执行的命令、
节点路径、失败原因；新 bundle 先写到临时暂存目录，全部验证后一次发布到目标。
失败不留 partial 目录，进程退出非零。已有目标一律拒绝并提示使用新目标，
不凭文件存在推断当前 schema 有效。保护用户已有生成目录和其中额外文件。

账本保留全部 195 条产品项，结构/CPU/视觉/交互/soak 分别统计；
未运行状态保持 PENDING，不填 OK，结构 OK 不代表会话或画面可用。
中断时通过原子检查点文件保留已完成项，重跑使用新输出目录。
"""
import argparse
import ctypes
import datetime
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).parent / 'import_emote'))
import catalog_index


def head_commit():
    """返回当前仓库 HEAD commit hash，不在 git 仓库中或失败时返回 None。"""
    try:
        proc = subprocess.run(
            ['git', 'rev-parse', 'HEAD'], capture_output=True, text=True, timeout=10)
        if proc.returncode == 0:
            return proc.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        pass
    return None


def tool_hash(path):
    """计算可执行文件 sha256，缺失或不可读返回 None。"""
    if path is None:
        return None
    resolved = pathlib.Path(path)
    if not resolved.is_file():
        return None
    return catalog_index.sha256(resolved)


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


def bundle_schema_version(bundle):
    """读取已发布 bundle 的 model.nkm.json schema 版本，缺失返回 None。"""
    manifest = bundle / 'model.nkm.json'
    if not manifest.is_file():
        return None
    try:
        return json.loads(manifest.read_text()).get('schema')
    except (json.JSONDecodeError, OSError):
        return None


def bundle_hash(bundle):
    """计算 bundle 内全部文件的 sha256 摘要（路径→hash），按相对路径排序。"""
    if not bundle.is_dir():
        return None
    digest = {}
    for p in sorted(bundle.rglob('*')):
        if p.is_file():
            rel = str(p.relative_to(bundle))
            digest[rel] = catalog_index.sha256(p)
    return digest


def run_cpu_audit(auditor, source, resx, external, audit_output, steps):
    """运行 CPU 物理采样，返回 (status, detail)。detail 保留 stdout/stderr 摘要。"""
    cmd = [str(auditor), str(source), str(resx),
           '--output', str(audit_output),
           '--steps', str(steps)]
    for tex, path in external.items():
        if path is not None:
            cmd.extend(['--texture', f'{tex}={path}'])
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return 'TIMEOUT', {'command': cmd, 'reason': 'runtime audit timeout'}
    detail = {'command': cmd, 'returncode': proc.returncode,
              'stdout_tail': proc.stdout[-300:], 'stderr_tail': proc.stderr[-300:]}
    if proc.returncode != 0:
        return 'FAIL', detail
    return 'OK', detail


def import_one(name, record, reference_root, index, importer, session_tool,
               presentation, output, temporary, runtime_auditor, runtime_steps):
    """事务导入单个模型：先写到暂存目录，验证后发布到目标。

    `index` 由调用方预先建立并复用，不在每项内重建。
    失败时 row 仍保留 commands、nodes、errors，便于账本溯源。
    """
    row = {
        'model': name,
        'character': record.get('character'),
        'costume': record.get('costume'),
        'variants': record.get('variants', []),
        'config': record.get('config'),
        'status': {'structure': 'PENDING', 'cpu': 'PENDING',
                   'visual': 'PENDING', 'interaction': 'PENDING', 'soak': 'PENDING'},
        'errors': [],
        'commands': [],
        'nodes': [],
    }
    try:
        catalog_index.validate_model_name(name)
        return import_into_row(row, name, reference_root, index, importer, session_tool,
                               presentation, output, temporary, runtime_auditor, runtime_steps)
    except (OSError, ValueError, TypeError, subprocess.SubprocessError) as error:
        if row['status']['structure'] == 'OK':
            row['status']['cpu'] = 'ERROR'
        else:
            row['status']['structure'] = 'ERROR'
        row['errors'].append(f'{type(error).__name__}: {error}')
        return row


def import_into_row(row, name, reference_root, index, importer, session_tool,
                    presentation, output, temporary, runtime_auditor, runtime_steps):
    key = name.removesuffix('.pure')
    target = output / key

    if os.path.lexists(target):
        row['status']['structure'] = 'EXISTS_UNCHECKED'
        row['errors'].append('existing output preserved without certification; use a new --output')
        row['nodes'].append(str(target))
        return row

    candidates = catalog_index.resolve_candidates(index, name)
    alternatives = [catalog_index.relative_to(p, reference_root) for p in candidates]

    if len(candidates) != 1:
        row['status']['structure'] = 'AMBIGUOUS' if candidates else 'MISSING'
        row['errors'].append('missing or ambiguous unpacked model')
        row['alternatives'] = alternatives
        return row

    source = candidates[0]
    resx = source.with_suffix('.resx.json')
    row['source'] = catalog_index.relative_to(source, reference_root)
    row['source_sha256'] = catalog_index.sha256(source)
    row['resx'] = catalog_index.relative_to(resx, reference_root) if resx.is_file() else None
    row['resx_sha256'] = catalog_index.sha256(resx) if resx.is_file() else None
    row['nodes'].append(catalog_index.relative_to(source, reference_root))

    psb = reference_root / 'data' / (key + '.pure.psb.zip')
    if psb.is_file():
        row['psb'] = catalog_index.relative_to(psb, reference_root)
        row['psb_sha256'] = catalog_index.sha256(psb)
    else:
        row['psb'] = None

    external = catalog_index.find_external_textures(source, source.parent)
    row['external_textures'] = external
    if any(v is None for v in external.values()):
        row['errors'].append('missing or ambiguous external texture')

    staging = temporary / key

    cmd = [str(importer), str(source), str(resx),
           '--output', str(staging), '--resource-root', str(source.parent)]
    for tex, path in external.items():
        if path is not None:
            cmd.extend(['--texture', f'{tex}={path}'])
    row['commands'].append(cmd)
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        row['status']['structure'] = 'TIMEOUT'
        row['errors'].append('importer timeout after 120s')
        shutil.rmtree(staging, ignore_errors=True)
        return row
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
        row['commands'].append(cmd2)
        try:
            proc2 = subprocess.run(cmd2, capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired:
            row['status']['structure'] = 'SESSION_TIMEOUT'
            row['errors'].append('session tool timeout after 60s')
            shutil.rmtree(staging, ignore_errors=True)
            return row
        if proc2.returncode != 0:
            row['status']['structure'] = 'SESSION_FAIL'
            row['errors'].append(proc2.stderr.strip()[:300] or proc2.stdout.strip()[-300:])
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
    row['schema'] = bundle_schema_version(target)
    row['bundle_sha256'] = bundle_hash(target)
    row['nodes'].append(str(target))

    if runtime_auditor is not None:
        audit_output = temporary / (key + '.runtime.json')
        status, detail = run_cpu_audit(
            runtime_auditor, source, resx, external, audit_output, runtime_steps)
        row['status']['cpu'] = status
        row['cpu_detail'] = detail
        if audit_output.is_file():
            row['cpu_result'] = json.loads(audit_output.read_text())

    return row


def save_checkpoint(report, checkpoint_path):
    """原子写检查点：先写 .tmp 再 rename，中断时不破坏上一次完整账本。"""
    checkpoint_path.parent.mkdir(parents=True, exist_ok=True)
    tmp = checkpoint_path.with_suffix(checkpoint_path.suffix + '.tmp')
    tmp.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    tmp.replace(checkpoint_path)


def build_summary(rows):
    """统计结构/CPU/视觉/交互/soak 各状态计数。未运行保持 PENDING 不计入 OK。"""
    summary = {'total': len(rows)}
    for field in ('structure', 'cpu', 'visual', 'interaction', 'soak'):
        counter = {}
        for r in rows:
            st = r['status'][field]
            counter[st] = counter.get(st, 0) + 1
        summary[field] = counter
    return summary


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
    try:
        catalog, catalog_hash = catalog_index.load_catalog(root)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    presentation = {}
    if args.presentation is not None:
        presentation = json.loads(args.presentation.read_text())

    importer = args.importer.resolve()
    if not importer.is_file():
        parser.error(f'importer not found: {importer}')

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    index = catalog_index.build_index(root)

    rows = []
    fail_count = 0
    checkpoint = args.report.with_name(args.report.name + '.checkpoint') if args.report else None
    provenance = {
        'head_commit': head_commit(),
        'generated_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'tool_hashes': {
            'importer': tool_hash(importer),
            'runtime_auditor': tool_hash(args.runtime_auditor),
            'session_tool': tool_hash(args.session_tool),
        },
        'runtime_steps': args.runtime_steps,
    }
    with tempfile.TemporaryDirectory(prefix='.nekomote-import-staging-', dir=output) as temporary:
        tmp = pathlib.Path(temporary)
        total = len(catalog)
        for i, (name, record) in enumerate(sorted(catalog.items()), 1):
            row = import_one(name, record, root, index, importer, args.session_tool,
                             presentation, output, tmp,
                             args.runtime_auditor, args.runtime_steps)
            rows.append(row)
            st = row['status']['structure']
            if st != 'OK' or row['status']['cpu'] not in ('OK', 'PENDING'):
                fail_count += 1
            print(f'  [{i}/{total}] {st:12s} {name}', flush=True)
            if checkpoint is not None:
                save_checkpoint(
                    {'complete': False, 'catalog_sha256': catalog_hash,
                     **provenance, 'summary': build_summary(rows), 'models': rows},
                    checkpoint)

    summary = build_summary(rows)
    report = {
        'complete': True,
        'catalog_records': total,
        'catalog_sha256': catalog_hash,
        **provenance,
        'summary': summary,
        'models': rows,
    }

    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        save_checkpoint(report, args.report)
        if checkpoint is not None and checkpoint.exists():
            checkpoint.unlink()
    print(json.dumps(summary, ensure_ascii=False, indent=2))

    if fail_count:
        print(f'\n{fail_count} 项失败或缺失，退出非零', file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
