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
from validate_static import run, configure_asset_input, resolve_source, catalog_index


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('reference-root', 'preview', 'evaluator', 'reuse-reference', 'output'):
        p.add_argument('--' + name, type=pathlib.Path, required=True)
    p.add_argument("--inputs", type=pathlib.Path,
                   help="explicit frozen input matrix; defaults to playback-inputs.json")
    configure_asset_input(p)
    args = p.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[2]
    baseline = json.loads((args.inputs or repo / 'docs/validation/playback-inputs.json').read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    imported, results = set(), []
    source_index = catalog_index.build_index(args.reference_root) if args.importer else None
    def save_report():
        (args.output / 'report.json').write_text(json.dumps(dict(
            stage_complete=False, production_playback_certified=False,
            passed=all(c['passed'] for c in results), cases=results), indent=2) + '\n')
    for case in baseline['cases']:
        name, model = case['case'], case['model']
        asset = (args.asset_root or args.output) / model / 'model.nkm.json'
        if model not in imported:
            if args.importer:
                source = resolve_source(source_index, model)
                run([args.importer, source, source.with_suffix('.resx.json'),
                     '--output', asset.parent], args.output / (model+'-import.log'))
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
        metadata = json.loads(pathlib.Path(str(reference) + '.json').read_text())
        expected = dict(width=800, height=1080, x=case['origin'][0], y=case['origin'][1],
                        scale=case.get("scale", 1), opacity=1, stepFrames=1, steps=case['steps'],
                        variables=case['variables'], plays=case['plays'], transitions=case.get('transitions', {}), events=case.get('events', []),
                        physics=dict(hair=1, parts=1, bust=1, wind=False))
        if any(metadata.get(k) != v for k, v in expected.items()) or metadata['diagnostic_fixture_edits']:
            raise ValueError('reference is not the frozen, unmodified request: ' + name)
        if metadata.get('fixedClockMs') != case.get('fixed_clock_ms'):
            raise ValueError('reference initialization clock changed: ' + name)
        if metadata.get('differences', {}) != case.get('differences', {}):
            raise ValueError('reference difference requests changed: ' + name)
        if metadata.get('timelineRequests', []) != case.get('timeline_requests', []):
            raise ValueError('reference timeline requests changed: ' + name)
        if 'fixed_clock_ms' in case and case['random_seed'] != case['fixed_clock_ms'] // 1000:
            raise ValueError('native seed differs from source initialization clock: ' + name)
        if metadata['source_hashes'] != case['source_hashes']:
            raise ValueError('reference source hashes changed')
        for driver in ('FreeMoteDriver.js', 'emoteplayer.js'):
            if sha(args.reference_root / 'driver' / driver) != case['source_hashes'][driver]:
                raise ValueError('reference driver changed')
        request = dict(steps=case['steps'], origin=case['origin'], scale=case.get('scale', 1),
                       variables=case['variables'], transitions=case.get('transitions', {}),
                       plays=case['plays'], differences=case.get('differences', {}),
                       timeline_requests=case.get('timeline_requests', []),
                       events=case.get('events', []), dump_frame=True)
        if 'random_seed' in case:
            request['random_seed'] = case['random_seed']
        input_path = args.output / (name + '-inputs.json')
        input_path.write_text(json.dumps(request, ensure_ascii=False, indent=2) + '\n')
        native_path = args.output / (name + '-native.json')
        try:
            run([args.evaluator, asset, input_path, native_path], args.output / (name + '-evaluate.log'))
        except RuntimeError as error:
            results.append(dict(case=name, model=model, passed=False,
                                native_failure=dict(phase='evaluate', error=str(error),
                                    log=(args.output / (name + '-evaluate.log')).read_text()),
                                source_hashes=case['source_hashes'], asset_sha256=case['asset_sha256']))
            save_report()
            print(name, 'FAIL native evaluation; see retained log', flush=True)
            continue
        native = json.loads(native_path.read_text())
        command = [args.preview, '--playback', '--asset', asset, '--origin',
                   case['origin'][0] + 400, case['origin'][1] + 540, '--steps', case['steps'],
                   '--scale', case.get('scale', 1),
                   '--hidden', '--screenshot', args.output / (name + '-native.png')]
        for key, value in case['variables'].items():
            command += ['--variable', key, value]
        if 'random_seed' in case:
            command += ['--random-seed', case['random_seed']]
        if request['timeline_requests'] or request['events'] or request['differences']:
            command += ['--playback-script', input_path]
        for key, transition in case.get('transitions', {}).items():
            command += ['--transition', key, transition['value'], transition['frames'], transition['easing']]
        for play in case['plays']:
            command += ['--play', play['label'], play['flags']]
        try:
            run(command, args.output / (name + '-render.log'))
        except RuntimeError as error:
            results.append(dict(case=name, model=model, passed=False,
                                native_failure=dict(phase='render', error=str(error),
                                    log=(args.output / (name + '-render.log')).read_text()),
                                source_hashes=case['source_hashes'], asset_sha256=case['asset_sha256']))
            save_report()
            print(name, 'FAIL native rendering; see retained log', flush=True)
            continue
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
        maximum = max(errors, key=lambda e: e['error']) if errors else dict(error=0, step=None, variable=None, reference=None, native=None)
        scalar_passed = maximum['error'] <= baseline['scalar_tolerance']
        weight_errors, playing_errors = [], []
        observed_timelines = case.get('observed_timeline_weights', [])
        if observed_timelines and not metadata.get('timelineBlendQueryAvailable'):
            raise ValueError('SDK timeline weight query unavailable')
        for a, b in zip(source_samples, native['samples']):
            for label in observed_timelines:
                weight_errors.append(dict(step=a['step'], timeline=label,
                    reference=a['blendRatios'][label], native=b['blend_ratios'][label],
                    error=abs(a['blendRatios'][label] - b['blend_ratios'][label])))
                reference_playing = bool(a['playing'][metadata['timelines'].index(label)])
                if reference_playing != b['playing'][label]:
                    playing_errors.append(dict(step=a['step'], timeline=label,
                        reference=reference_playing, native=b['playing'][label]))
        weight_maximum = max(weight_errors, key=lambda e: e['error']) if weight_errors else None
        weights_passed = not playing_errors and (weight_maximum is None or
            weight_maximum['error'] <= baseline['scalar_tolerance'])
        difference_errors = []
        for a, b in zip(source_samples, native['samples']):
            for source, variables in case.get('observed_differences', {}).items():
                for variable in variables:
                    expected_value = a['differences'][source][variable]
                    actual_value = b['differences'][source][variable]
                    difference_errors.append(dict(step=a['step'], source=source, variable=variable,
                        reference=expected_value, native=actual_value,
                        error=abs(expected_value - actual_value)))
        difference_maximum = max(difference_errors, key=lambda e: e['error']) if difference_errors else None
        differences_passed = difference_maximum is None or difference_maximum['error'] <= baseline['scalar_tolerance']
        geometry = compare(native, json.loads(pathlib.Path(str(reference) + '.trace.json').read_text()))
        (args.output / (name + '-geometry.json')).write_text(json.dumps(geometry, indent=2) + '\n')
        comparison = args.output / (name + '-comparison')
        with (args.output / (name + '-compare.log')).open('w') as log:
            status = subprocess.run(['python3', str(repo / 'tools/reference_capture/compare_images.py'),
                str(reference) + '.png', str(args.output / (name + '-native.png')),
                '--output', str(comparison), '--tolerance', str(baseline['image_tolerance'])],
                stdout=log, stderr=subprocess.STDOUT).returncode
        image = json.loads(pathlib.Path(str(comparison) + '.json').read_text())
        passed = scalar_passed and differences_passed and weights_passed and geometry['passed'] and status == 0 and image['passed']
        results.append(dict(case=name, model=model, source_hashes=case['source_hashes'],
                            asset_sha256=case['asset_sha256'], scalar_samples=len(errors),
                            scalar_maximum=maximum, scalar_passed=scalar_passed,
                            difference_samples=len(difference_errors),
                            difference_maximum=difference_maximum, differences_passed=differences_passed,
                            timeline_weight_samples=len(weight_errors),
                            timeline_weight_maximum=weight_maximum, playing_errors=playing_errors,
                            timeline_weights_passed=weights_passed,
                            geometry=geometry, image=image, passed=passed))
        save_report()
        print(name, 'PASS' if passed else 'FAIL', 'scalar=', maximum['error'],
              'geometry=', geometry['frame_max_error_px'], 'image pixels=',
              image['pixels_over_tolerance'], flush=True)
    if not all(c['passed'] for c in results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
