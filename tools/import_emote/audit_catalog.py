#!/usr/bin/env python3
"""Audit every catalogue record without copying models or modifying the source."""
import argparse
import collections
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=pathlib.Path, required=True)
    parser.add_argument('--importer', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--runtime-auditor', type=pathlib.Path,
                        help='Also sample initial pose and every timeline with native CPU physics')
    parser.add_argument('--runtime-steps', type=int, default=120)
    args = parser.parse_args()
    if not 1 <= args.runtime_steps <= 36000:
        parser.error('--runtime-steps requires 1..36000')
    root = args.reference_root.resolve()
    catalog_path = root / 'config/model-catalog.json'
    catalog = json.loads(catalog_path.read_text())
    index = collections.defaultdict(list)
    for p in sorted((root / 'model_conversion').rglob('*.json')):
        if not p.name.endswith('.resx.json'):
            index[p.stem.removesuffix(".pure")].append(p)
    rows = []
    catalog_hash = hashlib.sha256(catalog_path.read_bytes()).hexdigest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    tool_hashes = {}

    def save_report(complete):
        summary = dict(catalog_records=len(catalog), audited_records=len(rows),
                       structural_imports=sum(r['ok'] for r in rows),
                       failed=sum(not r['ok'] for r in rows), playback_certified=0)
        if args.runtime_auditor:
            timelines = [c for r in rows for c in r.get('runtime', {}).get('timelines', [])]
            summary.update(runtime_compiled=sum(r.get('runtime', {}).get('compiled', False)
                                                for r in rows),
                           runtime_cases_passed=sum(r.get('runtime', {}).get('ok', False)
                                                    for r in rows),
                           runtime_steps_per_case=args.runtime_steps,
                           timeline_cases=len(timelines),
                           timeline_cases_passed=sum(c['ok'] for c in timelines))
        report = dict(complete=complete, catalog_sha256=catalog_hash,
                      tool_sha256=tool_hashes, summary=summary, models=rows)
        checkpoint = args.output.with_name(args.output.name + '.checkpoint')
        checkpoint.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
        checkpoint.replace(args.output)
        return summary

    with tempfile.TemporaryDirectory(prefix='nekomote-audit-') as temporary:
        # Freeze executables once. Rebuilding the workspace during a long audit
        # must neither mix runtime versions nor race a linker replacing a binary.
        importer = pathlib.Path(temporary) / 'importer'
        shutil.copy2(args.importer.resolve(), importer)
        tool_hashes['importer'] = hashlib.sha256(importer.read_bytes()).hexdigest()
        auditor = pathlib.Path(temporary) / 'runtime-auditor'
        if args.runtime_auditor:
            shutil.copy2(args.runtime_auditor.resolve(), auditor)
            tool_hashes['runtime_auditor'] = hashlib.sha256(auditor.read_bytes()).hexdigest()
        save_report(False)
        for name, record in catalog.items():
            key = name.removesuffix('.pure')
            candidates = index.get(key, [])
            canonical = [p for p in candidates if p.parent.name == key]
            alternatives = [str(p.relative_to(root)) for p in candidates]
            if len(canonical) == 1:
                candidates = canonical
            row = dict(model=name, canonical=key, catalog=record, alternatives=alternatives)
            psb = root / 'data' / (key + '.pure.psb.zip')
            row['psb'] = str(psb.relative_to(root)) if psb.is_file() else None
            if len(candidates) != 1:
                row.update(ok=False, errors=['missing or ambiguous unpacked model'], candidates=[str(p.relative_to(root)) for p in candidates])
            else:
                source = candidates[0]
                resx = source.with_suffix('.resx.json')
                report = pathlib.Path(temporary) / 'report.json'
                external = {}
                document = json.loads(source.read_text())
                for texture, value in document.get('source', {}).items():
                    if value.get('texture', {}).get('pixel') is None:
                        suffix = 'tex' + (texture.split('#')[-1] if '#' in texture else '000') + '.png'
                        matches = sorted(source.parent.glob('*' + suffix))
                        if len(matches) == 1:
                            external[texture] = str(matches[0].relative_to(source.parent))
                row['external_texture_mapping'] = external
                command = [str(importer), str(source), str(resx), '--audit-report', str(report)]
                for texture, path in external.items():
                    command.extend(['--texture', texture + '=' + path])
                run = subprocess.run(command, capture_output=True, text=True)
                if report.is_file():
                    row.update(json.loads(report.read_text()))
                    report.unlink()
                else:
                    row.update(ok=False, errors=[run.stdout + run.stderr])
                row['json'] = str(source.relative_to(root))
                row['json_sha256'] = hashlib.sha256(source.read_bytes()).hexdigest()
                row['resx'] = str(resx.relative_to(root)) if resx.is_file() else None
                cfg = root / 'config' / record.get('config', '')
                row['config_exists'] = cfg.is_file()
                if args.runtime_auditor and row['ok']:
                    runtime_report = pathlib.Path(temporary) / 'runtime.json'
                    command = [str(auditor), str(source), str(resx),
                               '--output', str(runtime_report),
                               '--steps', str(args.runtime_steps)]
                    for texture, path in external.items():
                        command.extend(['--texture', texture + '=' + path])
                    run = subprocess.run(command, capture_output=True, text=True)
                    if runtime_report.is_file():
                        row['runtime'] = json.loads(runtime_report.read_text())
                        runtime_report.unlink()
                    else:
                        row['runtime'] = dict(ok=False, compiled=False,
                                              process_error=run.stdout + run.stderr)
                    print(json.dumps(dict(model=name, runtime_ok=row['runtime']['ok']),
                                     ensure_ascii=False), flush=True)
            rows.append(row)
            save_report(False)
    print(json.dumps(save_report(True)))


if __name__ == '__main__':
    main()
