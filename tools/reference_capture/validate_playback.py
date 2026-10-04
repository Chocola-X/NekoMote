#!/usr/bin/env python3
"""Verify full native author sessions against frozen, unmodified WebGL captures.

Native input is independently frozen events/assets, never source output values.
GetVariable caches for additive animation exclude differences, so raw comparison
covers only explicitly observed base or controller outputs; final geometry tests composition.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import zipfile
from compare_playback_frame import compare
from validate_static import run


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('reference-root', 'importer', 'preview', 'evaluator', 'reuse-reference', 'output'):
        p.add_argument('--' + name, type=pathlib.Path, required=True)
    p.add_argument("--inputs", type=pathlib.Path,
                   help="explicit frozen input matrix; defaults to playback-inputs.json")
    args = p.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[2]
    baseline = json.loads((args.inputs or repo / 'docs/validation/playback-inputs.json').read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    imported, results = set(), []
    for case in baseline['cases']:
        name, model = case['case'], case['model']
        asset = args.output / model / 'model.nkm.json'
        if model not in imported:
            source = args.reference_root / 'model_conversion' / model.split('-')[0] / model / model
            run([args.importer, str(source) + '.json', str(source) + '.resx.json',
                 '--output', asset.parent], args.output / (model + '-import.log'))
            imported.add(model)
            with zipfile.ZipFile(args.reference_root / 'data' / (model + '.pure.psb.zip')) as archive:
                members = [n for n in archive.namelist() if n.endswith('.psb')]
                if len(members) != 1:
                    raise ValueError('expected exactly one PSB')
                if hashlib.sha256(archive.read(members[0])).hexdigest() != case['source_hashes']['psb']:
                    raise ValueError('original PSB changed')
        if sha(asset) != case['asset_sha256']:
            raise ValueError('asset changed: ' + model)
        reference = args.reuse_reference / name
        for suffix, field in [('.json', 'metadata'), ('.png', 'png'), ('.trace.json', 'trace')]:
            if sha(pathlib.Path(str(reference) + suffix)) != case['reference_' + field + '_sha256']:
                raise ValueError('reference changed: ' + name + suffix)
        metadata = json.loads(reference.with_suffix('.json').read_text())
        expected = dict(width=800, height=1080, x=case['origin'][0], y=case['origin'][1],
                        scale=case.get("scale", 1), opacity=1, stepFrames=1, steps=case['steps'],
                        variables=case['variables'], plays=case['plays'], transitions=case.get('transitions', {}), events=[],
                        physics=dict(hair=1, parts=1, bust=1, wind=False))
        if any(metadata.get(k) != v for k, v in expected.items()) or metadata['diagnostic_fixture_edits']:
            raise ValueError('reference is not the frozen, unmodified request: ' + name)
        if metadata['source_hashes'] != case['source_hashes']:
            raise ValueError('reference source hashes changed')
        for driver in ('FreeMoteDriver.js', 'emoteplayer.js'):
            if sha(args.reference_root / 'driver' / driver) != case['source_hashes'][driver]:
                raise ValueError('reference driver changed')
        request = dict(steps=case['steps'], origin=case['origin'], scale=case.get('scale', 1),
                       variables=case['variables'], transitions=case.get('transitions', {}),
                       plays=case['plays'], dump_frame=True)
        input_path = args.output / (name + '-inputs.json')
        input_path.write_text(json.dumps(request, ensure_ascii=False, indent=2) + '\n')
        native_path = args.output / (name + '-native.json')
        run([args.evaluator, asset, input_path, native_path], args.output / (name + '-evaluate.log'))
        native = json.loads(native_path.read_text())
        command = [args.preview, '--playback', '--asset', asset, '--origin',
                   case['origin'][0] + 400, case['origin'][1] + 540, '--steps', case['steps'],
                   '--scale', case.get('scale', 1),
                   '--hidden', '--screenshot', args.output / (name + '-native.png')]
        for key, value in case['variables'].items():
            command += ['--variable', key, value]
        for key, transition in case.get('transitions', {}).items():
            command += ['--transition', key, transition['value'], transition['frames'], transition['easing']]
        for play in case['plays']:
            command += ['--play', play['label'], play['flags']]
        run(command, args.output / (name + '-render.log'))
        source_samples = metadata['variableSamples']
        if len(source_samples) != len(native['samples']) or native['steps'] != case['steps']:
            raise ValueError('sample count differs')
        errors = []
        for a, b in zip(source_samples[1:], native['samples'][1:]):
            if a['step'] != b['step']:
                raise ValueError('author step differs')
            for key in case['observed_variables']:
                errors.append(dict(error=abs(a['values'][key] - b['values'][key]),
                                   step=a['step'], variable=key,
                                   reference=a['values'][key], native=b['values'][key]))
        maximum = max(errors, key=lambda e: e['error'])
        scalar_passed = maximum['error'] <= baseline['scalar_tolerance']
        geometry = compare(native, json.loads(pathlib.Path(str(reference) + '.trace.json').read_text()))
        (args.output / (name + '-geometry.json')).write_text(json.dumps(geometry, indent=2) + '\n')
        comparison = args.output / (name + '-comparison')
        with (args.output / (name + '-compare.log')).open('w') as log:
            status = subprocess.run(['python3', str(repo / 'tools/reference_capture/compare_images.py'),
                str(reference) + '.png', str(args.output / (name + '-native.png')),
                '--output', str(comparison), '--tolerance', str(baseline['image_tolerance'])],
                stdout=log, stderr=subprocess.STDOUT).returncode
        image = json.loads(comparison.with_suffix('.json').read_text())
        passed = scalar_passed and geometry['passed'] and status == 0 and image['passed']
        results.append(dict(case=name, model=model, source_hashes=case['source_hashes'],
                            asset_sha256=case['asset_sha256'], scalar_samples=len(errors),
                            scalar_maximum=maximum, scalar_passed=scalar_passed,
                            geometry=geometry, image=image, passed=passed))
        (args.output / 'report.json').write_text(json.dumps(dict(
            stage_complete=False, production_playback_certified=False,
            passed=all(c['passed'] for c in results), cases=results), indent=2) + '\n')
        print(name, 'PASS' if passed else 'FAIL', 'scalar=', maximum['error'],
              'geometry=', geometry['frame_max_error_px'], 'image pixels=',
              image['pixels_over_tolerance'], flush=True)
    if not all(c['passed'] for c in results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
