#!/usr/bin/env python3
"""Audit every catalogue record without copying models or modifying the source."""
import argparse
import collections
import hashlib
import json
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=pathlib.Path, required=True)
    parser.add_argument('--importer', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    root = args.reference_root.resolve()
    catalog_path = root / 'config/model-catalog.json'
    catalog = json.loads(catalog_path.read_text())
    index = collections.defaultdict(list)
    for p in sorted((root / 'model_conversion').rglob('*.json')):
        if not p.name.endswith('.resx.json'):
            index[p.stem.removesuffix(".pure")].append(p)
    rows = []
    with tempfile.TemporaryDirectory(prefix='nekomote-audit-') as temporary:
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
                command = [str(args.importer.resolve()), str(source), str(resx), '--audit-report', str(report)]
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
            rows.append(row)
    summary = {'catalog_records': len(rows), 'structural_imports': sum(r['ok'] for r in rows),
               'failed': sum(not r['ok'] for r in rows), 'playback_certified': 0}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'catalog_sha256': hashlib.sha256(catalog_path.read_bytes()).hexdigest(),
                                      'summary': summary, 'models': rows}, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
