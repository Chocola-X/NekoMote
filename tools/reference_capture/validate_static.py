#!/usr/bin/env python3
"""Validate complete native static frames against the original WebGL player."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import zipfile


def run(command, log):
    with log.open('w') as output:
        result = subprocess.run([str(v) for v in command], stdout=output,
                                stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f'command failed ({result.returncode}); see {log}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=pathlib.Path, required=True)
    parser.add_argument('--importer', type=pathlib.Path, required=True)
    parser.add_argument('--preview', type=pathlib.Path, required=True)
    parser.add_argument('--evaluator', type=pathlib.Path, required=True)
    parser.add_argument('--browser', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--reuse-reference', type=pathlib.Path,
                        help='explicit previously captured reference directory; hashes are checked')
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    baseline = json.loads((root / 'docs/validation/static-inputs.json').read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    imported, results = set(), []
    current_sources = {name: hashlib.sha256((args.reference_root / 'driver' / name).read_bytes()).hexdigest()
                       for name in ('FreeMoteDriver.js', 'emoteplayer.js')}
    for case in baseline['cases']:
        model, name = case['model'], case['case']
        asset = args.output / model / 'model.nkm.json'
        if model not in imported:
            source = args.reference_root / 'model_conversion' / model.split('-')[0] / model / model
            run([args.importer, str(source)+'.json', str(source)+'.resx.json',
                 '--output', asset.parent], args.output / (model+'-import.log'))
            imported.add(model)
            with zipfile.ZipFile(args.reference_root / 'data' / (model+'.pure.psb.zip')) as archive:
                members = [name for name in archive.namelist() if name.endswith('.psb')]
                if len(members) != 1:
                    raise ValueError('expected one PSB: '+model)
                current_sources[model] = hashlib.sha256(archive.read(members[0])).hexdigest()
        stem_name = model+'-'+name
        reference = (args.reuse_reference or (args.output / 'reference')) / stem_name
        if not args.reuse_reference:
            run(['python3', root / 'tools/reference_capture/capture.py',
                 '--reference-root', args.reference_root, '--browser', args.browser,
                 '--model', model, '--variables', json.dumps(case['variables']),
                 '--trace', '--output', reference], args.output / (stem_name+'-capture.log'))
        metadata = json.loads(reference.with_suffix('.json').read_text())
        if (metadata['variables'] != case['variables'] or
                metadata['physics'] != dict(hair=0, parts=0, bust=0, wind=False) or
                metadata['diagnostic_fixture_edits'] or any(
                metadata[key] != value for key, value in
                dict(width=800, height=1080, x=0, y=400, scale=1, opacity=1, steps=0).items())):
            raise ValueError('reference inputs differ: '+stem_name)
        for key, expected in case['reference_sources'].items():
            if (metadata['source_hashes'].get(key) != expected or
                    current_sources[model if key in ('psb', 'original_psb') else key] != expected):
                raise ValueError('reference source changed: '+key)
        if hashlib.sha256(asset.read_bytes()).hexdigest() != case['asset_sha256']:
            raise ValueError('imported asset changed; review and rebuild the baseline: '+model)
        if hashlib.sha256(reference.with_suffix('.png').read_bytes()).hexdigest() != case['reference_png_sha256']:
            raise ValueError('reference image changed: '+stem_name)
        if hashlib.sha256(pathlib.Path(str(reference)+'.trace.json').read_bytes()).hexdigest() != case['reference_trace_sha256']:
            raise ValueError('reference trace changed: '+stem_name)
        native = args.output / stem_name
        command = [args.preview, '--geometry-diagnostic', '--asset', asset,
                   '--origin', 400, 940, '--width', 800, '--height', 1080,
                   '--hidden', '--screenshot', str(native)+'.png']
        for key, value in case['variables'].items():
            command += ['--variable', key, value]
        run(command, native.with_suffix('.log'))
        geometry_file = args.output / (stem_name+'-geometry.json')
        run([args.evaluator, '--asset', asset, '--variables', json.dumps(case['variables']),
             '--sample-trace', str(reference)+'.trace.json', '--output', geometry_file],
            args.output / (stem_name+'-geometry.log'))
        geometry = json.loads(geometry_file.read_text())
        geometry_passed = (geometry['frame_error'] == 0 and not geometry['missing_primitives'] and
                           geometry['primitives'] == case['primitives'] and
                           geometry['frame_max_error_px'] <= baseline['coordinate_tolerance_px'] and
                           all(geometry[key] == 0 for key in
                               ['unmatched_reference_vertices', 'unused_native_vertices',
                                'missing_reference_triangles', 'unused_native_triangles']))
        comparison = args.output / (stem_name+'-comparison')
        with (args.output / (stem_name+'-comparison.log')).open('w') as output:
            status = subprocess.run(['python3', str(root / 'tools/reference_capture/compare_images.py'),
                                     str(reference)+'.png', str(native)+'.png',
                                     '--output', str(comparison), '--tolerance', '2'],
                                    stdout=output, stderr=subprocess.STDOUT).returncode
        image = json.loads(comparison.with_suffix('.json').read_text())
        passed = geometry_passed and status == 0 and image['passed']
        results.append(dict(model=model, case=name, variables=case['variables'], passed=passed,
                            reference_sources=metadata['source_hashes'], image=image,
                            geometry_passed=geometry_passed,
                            frame_max_error_px=geometry['frame_max_error_px']))
        report = dict(stage_complete=False, production_playback_certified=False,
                      passed=all(c['passed'] for c in results), cases=results)
        (args.output / 'report.json').write_text(json.dumps(report, indent=2)+'\n')
        print(model, name, 'PASS' if passed else 'FAIL',
              image['pixels_over_tolerance'], flush=True)
    if not all(case['passed'] for case in results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
