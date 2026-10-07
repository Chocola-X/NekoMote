#!/usr/bin/env python3
"""Compare independent native bm19/attr2 frames to a frozen WebGL request matrix."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import zipfile

from validate_static import run
from compare_playback_frame import compare


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reference-root', 'preview', 'evaluator', 'playback-evaluator', 'frame-renderer',
                 'inputs', 'output'):
        parser.add_argument('--' + name, type=pathlib.Path, required=True)
    args = parser.parse_args()
    matrix = json.loads(args.inputs.read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    results = []
    for case in matrix['cases']:
        name = case['case']
        asset = pathlib.Path(case['asset'])
        reference = pathlib.Path(case['reference'])
        if sha(asset) != case['asset_sha256']:
            raise ValueError('asset changed: ' + name)
        for suffix, expected in case['reference_hashes'].items():
            if sha(pathlib.Path(str(reference) + suffix)) != expected:
                raise ValueError('reference changed: ' + name + suffix)
        with zipfile.ZipFile(args.reference_root / 'data' /
                             (case['model'] + '.pure.psb.zip')) as archive:
            members = [p for p in archive.namelist() if p.endswith('.psb')]
            if len(members) != 1 or hashlib.sha256(archive.read(members[0])).hexdigest() != \
                    case['source_hashes']['original_psb']:
                raise ValueError('source PSB changed: ' + name)
        for driver in ('FreeMoteDriver.js', 'emoteplayer.js'):
            if sha(args.reference_root / 'driver' / driver) != case['source_hashes'][driver]:
                raise ValueError('source driver changed: ' + driver)
        metadata = json.loads(reference.with_suffix('.json').read_text())
        expected = dict(variables=case['variables'], steps=case['steps'],
                        opacity=case['opacity'], x=case['origin'][0], y=case['origin'][1],
                        scale=case['scale'], width=800, height=1080,
                        physics=dict(hair=0, parts=0, bust=0, wind=False))
        if any(metadata.get(k) != v for k, v in expected.items()) or \
                metadata['diagnostic_fixture_edits']:
            raise ValueError('reference inputs differ: ' + name)
        native = args.output / name
        geometry_path = args.output / (name + '-geometry.json')
        command = [args.preview, '--asset', asset, '--origin',
                   case['origin'][0] + 400, case['origin'][1] + 540,
                   '--scale', case['scale'], '--opacity', case['opacity'],
                   '--hidden', '--screenshot', str(native) + '.png']
        probe = [args.evaluator, '--asset', asset, '--variables',
                 json.dumps(case['variables']), '--sample-trace', str(reference) + '.trace.json',
                 '--output', geometry_path]
        command += ['--geometry-diagnostic']
        for key, value in case['variables'].items():
            command += ['--variable', key, value]
        try:
            # Nonzero author steps require managed Eye/controller inputs even
            # with zero physical amplitudes. The isolated animation diagnostic
            # intentionally uses manual controllers and is a different request.
            if case['steps']:
                if case['opacity'] != 1:
                    raise ValueError('timed frame capture currently requires opacity=1')
                request = dict(steps=case['steps'], origin=case['origin'], scale=case['scale'],
                               variables=case['variables'], plays=[], random_seed=0,
                               amplitudes=dict(hair=0, parts=0, bust=0), dump_frame=True)
                request_path = args.output / (name + '-inputs.json')
                observation = args.output / (name + '-native.json')
                request_path.write_text(json.dumps(request, indent=2) + '\n')
                run([args.playback_evaluator, asset, request_path, observation],
                    args.output / (name + '-geometry.log'))
                run([args.frame_renderer, asset, observation, str(native) + '.png'],
                    native.with_suffix('.log'))
                geometry = compare(json.loads(observation.read_text()),
                                   json.loads(pathlib.Path(str(reference) + '.trace.json').read_text()))
                geometry_passed = geometry['passed']
                geometry_path.write_text(json.dumps(geometry, indent=2) + '\n')
            else:
                run(command, native.with_suffix('.log'))
                run(probe, args.output / (name + '-geometry.log'))
                geometry = json.loads(geometry_path.read_text())
                geometry_passed = (geometry['frame_error'] == 0 and
                               not geometry['missing_primitives'] and
                               geometry['frame_max_error_px'] <= matrix['coordinate_tolerance_px'] and
                               all(geometry[k] == 0 for k in (
                                   'unmatched_reference_vertices', 'unused_native_vertices',
                                   'missing_reference_triangles', 'unused_native_triangles')))
            comparison = args.output / (name + '-comparison')
            with (args.output / (name + '-comparison.log')).open('w') as log:
                status = subprocess.run(
                    [str(v) for v in ('python3', pathlib.Path(__file__).with_name('compare_images.py'),
                     str(reference) + '.png', str(native) + '.png', '--output', comparison,
                     '--tolerance', matrix['image_tolerance'])],
                    stdout=log, stderr=subprocess.STDOUT).returncode
            image = json.loads(comparison.with_suffix('.json').read_text())
            row = dict(case=name, passed=geometry_passed and status == 0 and image['passed'],
                       geometry_passed=geometry_passed, geometry=geometry, image=image)
        except RuntimeError as error:
            row = dict(case=name, passed=False, error=str(error))
        results.append(row)
        report = dict(passed=all(r['passed'] for r in results), cases=results,
                      inputs_sha256=sha(args.inputs),
                      binaries={str(p): sha(p) for p in (args.preview, args.evaluator,
                                args.playback_evaluator, args.frame_renderer)},
                      full_catalog_certified=False)
        (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(name, 'PASS' if row['passed'] else 'FAIL', flush=True)
    if not all(r['passed'] for r in results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
