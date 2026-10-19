#!/usr/bin/env python3
"""Compare procedural PSB inputs with the actual native evaluator and renderer.

Only public SDK calls, submitted GPU geometry/state and final images are observed.
The original driver files are served unchanged; shader source and core execution
observers are disabled. Run with exclusive GPU access, as in the README.
"""
import argparse
import hashlib
import json
import math
import pathlib
import shutil
import subprocess
import sys
from synthetic_model import cases, write_model


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=pathlib.Path, required=True)
    parser.add_argument('--browser', type=pathlib.Path, required=True)
    parser.add_argument('--importer', type=pathlib.Path, required=True)
    parser.add_argument('--evaluator', type=pathlib.Path, required=True)
    parser.add_argument('--preview', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--reuse-reference', type=pathlib.Path,
                        help='reuse captures only after checking model, driver and input hashes')
    parser.add_argument('--case', action='append', default=[],
                        help='select named cases; repeat to run a focused follow-up')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    frozen = args.output/'bin'
    frozen.mkdir()
    for option in ['importer', 'evaluator', 'preview']:
        original = getattr(args, option)
        destination = frozen/original.name
        shutil.copy2(original, destination)
        setattr(args, option, destination)
    here = pathlib.Path(__file__).resolve().parent
    inputs = cases()
    if args.case:
        unknown = set(args.case) - {c['name'] for c in inputs}
        if unknown:
            parser.error('unknown cases: ' + ', '.join(sorted(unknown)))
        inputs = [c for c in inputs if c['name'] in args.case]
    report = dict(observation='SDK / submitted geometry / final image; no shader or core inspection',
                  geometry_tolerance_px=0.001, image_tolerance=2,
                  source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                  hashes={str(p): sha(p) for p in [
                      args.browser, args.importer, args.evaluator, args.preview,
                      args.reference_root/'driver/FreeMoteDriver.js',
                      args.reference_root/'driver/emoteplayer.js',
                      here/'synthetic_model.py', here/'validate_synthetic.py',
                      here/'capture.py', here/'capture.cjs', here/'reference.html', here/'trace.js',
                      pathlib.Path('src/runtime/geometry_scene.cpp')]},
                  cases=[], passed=False)
    summary = args.output/'results.json'

    def save():
        summary.write_text(json.dumps(report, indent=2) + '\n')

    for case in inputs:
        directory = args.output/case['name']
        write_model(directory, case['document'])
        x, y, scale = case.get('x', 0), case.get('y', 0), case.get('scale', 1)
        variables = case['variables']
        entry = dict(name=case['name'], variables=variables, placement=[x, y, scale],
                     asset_hashes=json.loads((directory/'manifest.json').read_text())['files'],
                     commands=[], passed=False)
        report['cases'].append(entry)

        def run(command, name):
            command = [str(value) for value in command]
            entry['commands'].append(command)
            result = subprocess.run(command, capture_output=True, text=True)
            (directory/(name+'.log')).write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f'{name} exited {result.returncode}; see {directory}/{name}.log')

        try:
            previous = args.reuse_reference/case['name'] if args.reuse_reference else None
            if previous and (previous/'reference.json').is_file():
                metadata = json.loads((previous/'reference.json').read_text())
                previous_report = json.loads((args.reuse_reference/'results.json').read_text())
                previous_case = next(c for c in previous_report['cases'] if c['name'] == case['name'])
                if (metadata['source_hashes']['psb'] != sha(directory/'model.psb') or
                    not metadata.get('traceGeometry') or
                    metadata['steps'] != 0 or metadata['opacity'] != 1 or
                    metadata['glError'] != 0 or metadata['visiblePixels'] <= 0 or
                    metadata['physics'] != dict(hair=0, parts=0, bust=0, wind=False) or
                    any(metadata[k] for k in ['traceMath', 'traceFloatStores',
                        'traceFloatAllReads', 'traceFloatReadIndices', 'traceIntegerStores',
                        'traceIntegerReads', 'traceRandom', 'traceClock', 'timelines', 'plays',
                        'transitions', 'events', 'differences', 'diagnostic_fixture_edits']) or
                    [metadata[k] for k in ['x', 'y', 'scale']] != [x, y, scale] or
                    metadata['variables'] != variables or
                    metadata['width'] != 256 or metadata['height'] != 256 or
                    any(metadata['source_hashes'][name] !=
                        sha(args.reference_root/'driver'/name)
                        for name in ['FreeMoteDriver.js', 'emoteplayer.js'])):
                    raise ValueError('reused reference input or driver does not match')
                if previous_report['hashes'].get(str(args.browser)) != sha(args.browser):
                    raise ValueError('reused reference browser does not match')
                entry['reused_reference'] = str(previous)
                for suffix in ['.json', '.png', '.trace.json', '.state.json']:
                    source = previous/('reference'+suffix)
                    if previous_case['reference_hashes'][source.name] != sha(source):
                        raise ValueError('reused reference artifact hash does not match')
                    shutil.copy2(source, directory/source.name)
            else:
                run([sys.executable, here/'capture.py', '--reference-root', args.reference_root,
                     '--psb', directory/'model.psb', '--browser', args.browser,
                     '--output', directory/'reference', '--width', '256', '--height', '256',
                     '--x', x, '--y', y, '--scale', scale, '--variables', json.dumps(variables),
                     '--trace-geometry', '--capture-timeout-ms', '10000'], 'reference')
            entry['reference_hashes'] = {('reference'+suffix): sha(directory/('reference'+suffix))
                                        for suffix in ['.json', '.png', '.trace.json', '.state.json']}
            run([args.importer, directory/'model.json', directory/'model.resx.json',
                 '--output', directory/'native'], 'import')
            run([args.evaluator, '--asset', directory/'native/model.nkm',
                 '--sample-trace', directory/'reference.trace.json',
                 '--variables', json.dumps(variables), '--output', directory/'geometry.json',
                 '--dump-frame'], 'geometry')
            geometry = json.loads((directory/'geometry.json').read_text())
            entry['geometry'] = {k: geometry[k] for k in [
                'max_error', 'frame_max_error_px', 'frame_error', 'primitives',
                'missing_primitives', 'unmatched_reference_vertices', 'unused_native_vertices',
                'missing_reference_triangles', 'unused_native_triangles']}
            # Count/topology coverage prevents an empty or partial comparison passing.
            geometry_passed = (geometry['primitives'] > 0 and geometry['frame_error'] == 0 and
                               math.isfinite(geometry['max_error']) and
                               math.isfinite(geometry['frame_max_error_px']) and
                               geometry['max_error'] <= report['geometry_tolerance_px'] and
                               geometry['frame_max_error_px'] <= report['geometry_tolerance_px'] and
                               not geometry['missing_primitives'] and
                               all(geometry[k] == 0 for k in [
                                   'unmatched_reference_vertices', 'unused_native_vertices',
                                   'missing_reference_triangles', 'unused_native_triangles']))
            command = [args.preview, '--asset', directory/'native/model.nkm',
                       '--geometry-diagnostic', '--hidden', '--width', '256', '--height', '256',
                       '--origin', 128+x, 128+y, '--scale', scale,
                       '--screenshot', directory/'native.png']
            for label, value in variables.items():
                command.extend(['--variable', label, value])
            run(command, 'render')
            run([sys.executable, here/'compare_images.py', directory/'reference.png',
                 directory/'native.png', '--output', directory/'comparison'], 'image')
            entry['image'] = json.loads((directory/'comparison.json').read_text())
            entry['passed'] = geometry_passed and entry['image']['passed']
            trace = json.loads((directory/'reference.trace.json').read_text())
            if any(draw.get('shaders') for draw in trace):
                raise RuntimeError('geometry-only capture unexpectedly contains shader source')
        except Exception as error:
            entry['error'] = str(error)
            entry['passed'] = False
        print(case['name'], 'PASS' if entry['passed'] else 'FAIL',
              entry.get('geometry', {}).get('max_error', entry.get('error', '')), flush=True)
        save()
    report['passed'] = all(c['passed'] for c in report['cases'])
    report['passed_count'] = sum(c['passed'] for c in report['cases'])
    save()
    if not report['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
