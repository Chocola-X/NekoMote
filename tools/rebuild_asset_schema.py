#!/usr/bin/env python3
"""One-time schema 5 -> 6 publication from explicitly reimported bundles.

All existing model data and decoded texture pixels must agree before any local
asset is changed. Frozen evidence bundles are separate inputs and stay intact.
"""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
from PIL import Image


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def migrate(assets, rebuilt, backup):
    targets = sorted(assets.glob('*/model.nkm.json'))
    if not targets:
        raise ValueError('no existing assets')
    if backup.exists():
        raise ValueError('backup destination already exists')
    plan = []
    for target in targets:
        candidate = rebuilt / target.parent.name / target.name
        old_payload, new_payload = target.read_bytes(), candidate.read_bytes()
        old, new = json.loads(old_payload), json.loads(new_payload)
        if old['schema'] != 5 or new['schema'] != 6:
            raise ValueError(f'expected explicit schema 5 -> 6: {target}')
        new_model = dict(new['model'])
        new_model.pop('mirrored_parameters')
        old_model = dict(old['model'])
        # Schema 5's documented reader defaulted an omitted mirror to false.
        # This is an explicit offline comparison, never a runtime fallback.
        old_model.setdefault('mirror', False)
        if old_model != new_model:
            raise ValueError(f'author data changed; independent review required: {target}')
        for texture in new_model['textures']:
            path = pathlib.Path(texture['path'])
            if path.is_absolute() or '..' in path.parts:
                raise ValueError('invalid texture path')
            with Image.open(target.parent / path) as a, Image.open(candidate.parent / path) as b:
                if a.size != b.size or a.convert('RGBA').tobytes() != b.convert('RGBA').tobytes():
                    raise ValueError(f'texture pixels changed: {target.parent / path}')
        plan.append(dict(model=target.parent.name,
                         old_sha256=hashlib.sha256(old_payload).hexdigest(),
                         new_sha256=hashlib.sha256(new_payload).hexdigest()))
    backup.mkdir(parents=True)
    # Retain every old model before publishing the first replacement.
    for row in plan:
        target = assets / row['model'] / 'model.nkm.json'
        shutil.copyfile(target, backup / (row['model'] + '.nkm.json'))
        saved = backup / (row['model'] + '.nkm.json')
        if (sha(target) != row['old_sha256'] or
                sha(saved) != row['old_sha256']):
            raise ValueError(f'asset changed during migration: {target}')
    published = []
    report = backup / 'migration.json'
    try:
        for row in plan:
            target = assets / row['model'] / 'model.nkm.json'
            candidate = rebuilt / row['model'] / 'model.nkm.json'
            payload = candidate.read_bytes()
            if (sha(target) != row['old_sha256'] or
                    hashlib.sha256(payload).hexdigest() != row['new_sha256']):
                raise ValueError(f'asset changed during migration: {target}')
            temporary = target.with_name('.schema6-model.tmp')
            with temporary.open('xb') as f:
                f.write(payload)
            try:
                os.replace(temporary, target)
            finally:
                temporary.unlink(missing_ok=True)
            published.append(row['model'])
            report.write_text(json.dumps(dict(complete=False, plan=plan, published=published),
                                         indent=2) + '\n')
        report.write_text(json.dumps(dict(complete=True, plan=plan, published=published),
                                     indent=2) + '\n')
    except Exception:
        # Roll back only our replacements. Preserve any concurrent external edit.
        for row in plan:
            if row['model'] in published:
                target = assets / row['model'] / 'model.nkm.json'
                if sha(target) == row['new_sha256']:
                    shutil.copyfile(backup / (row['model'] + '.nkm.json'), target)
        raise
    return len(plan)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('assets', 'rebuilt', 'backup'):
        parser.add_argument('--' + name, required=True, type=pathlib.Path)
    args = parser.parse_args()
    try:
        count = migrate(args.assets, args.rebuilt, args.backup)
    except (OSError, KeyError, ValueError) as error:
        parser.exit(1, str(error) + '\n')
    print(f'Rebuilt {count} model files; textures and session assets retained. Backup: {args.backup}')


if __name__ == '__main__':
    main()
