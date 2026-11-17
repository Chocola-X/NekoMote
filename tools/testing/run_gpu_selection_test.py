#!/usr/bin/env python3
"""Exercise GPU selection in a private KWin/XWayland session; no desktop input."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import tempfile

from run_layer_shell_test import private_environment

REPO = Path(__file__).resolve().parents[2]


def worker(args):
    evidence = args.evidence.resolve()
    binaries = {name: args.build.resolve() / 'bin' / ('nekomote_' + name)
                for name in ('player', 'x11', 'layer_shell')}
    model = args.assets.resolve() / 'characters/chocola/date-a'
    results = {'cases': [], 'binary_sha256': {
        name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in binaries.items()}}
    for name, binary in binaries.items():
        if args.measure_only:
            break
        result = subprocess.run([str(binary), '--list-gpus'], capture_output=True, text=True,
                                timeout=15)
        (evidence / (name + '-devices.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError('GPU discovery failed: ' + name)
        for backend in ('opengl', 'vulkan'):
            for gpu in ['default', *args.gpu, 'pci:ffff:ff:1f.7']:
                label = '-'.join((name, backend, gpu.replace(':', '_')))
                capture = evidence / (label + ('.png' if name == 'player' else '.ppm'))
                command = [str(binary), '--model', str(model / 'model.nkm'), '--session',
                           str(model / 'session.nks.json'), '--backend', backend,
                           '--config', str(evidence / (label + '.json')), '--mute']
                if name == 'player':
                    command += ['--width', '320', '--height', '240', '--fit', '--no-save',
                                '--capture', str(capture), '--steps', '4']
                else:
                    command += ['--frames', '4', '--capture', str(capture)]
                if gpu != 'default':
                    command += ['--gpu', gpu]
                result = subprocess.run(command, capture_output=True, text=True, timeout=45)
                log = result.stdout + result.stderr
                (evidence / (label + '.log')).write_text(log)
                expected = gpu != 'pci:ffff:ff:1f.7'
                matched = (gpu == 'default' or gpu in log)
                passed = ((result.returncode == 0) == expected and
                          (not expected or (matched and capture.exists())))
                results['cases'].append(dict(name=label, command=command,
                                              exit_code=result.returncode, passed=passed))
                print(label, result.returncode, 'PASS' if passed else 'FAIL', flush=True)
                (evidence / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    if args.measure or args.measure_only:
        for backend in ('opengl', 'vulkan'):
            for gpu in ['default', *args.gpu]:
                label = 'measure-' + backend + '-' + gpu.replace(':', '_')
                command = [str(binaries['player']), '--model', str(model / 'model.nkm'),
                           '--session', str(model / 'session.nks.json'), '--backend', backend,
                           '--config', str(evidence / (label + '.json')), '--mute', '--no-save',
                           '--width', '320', '--height', '240', '--fit', '--fps', '60',
                           '--measure-frames', '120']
                if gpu != 'default':
                    command += ['--gpu', gpu]
                result = subprocess.run(command, capture_output=True, text=True, timeout=45)
                log = result.stdout + result.stderr
                (evidence / (label + '.log')).write_text(log)
                passed = result.returncode == 0 and 'P99=' in log
                results['cases'].append(dict(name=label, command=command,
                                              exit_code=result.returncode, passed=passed))
                print(label, result.returncode, 'PASS' if passed else 'FAIL', flush=True)
    results['passed'] = all(case['passed'] for case in results['cases'])
    (evidence / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0 if results['passed'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--gpu', action='append', required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--measure', action='store_true')
    parser.add_argument('--measure-only', action='store_true')
    parser.add_argument('--worker', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.worker:
        return worker(args)
    args.evidence.resolve().mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix='nkm-gpu-') as temporary:
        root = Path(temporary)
        env = private_environment(root)
        session = root / 'session.sh'
        session.write_text('#!/bin/sh\nexec ' + shlex.join([
            sys.executable, str(Path(__file__).resolve()), *sys.argv[1:], '--worker']) + '\n')
        session.chmod(0o700)
        command = ['dbus-run-session', '--', 'kwin_wayland', '--virtual', '--xwayland',
                   '--width', '640', '--height', '480', '--socket', 'nekomote-gpu-test',
                   '--no-lockscreen', '--no-global-shortcuts', '--no-kactivities',
                   '--exit-with-session', str(session)]
        with (REPO / 'build/gpu-validation.lock').open('a') as lock, \
                (args.evidence / 'kwin.log').open('w') as log:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
            process = subprocess.Popen(command, env=env, stdout=log, stderr=log,
                                       stdin=subprocess.DEVNULL, start_new_session=True)
            try:
                return process.wait(timeout=600)
            finally:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                process.wait(timeout=10)


if __name__ == '__main__':
    raise SystemExit(main())
