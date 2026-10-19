#!/usr/bin/env python3
"""Reproduce the recorded P3 experiment matrix using explicit external inputs."""
import argparse
import json
import pathlib
import subprocess


def run(*args):
    subprocess.run([str(v) for v in args], check=True, stdout=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=pathlib.Path, required=True)
    parser.add_argument('--importer', type=pathlib.Path, required=True)
    parser.add_argument('--native-evaluator', type=pathlib.Path,
                        help='optional compiled nekomote_pose; verifies actual vertices and topology')
    parser.add_argument('--browser', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True,
                        help='new directory; never overwrites existing evidence')
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    baseline = json.loads((root / 'docs/validation/p3-geometry.json').read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    imported = set()
    reports = []
    for case in baseline['cases']:
        model, name = case['model'], case['case']
        asset = args.output / model / 'model.nkm'
        if model not in imported:
            source = args.reference_root / 'model_conversion' / model.split('-')[0] / model / model
            run(args.importer, str(source)+'.json', str(source)+'.resx.json',
                '--output', asset.parent)
            imported.add(model)
        stem = args.output / 'reference' / (model+'-'+name)
        run('python3', root / 'tools/reference_capture/capture.py',
            '--reference-root', args.reference_root, '--browser', args.browser,
            '--model', model, '--variables', json.dumps(case['variables']),
            '--trace', '--output', stem)
        metadata = json.loads(stem.with_suffix('.json').read_text())
        for key, expected in case['reference_sources'].items():
            if metadata['source_hashes'].get(key) != expected:
                raise ValueError('reference source changed: '+key+'; review the baseline')
        probe = args.output / (model+'-'+name+'-pose.json')
        output = args.output / (model+'-'+name+'-geometry.json')
        run('python3', root / 'tools/pose_probe/probe.py', '--asset', asset,
            '--variables', json.dumps(case['variables']), '--output', probe)
        run('python3', root / 'tools/pose_probe/compare_geometry.py', '--asset', asset,
            '--probe', probe, '--trace', str(stem)+'.trace.json', '--output', output)
        report = json.loads(output.read_text())
        passed = (not report['missing_primitives'] and report['max_error'] is not None and
                  report['max_error'] <= baseline['coordinate_tolerance_px'] and
                  len(report['comparisons']) == case['primitives'])
        native = None
        if args.native_evaluator:
            native_path = args.output / (model+'-'+name+'-native.json')
            run(args.native_evaluator, '--asset', asset, '--variables', json.dumps(case['variables']),
                '--sample-trace', str(stem)+'.trace.json', '--output', native_path)
            native = json.loads(native_path.read_text())
            passed = passed and (
                native['frame_error'] == 0 and not native['missing_primitives'] and
                native['primitives'] == case['primitives'] and
                native['frame_max_error_px'] <= baseline['coordinate_tolerance_px'] and
                native['unmatched_reference_vertices'] == 0 and
                native['unused_native_vertices'] == 0 and
                native['missing_reference_triangles'] == 0 and
                native['unused_native_triangles'] == 0)
        reports.append(dict(model=model, case=name, passed=passed, report=report, native=native))
        (args.output / 'report.json').write_text(json.dumps(reports, ensure_ascii=False, indent=2)+'\n')
        print(model, name, 'PASS' if passed else 'FAIL', report['max_error'], flush=True)
        if not passed:
            raise ValueError('geometry experiment failed: '+model+'/'+name)


if __name__ == '__main__':
    main()
