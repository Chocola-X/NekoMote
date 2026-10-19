#!/usr/bin/env python3
"""Convert schema-6 model JSON to the current binary container in a new assets tree.

The source remains read only. PNGs, relative audio links, provenance and unknown
files retain their bytes. Each native conversion must decode to the complete
original JSON document before its staged JSON is removed. Publish the complete
new tree atomically without replacing an existing output.
"""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import tempfile

from asset_resources import publish_directory, sha256, tree_ledger, validate_links
from model_container import read_model


def same_document(left, right):
    # Python considers True == 1 and 1 == 1.0. Author scalar kinds must also
    # survive conversion; a flag accidentally encoded as an integer is a change.
    if type(left) is not type(right):
        return False
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(same_document(left[key], right[key]) for key in left)
    if isinstance(left, list):
        return len(left) == len(right) and all(same_document(a, b) for a, b in zip(left, right))
    return left == right


def convert_tree(source, output, converter):
    source = pathlib.Path(source).resolve(strict=True)
    output = pathlib.Path(output).absolute()
    converter = pathlib.Path(converter).resolve(strict=True)
    if not source.is_dir():
        raise ValueError('--source must be an asset directory')
    if os.path.lexists(output):
        raise ValueError('output already exists; select a new --output')
    if output.resolve().is_relative_to(source):
        raise ValueError('output must be outside the source tree')
    validate_links(source)
    original_ledger = tree_ledger(source)
    models = sorted(source.rglob('model.nkm.json'))
    if not models:
        raise ValueError('no schema-6 model.nkm.json files found')
    if any(path.is_symlink() or not path.is_file() for path in models):
        raise ValueError('source model JSON must be a regular file')
    for path in models:
        if os.path.lexists(path.with_name('model.nkm')):
            raise ValueError(f'current model already exists: {path.with_name("model.nkm")}')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.nekomote-model-', dir=output.parent) as temporary:
        staging = pathlib.Path(temporary) / 'assets'
        shutil.copytree(source, staging, symlinks=True)
        converted = []
        for original in models:
            relative = original.relative_to(source)
            legacy = staging / relative
            document = json.loads(legacy.read_text())
            if not isinstance(document, dict) or type(document.get('schema')) is not int or document['schema'] != 6:
                raise ValueError(f'{original}: expected schema 6; reimport older assets from FreeMote sources')
            destination = legacy.with_name('model.nkm')
            if os.path.lexists(destination):
                raise ValueError(f'current model already exists: {destination}')
            process = subprocess.run([str(converter), str(legacy), str(destination)],
                                     capture_output=True, text=True, check=True)
            metadata = json.loads(process.stdout)
            if not isinstance(metadata, dict) or metadata.get('ok') is not True:
                raise ValueError(f'{original}: native converter did not verify model')
            if destination.is_symlink() or not destination.is_file():
                raise ValueError(f'{original}: converter produced no regular model container')
            if not same_document(read_model(destination), document):
                raise ValueError(f'{original}: decoded model differs from complete original JSON')
            converted.append({'source': relative.as_posix(), 'source_sha256': sha256(legacy),
                              'output': destination.relative_to(staging).as_posix(),
                              'output_sha256': sha256(destination)})
            legacy.unlink()
        if tree_ledger(source) != original_ledger:
            raise ValueError('source assets changed during migration; output was not published')
        result = {'schema': 'nekomote.model-migration', 'version': 1,
                  'source': str(source), 'output': str(output),
                  'converter_sha256': sha256(converter), 'models': len(converted),
                  'converted': converted, 'source_sha256': original_ledger,
                  'sha256': tree_ledger(staging)}
        report = staging / 'model-migration-report.json'
        while os.path.lexists(report):
            report = report.with_name(report.stem + '.new.json')
        result['report_file'] = report.name
        report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
        publish_directory(staging, output)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    parser.add_argument('--converter', required=True, type=pathlib.Path)
    args = parser.parse_args()
    try:
        result = convert_tree(args.source, args.output, args.converter)
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(1, f'model migration failed: {error}\n')
    print(json.dumps({key: value for key, value in result.items()
                      if key not in ('source_sha256', 'sha256', 'converted')},
                     ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
