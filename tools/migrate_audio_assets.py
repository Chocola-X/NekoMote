#!/usr/bin/env python3
"""One-time WAV to lossless FLAC migration; PNGs and model assets remain unchanged.

Read an existing assets tree, stage a complete new tree, and publish without
replacing any output. Voice namespaces in session reaction IDs select character
``audio/main`` or ``audio/little``; outfit names never select the voice group.
Source inputs, source reports, unrelated files and image bytes are preserved.
"""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import tempfile

from asset_resources import (audio_group_name, convert_audio, group_audio,
                             publish_directory, safe_relative, sha256,
                             tree_ledger, validate_links)


def conversion_plan(source):
    plans = []
    for session_path in sorted((source / 'characters').glob('*/*/session.nks.json')):
        bundle = session_path.parent
        if bundle.is_symlink() or session_path.is_symlink():
            raise ValueError(f'migration requires regular model directories and sessions: {bundle}')
        document = json.loads(session_path.read_text())
        reactions = document['session']['reactions']
        voices = set()
        referenced = set()
        for reaction in reactions:
            value = reaction.get('audio_path', '')
            if not value:
                continue
            relative = safe_relative(value)
            if len(relative.parts) < 2 or relative.parts[0] != 'audio' or relative.suffix != '.wav':
                raise ValueError(f'{session_path}: expected an audio/*.wav reference; run once on original WAV assets')
            if not (bundle / relative).is_file():
                raise ValueError(f'{session_path}: missing audio: {value}')
            label = safe_relative(reaction['label'])
            if len(label.parts) < 2:
                raise ValueError(f'{session_path}: reaction ID must retain source voice namespace')
            voices.add(label.parts[0])
            referenced.add(relative)
        if not referenced:
            continue
        group = audio_group_name(bundle.parent.name, voices)
        audio = bundle / 'audio'
        if audio.is_symlink():
            raise ValueError(f'migration requires original audio directories: {audio}')
        files = []
        for path in sorted(audio.rglob('*')):
            if path.is_symlink():
                raise ValueError(f'migration requires original audio files: {path}')
            if path.is_file():
                if path.suffix != '.wav':
                    raise ValueError(f'non-WAV file in original audio directory: {path}')
                files.append(path)
        if not files:
            raise ValueError(f'no source audio: {audio}')
        plans.append((bundle, document, group, files))
    if not plans:
        raise ValueError('no original WAV sessions found under characters/ROLE/MODEL')
    return plans


def convert_tree(source, output, converter):
    source = pathlib.Path(source).resolve(strict=True)
    output = pathlib.Path(output).absolute()
    converter = pathlib.Path(converter).resolve(strict=True)
    if not source.is_dir():
        raise ValueError('--source must be an asset directory containing characters/')
    if os.path.lexists(output):
        raise ValueError('output already exists; select a new --output')
    if output.resolve().is_relative_to(source):
        raise ValueError('output must be outside the source tree')
    validate_links(source)
    original_ledger = tree_ledger(source)
    plans = conversion_plan(source)
    output.parent.mkdir(parents=True, exist_ok=True)
    source_bytes = sum(p.stat().st_size for p in source.rglob('*')
                       if p.is_file() and not p.is_symlink())
    counts = {'models': len(plans), 'audio_files': 0, 'unique_audio_conversions': 0}
    with tempfile.TemporaryDirectory(prefix='.nekomote-audio-', dir=output.parent) as temporary:
        staging = pathlib.Path(temporary) / 'assets'
        shutil.copytree(source, staging, symlinks=True)
        cache = pathlib.Path(temporary) / 'audio-cache'
        cache.mkdir()
        audio_cache = {}
        for bundle, document, group, files in plans:
            target = staging / bundle.relative_to(source)
            for original in files:
                digest = sha256(original)
                if digest not in audio_cache:
                    cached = cache / (digest + '.flac')
                    convert_audio(converter, original, cached)
                    audio_cache[digest] = cached
                    counts['unique_audio_conversions'] += 1
                old = staging / original.relative_to(source)
                destination = old.with_suffix('.flac')
                if os.path.lexists(destination):
                    raise ValueError(f'converted audio collides: {destination}')
                # Temporary cache links avoid retaining one copy per outfit.
                os.link(audio_cache[digest], destination)
                old.unlink()
                counts['audio_files'] += 1
            for reaction in document['session']['reactions']:
                if reaction.get('audio_path'):
                    reaction['audio_path'] = str(pathlib.PurePosixPath(reaction['audio_path']).with_suffix('.flac'))
            (target / 'session.nks.json').write_text(
                json.dumps(document, ensure_ascii=False, separators=(',', ':'), allow_nan=False) + '\n')
            group_audio(target, target.parent, name=group)
        if tree_ledger(source) != original_ledger:
            raise ValueError('source assets changed during migration; output was not published')
        ledger = tree_ledger(staging)
        result = {'schema': 'nekomote.audio-migration', 'version': 1,
                  'source': str(source), 'output': str(output),
                  'converter_sha256': sha256(converter), **counts,
                  'source_bytes': source_bytes,
                  'output_bytes': sum(p.stat().st_size for p in staging.rglob('*')
                                      if p.is_file() and not p.is_symlink()),
                  'links': sum(p.is_symlink() for p in staging.rglob('*')),
                  'sha256': ledger}
        report = staging / 'audio-migration-report.json'
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
        parser.exit(1, f'audio migration failed: {error}\n')
    print(json.dumps({key: value for key, value in result.items() if key != 'sha256'},
                     ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
