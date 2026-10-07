#!/usr/bin/env python3
"""Package existing observations; derive status from geometry and image reports.

This tool does not capture models or infer source semantics. Output must be new;
input screenshots, traces and earlier ledgers are never overwritten.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import datetime


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reference', 'native', 'geometry', 'output'):
        parser.add_argument('--' + name, type=pathlib.Path, required=True)
    parser.add_argument('--geometry-format', choices=('pose', 'playback'), required=True)
    args = parser.parse_args()
    geometry = json.loads(args.geometry.read_text())
    counts = ('unmatched_reference_vertices', 'unused_native_vertices',
              'missing_reference_triangles', 'unused_native_triangles')
    geometry_ok = geometry['frame_max_error_px'] <= .001 and all(not geometry[k] for k in counts)
    if args.geometry_format == 'pose':
        geometry_ok = (geometry_ok and geometry['frame_error'] == 0 and
                       not geometry['missing_primitives'] and geometry['frame_vertices'] > 0 and
                       geometry['frame_indices'] > 0)
    else:
        geometry_ok = (geometry_ok and bool(geometry['commands']) and
                       all(command['samples'] > 0 for command in geometry['commands']))
    args.output.mkdir(parents=True, exist_ok=False)
    comparator = pathlib.Path(__file__).with_name('compare_images.py')
    with (args.output / 'compare.log').open('w') as log:
        result = subprocess.run([sys.executable, str(comparator), str(args.reference),
                                 str(args.native), '--output', str(args.output / 'comparison')],
                                stdout=log, stderr=subprocess.STDOUT)
    pixels_file = args.output / 'comparison.json'
    pixels = json.loads(pixels_file.read_text()) if pixels_file.exists() else None
    measured = geometry_ok and pixels is not None
    report = dict(generated_at=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  geometry_format=args.geometry_format, geometry_passed=bool(geometry_ok),
                  geometry_max_px=geometry['frame_max_error_px'] if geometry_ok else None,
                  pixels=pixels, compare_exit=result.returncode,
                  passed=bool(measured and pixels['passed']),
                  files={str(p): sha(p) for p in
                         (args.reference, args.native, args.geometry, comparator, pathlib.Path(__file__))})
    (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(passed=report['passed'], output=str(args.output))))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
