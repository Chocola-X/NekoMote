#!/usr/bin/env python3
"""Re-encode PNG assets losslessly in a new assets tree.

Model textures under characters/*/textures/ are recompressed with per-row
optimal PNG filtering and zlib level 9. Backgrounds under backgrounds/ are
stripped of metadata chunks first; a recompression attempt replaces the
stripped file only when it is smaller. Icons and every non-PNG file keep
their bytes. The source tree stays read only; the tool verifies decoded
pixels before publishing each file, and the complete new tree is published
atomically without replacing an existing output.
"""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed

from asset_resources import publish_directory, sha256, tree_ledger, validate_links


def run_tool(tool, mode, source, destination):
    result = subprocess.run([str(tool), mode, str(source), str(destination)],
                            capture_output=True, text=True, check=True)
    report = json.loads(result.stdout)
    if report.get('ok') is not True:
        raise ValueError(f'{source}: {mode} failed: {report.get("error")}')
    return report['source_bytes'], report['output_bytes']


def migrate_png(source, destination, tool, mode):
    before, after = run_tool(tool, mode, source, destination)
    if mode == 'recompress':
        # A rare texture may already be optimally filtered; never publish a
        # larger re-encode when the source bytes are smaller.
        if after >= before:
            shutil.copy2(source, destination)
            return before, before
        return before, after
    # Backgrounds: pixel data is usually near-optimal, so first strip the
    # metadata, then keep a full re-encode only when it is smaller still.
    candidate = destination.with_name(destination.name + '.recompressed')
    try:
        _, recompressed = run_tool(tool, 'recompress', destination, candidate)
    except subprocess.SubprocessError:
        candidate.unlink(missing_ok=True)
        return before, after
    if recompressed < after:
        candidate.replace(destination)
        return before, recompressed
    candidate.unlink()
    return before, after


def convert_tree(source, output, tool, jobs):
    source = pathlib.Path(source).resolve(strict=True)
    output = pathlib.Path(output).absolute()
    tool = pathlib.Path(tool).resolve(strict=True)
    if not source.is_dir():
        raise ValueError('--source must be an asset directory')
    if os.path.lexists(output):
        raise ValueError('output already exists; select a new --output')
    if output.resolve().is_relative_to(source):
        raise ValueError('output must be outside the source tree')
    validate_links(source)
    original_ledger = tree_ledger(source)
    pngs = sorted(path for path in source.rglob('*.png')
                  if not path.is_symlink() and path.is_file())
    if not pngs:
        raise ValueError('no PNG files found')

    def mode_for(relative):
        parts = relative.parts
        if parts[0] == 'backgrounds':
            return 'strip'
        # Model textures and UI icons share the re-encode path.
        return 'recompress'

    planned = [(path, mode_for(path.relative_to(source))) for path in pngs]

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.nekomote-png-', dir=output.parent) as temporary:
        staging = pathlib.Path(temporary) / 'assets'
        shutil.copytree(source, staging, symlinks=True)
        # Each worker owns one destination file; level-9 zlib is CPU-bound, so
        # one process per core keeps the full-tree migration within minutes.
        migrated = {}
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = {pool.submit(migrate_png, original, staging / original.relative_to(source),
                                   tool, mode): (original, mode)
                       for original, mode in planned}
            for future in as_completed(futures):
                migrated[futures[future]] = future.result()
        entries = []
        totals = {'recompressed': 0, 'stripped': 0, 'unchanged': 0}
        source_bytes = output_bytes = 0
        for original, mode in planned:
            relative = original.relative_to(source)
            before, after = migrated[(original, mode)]
            entry = {'path': relative.as_posix(), 'mode': mode,
                     'source_bytes': before, 'output_bytes': after}
            if after < before:
                totals['recompressed' if mode == 'recompress' else 'stripped'] += 1
            else:
                totals['unchanged'] += 1
                entry['note'] = 'source encoding already optimal'
            source_bytes += before
            output_bytes += after
            entries.append(entry)
        if tree_ledger(source) != original_ledger:
            raise ValueError('source assets changed during migration; output was not published')
        result = {'schema': 'nekomote.png-migration', 'version': 1,
                  'source': str(source), 'output': str(output),
                  'tool_sha256': sha256(tool), 'pngs': len(entries),
                  'totals': totals, 'source_bytes': source_bytes,
                  'output_bytes': output_bytes, 'converted': entries,
                  'source_sha256': original_ledger, 'sha256': tree_ledger(staging)}
        report = staging / 'png-migration-report.json'
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
    parser.add_argument('--tool', required=True, type=pathlib.Path,
                        help='nekomote_png_recompress executable')
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 1),
                        help='parallel converter processes (default: up to 8)')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be at least 1')
    try:
        result = convert_tree(args.source, args.output, args.tool, args.jobs)
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(1, f'png migration failed: {error}\n')
    print(json.dumps({key: value for key, value in result.items()
                      if key not in ('source_sha256', 'sha256', 'converted')},
                     ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
