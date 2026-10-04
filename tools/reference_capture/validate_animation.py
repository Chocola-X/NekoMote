#!/usr/bin/env python3
"""Capture fixed native author steps and compare actual WebGL/native frames."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import zipfile
from validate_static import run


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference-root', type=pathlib.Path, required=True)
    p.add_argument('--importer', type=pathlib.Path, required=True)
    p.add_argument('--preview', type=pathlib.Path, required=True)
    p.add_argument('--evaluator', type=pathlib.Path, required=True)
    p.add_argument('--browser', type=pathlib.Path, required=True)
    p.add_argument('--output', type=pathlib.Path, required=True)
    p.add_argument('--reuse-reference', type=pathlib.Path)
    args = p.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[2]
    baseline = json.loads((repo / 'docs/validation/animation-inputs.json').read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    imported, results = set(), []
    driver_hashes = {n: hashlib.sha256((args.reference_root / 'driver' / n).read_bytes()).hexdigest()
                     for n in ('FreeMoteDriver.js', 'emoteplayer.js')}
    source_hashes = {}
    for case in baseline['cases']:
        model, name = case['model'], case['case']
        stem = model + '-' + name
        asset = args.output / model / 'model.nkm.json'
        if model not in imported:
            source = args.reference_root / 'model_conversion' / model.split('-')[0] / model / model
            run([args.importer, str(source)+'.json', str(source)+'.resx.json', '--output', asset.parent],
                args.output / (model+'-import.log'))
            imported.add(model)
            with zipfile.ZipFile(args.reference_root / 'data' / (model+'.pure.psb.zip')) as archive:
                members = [n for n in archive.namelist() if n.endswith('.psb')]
                if len(members) != 1:
                    raise ValueError('expected one PSB: '+model)
                source_hashes[model] = hashlib.sha256(archive.read(members[0])).hexdigest()
        if hashlib.sha256(asset.read_bytes()).hexdigest() != case['asset_sha256']:
            raise ValueError('asset changed: '+model)
        reference = (args.reuse_reference or args.output / 'reference') / stem
        if not args.reuse_reference:
            command = ['python3', repo / 'tools/reference_capture/capture.py', '--reference-root',
                       args.reference_root, '--browser', args.browser, '--model', model, '--output',
                       reference, '--variables', json.dumps(case['initial']), '--steps', case['steps'],
                       '--sample-variables', '--trace', '--disable-physics']
            if case['disable_blink']:
                command += ['--disable-auto-blink']
            for request in case['plays']:
                command += ['--play', request['label'], request['flags']]
            run(command, args.output / (stem+'-capture.log'))
        metadata = json.loads(reference.with_suffix('.json').read_text())
        expected = dict(width=800, height=1080, x=0, y=400, scale=1, opacity=1,
                        stepFrames=1, steps=case['steps'], variables=case['initial'], plays=case['plays'])
        if any(metadata.get(k) != v for k, v in expected.items()) or metadata.get('transitions'):
            raise ValueError('reference request differs: '+stem)
        if metadata['source_hashes'] != case['source_hashes'] or any(
                metadata['source_hashes'][key] != value for key, value in driver_hashes.items()):
            raise ValueError('source/driver changed: '+stem)
        if source_hashes[model] != case['source_hashes']['original_psb']:
            raise ValueError('original PSB changed: '+model)
        if metadata['diagnostic_fixture_edits'] != case['diagnostic_fixture_edits']:
            raise ValueError('reference fixture differs: '+stem)
        if hashlib.sha256(reference.with_suffix('.png').read_bytes()).hexdigest() != case['reference_png_sha256']:
            raise ValueError('reference image changed: '+stem)
        if hashlib.sha256(pathlib.Path(str(reference)+'.trace.json').read_bytes()).hexdigest() != case['reference_trace_sha256']:
            raise ValueError('reference trace changed: '+stem)
        native = args.output / stem
        command = [args.preview, '--animation-diagnostic', '--asset', asset, '--origin', 400, 940,
                   '--steps', case['steps'], '--hidden', '--screenshot', str(native)+'.png']
        if case['disable_blink']:
            # The frozen PSB aliases blinkEnabled and Eye enabled. Match the
            # actual controller fixture, not just its historically named flag.
            command += ['--disable-eye-controller']
        for request in case['plays']:
            command += ['--play', request['label'], request['flags']]
        for key, value in case['initial'].items():
            command += ['--variable', key, value]
        run(command, native.with_suffix('.log'))
        geometry_path = args.output / (stem+'-geometry.json')
        command = [args.evaluator, '--animation-diagnostic', '--asset', asset, '--steps', case['steps'],
             '--variables', json.dumps(case['initial']), '--plays', json.dumps(case['plays']),
             '--sample-trace', str(reference)+'.trace.json', '--output', geometry_path]
        if case['disable_blink']:
            command += ['--disable-eye-controller']
        run(command, args.output / (stem+'-geometry.log'))
        geometry = json.loads(geometry_path.read_text())
        geometry_passed = (geometry['frame_error'] == 0 and not geometry['missing_primitives'] and
            geometry['frame_max_error_px'] <= baseline['coordinate_tolerance_px'] and all(
                geometry[k] == 0 for k in ['unmatched_reference_vertices', 'unused_native_vertices',
                                         'missing_reference_triangles', 'unused_native_triangles']))
        comparison = args.output / (stem+'-comparison')
        with (args.output / (stem+'-comparison.log')).open('w') as log:
            status = subprocess.run(['python3', str(repo / 'tools/reference_capture/compare_images.py'),
                str(reference)+'.png', str(native)+'.png', '--output', str(comparison), '--tolerance', '2'],
                stdout=log, stderr=subprocess.STDOUT).returncode
        image = json.loads(comparison.with_suffix('.json').read_text())
        passed = geometry_passed and status == 0 and image['passed']
        results.append(dict(model=model, case=name, steps=case['steps'], initial=case['initial'],
                            plays=case['plays'], passed=passed, geometry_passed=geometry_passed,
                            frame_max_error_px=geometry['frame_max_error_px'], image=image,
                            source_hashes=metadata['source_hashes'],
                            diagnostic_fixture_edits=metadata['diagnostic_fixture_edits']))
        (args.output / 'report.json').write_text(json.dumps(dict(stage_complete=False,
            production_playback_certified=False, passed=all(c['passed'] for c in results), cases=results),
            indent=2)+'\n')
        print(stem, 'PASS' if passed else 'FAIL', image['pixels_over_tolerance'], flush=True)
    if not all(case['passed'] for case in results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
