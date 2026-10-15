#!/usr/bin/env python3
"""Generate and observe mesh.cc hypotheses; this is not semantic acceptance.

Only public SDK input, submitted GPU buffers and the final image are observed.
A finite image does not establish a cc formula. Run under gpu-validation.lock.
"""
import argparse
import copy
import hashlib
import json
import pathlib
import subprocess
import sys

from synthetic_model import model, patch, sprite, write_model


def cases(candidate):
    def make(name, bp_mode, side, time=15, frame_type=3):
        node = sprite()
        node['parameterize'] = 0
        first = copy.deepcopy(node['frameList'][0]['content'])
        last = copy.deepcopy(first)
        if bp_mode != 'absent':
            first['mesh']['bp'] = patch('x', 0 if bp_mode == 'identity' else 0.3)
            last['mesh']['bp'] = patch('x', 0 if bp_mode == 'identity' else -0.2)
        if side in ('left', 'both'):
            first['mesh']['cc'] = copy.deepcopy(candidate)
        if side in ('right', 'both'):
            last['mesh']['cc'] = copy.deepcopy(candidate)
        node['frameList'] = [dict(time=0, type=frame_type, content=first),
                             dict(time=60, type=2, content=last), dict(time=61, type=0)]
        document = model([node], ['p'])
        document['object']['test']['motion']['root']['parameter'] = [
            dict(id='p', rangeBegin=0, rangeEnd=60, division=60,
                 discretization=0, enabled=1)]
        return dict(name=name, bp_mode=bp_mode, cc_side=side, frame_type=frame_type,
                    variables={'p': time}, document=document)

    result = [make(f'{bp}-{side}', bp, side)
              for bp in ('absent', 'identity', 'varying')
              for side in ('none', 'left', 'right', 'both')]
    result.extend([make('identity-left-begin', 'identity', 'left', 0),
                   make('identity-left-end', 'identity', 'left', 60),
                   make('identity-left-hold', 'identity', 'left', frame_type=2)])
    return result


def nonfinite_counts(trace):
    counts = dict(nan=0, positiveInfinity=0, negativeInfinity=0)
    for draw in trace:
        for attribute in draw.get('attributes', {}).values():
            for kind, value in attribute.get('nonfinite', {}).items():
                counts[kind] += value
    return counts


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--cc-json', default='[' + ','.join(['0'] * 32) + ']',
                        help='candidate JSON value; default is 32 zero scalars (unverified)')
    parser.add_argument('--generate-only', action='store_true')
    parser.add_argument('--reference-root', type=pathlib.Path)
    parser.add_argument('--browser', type=pathlib.Path)
    parser.add_argument('--source-commit', help='source Git commit for a source archive')
    args = parser.parse_args()
    if not args.generate_only and (args.reference_root is None or args.browser is None):
        parser.error('--reference-root and --browser are required for observation')
    def reject_constant(value):
        raise ValueError('nonfinite JSON input: ' + value)
    candidate = json.loads(args.cc_json, parse_constant=reject_constant)
    json.dumps(candidate, allow_nan=False)  # Also reject exponent overflow before writing files.
    inputs = cases(candidate)
    here = pathlib.Path(__file__).resolve().parent
    source_commit = args.source_commit
    if source_commit is None:
        try:
            source_commit = subprocess.check_output(
                ['git', '-C', str(here.parents[1]), 'rev-parse', 'HEAD'], text=True).strip()
        except subprocess.CalledProcessError:
            parser.error('source Git metadata unavailable; provide --source-commit')
    if len(source_commit) != 40 or any(c not in '0123456789abcdef' for c in source_commit):
        parser.error('--source-commit requires a full 40-character Git commit')
    args.output.mkdir(parents=True, exist_ok=False)
    report = dict(accepted_semantics=False, candidate=candidate, cases=[],
                  source_commit=source_commit,
                  observation='public SDK / submitted GPU geometry / final image',
                  hashes={p.name: sha(p) for p in [here/'probe_mesh_cc.py',
                          here/'synthetic_model.py', here/'capture.py', here/'capture.cjs',
                          here/'reference.html', here/'trace.js']})
    if not args.generate_only:
        report['hashes'].update({str(p): sha(p) for p in [args.browser,
            args.reference_root/'driver/FreeMoteDriver.js',
            args.reference_root/'driver/emoteplayer.js']})
    summary = args.output/'results.json'
    failed = False
    for case in inputs:
        directory = args.output/case['name']
        write_model(directory, case['document'])
        entry = {k: v for k, v in case.items() if k != 'document'}
        entry['input_hashes'] = json.loads((directory/'manifest.json').read_text())['files']
        entry['status'] = 'not_captured'
        report['cases'].append(entry)
        if not args.generate_only:
            command = [sys.executable, str(here/'capture.py'), '--reference-root',
                       str(args.reference_root), '--browser', str(args.browser), '--psb',
                       str(directory/'model.psb'), '--output', str(directory/'reference'),
                       '--width', '256', '--height', '256', '--x', '0', '--y', '0',
                       '--variables', json.dumps(case['variables']), '--trace-geometry',
                       '--capture-timeout-ms', '3000']
            entry['command'] = command
            process = subprocess.run(command, capture_output=True, text=True)
            (directory/'capture.log').write_text(process.stdout + process.stderr)
            entry['returncode'] = process.returncode
            trace_file = directory/('reference.failure.trace.json' if process.returncode
                                    else 'reference.trace.json')
            if trace_file.exists():
                trace = json.loads(trace_file.read_text())
                if isinstance(trace, dict):
                    trace = trace['trace']
                entry['nonfinite'] = nonfinite_counts(trace)
            entry['status'] = 'capture_failed' if process.returncode else 'captured'
            if not process.returncode:
                entry['visible_pixels'] = json.loads((directory/'reference.json').read_text())[
                    'visiblePixels']
            if any(entry.get('nonfinite', {}).values()):
                entry['status'] = 'nonfinite_gpu_submission'
            failed |= entry['status'] not in ('captured', 'not_captured')
            entry['artifact_hashes'] = {p.name: sha(p) for p in directory.glob('reference.*')}
            print(case['name'], entry['status'], flush=True)
        summary.write_text(json.dumps(report, indent=2) + '\n')
    # Invalid hypotheses stay failures even though all observations are preserved.
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
