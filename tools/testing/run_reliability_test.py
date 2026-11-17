#!/usr/bin/env python3
"""Opt-in reliability audit in an owned KWin/XWayland/DBus session only.

No desktop input injection, installation or user-session reconfiguration.
CPU samples exclude Wayland tracing and pixel readback. Protocol traces are a
separate run. Physical connector/power behavior remains separate validation.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import shlex
import signal
import shutil
import subprocess
import sys
import tempfile
import time

from run_layer_shell_test import private_environment, stop

REPO = Path(__file__).resolve().parents[2]


def cpu_seconds(pid):
    fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')


def worker(args):
    root = args.evidence_dir
    private = args.private_root
    if (os.environ.get('NEKOMOTE_RELIABILITY_PRIVATE_ROOT') != str(private) or
            os.environ.get('XDG_RUNTIME_DIR') != str(private / 'runtime') or
            not os.environ.get('DISPLAY') or not os.environ.get('DBUS_SESSION_BUS_ADDRESS')):
        raise RuntimeError('audit requires its owned compositor and private session')
    env = dict(os.environ)
    env.update(DBUS_SYSTEM_BUS_ADDRESS=env['DBUS_SESSION_BUS_ADDRESS'],
               NEKOMOTE_TEST_SESSION_BUS=env['DBUS_SESSION_BUS_ADDRESS'],
               QT_QPA_PLATFORM='wayland')
    panel = root / 'panel.json'
    panel.write_text(json.dumps(dict(version=1, fps=60, mute=True)))
    results = dict(scope='private KWin virtual outputs; no physical power or login lock',
                   cases=[], passed=False)
    service_log = (root / 'session-service.log').open('w')
    service = subprocess.Popen([str(args.probe), '--serve'], env=env,
                               stdout=service_log, stderr=service_log)
    def control(action):
        subprocess.run(['busctl', '--address=' + env['DBUS_SESSION_BUS_ADDRESS'], 'call',
                        'org.freedesktop.ScreenSaver', '/ScreenSaver',
                        'nekomote.Test', 'Set', 's', action], env=env, check=True, timeout=5)

    def sample(child, seconds):
        before = cpu_seconds(child.pid)
        time.sleep(seconds)
        if child.poll() is not None:
            raise RuntimeError('wallpaper exited unexpectedly')
        return round(cpu_seconds(child.pid) - before, 6)

    try:
        time.sleep(.3)
        listing = subprocess.run([str(args.layer), '--list-outputs'], env=env,
                                 capture_output=True, text=True, check=True, timeout=10)
        output = listing.stdout.splitlines()[0]
        xlist = subprocess.run([str(args.x11), '--list-outputs'], env=env,
                               capture_output=True, text=True, check=True, timeout=10)
        xoutput = xlist.stdout.splitlines()[0].split()[0]
        shells = [] if args.skip_native else [('layer', args.layer, args.before_layer, output),
                                                 ('x11', args.x11, args.before_x11, xoutput)]
        for shell, current, baseline, target in shells:
            for backend in args.backends:
                versions = [('after', current)]
                if baseline:
                    versions.insert(0, ('before', baseline))
                for version, binary in versions:
                    name = f'{shell}-{backend}-{version}'
                    command = [str(binary), '--backend', backend, '--model', str(args.model),
                               '--session', str(args.session), '--output', target,
                               '--mute', '--no-save', '--state', str(panel)]
                    log = (root / (name + '.log')).open('w')
                    child = subprocess.Popen(command, env=env, stdout=log, stderr=log)
                    try:
                        time.sleep(2)
                        case = dict(name=name, active_cpu_seconds=sample(child, 3))
                        control('lock')
                        time.sleep(1.5)
                        case['locked_cpu_seconds'] = sample(child, 3)
                        control('unlock')
                        time.sleep(1.5)
                        case['resumed_cpu_seconds'] = sample(child, 3)
                        if version == 'after':
                            if case['locked_cpu_seconds'] > .04:
                                raise RuntimeError(name + ': excessive CPU while locked')
                            if case['resumed_cpu_seconds'] <= .02:
                                raise RuntimeError(name + ': playback did not resume')
                        results['cases'].append(case)
                    finally:
                        control('unlock')
                        stop(child)
                        log.close()
                    print(json.dumps(case), flush=True)
        if args.frame_benchmark:
            import resource
            for shell, current, baseline, target in [('layer', args.layer, args.before_layer, output),
                                                       ('x11', args.x11, args.before_x11, xoutput)]:
                if shell not in args.benchmark_shells:
                    continue
                if not baseline:
                    raise RuntimeError('frame benchmark requires both frozen baseline binaries')
                for backend in args.backends:
                    for round_index in range(args.benchmark_rounds):
                        versions = [('before', baseline), ('after', current)]
                        if round_index % 2:
                            versions.reverse()
                        for version, binary in versions:
                            name = f'frames-{shell}-{backend}-{version}-{round_index}'
                            command = [str(binary), '--backend', backend, '--model', str(args.model),
                                '--session', str(args.session), '--output', target, '--mute', '--no-save',
                                '--state', str(panel), '--input', 'passthrough', '--frames', '600' if args.present_metrics else '360']
                            with (root / (name + '.log')).open('w') as log:
                                before_cpu = resource.getrusage(resource.RUSAGE_CHILDREN)
                                started = time.monotonic()
                                metrics = root / (name + '-metrics.json')
                                benchmark_env = dict(env)
                                if args.present_metrics:
                                    benchmark_env.update(LD_PRELOAD=str(args.present_metrics),
                                        NEKOMOTE_PRESENT_METRICS_FILE=str(metrics))
                                child = subprocess.Popen(command, env=benchmark_env, stdout=log, stderr=log)
                                try:
                                    if child.wait(timeout=35):
                                        raise RuntimeError(name + ' failed')
                                finally:
                                    stop(child)
                                elapsed = time.monotonic() - started
                                after_cpu = resource.getrusage(resource.RUSAGE_CHILDREN)
                            cpu = after_cpu.ru_utime + after_cpu.ru_stime - before_cpu.ru_utime - before_cpu.ru_stime
                            case = dict(name=name, submitted_frames=360, process_cpu_seconds=round(cpu, 6),
                                        wall_seconds=round(elapsed, 6), cpu_ms_per_frame=round(cpu * 1000 / 360, 6),
                                        includes_startup_and_teardown=True)
                            if args.present_metrics:
                                case['steady'] = json.loads(metrics.read_text())
                                case['submitted_frames'] = 600
                                case.pop('cpu_ms_per_frame')
                            results['cases'].append(case)
                            print(json.dumps(case), flush=True)
        if args.hotplug:
            from hotplug_checks import check_hotplug
            check_hotplug(args, env, output, xoutput, panel, results, cpu_seconds)
        if args.plasma_probe:
            for backend in args.backends:
                completed = subprocess.run([str(args.plasma_probe)],
                    env=dict(env, NEKOMOTE_TEST_ASSETS_DIR=str(REPO / 'assets'),
                             NEKOMOTE_TEST_BACKEND=backend, NEKOMOTE_QT_PROFILE='1'), capture_output=True, text=True, timeout=50)
                (root / ('plasma-' + backend + '.log')).write_text(completed.stdout + completed.stderr)
                if completed.returncode:
                    raise RuntimeError('Plasma session test failed: ' + backend)
                results['cases'].append(dict(name='plasma-' + backend, passed=True))
        if args.plasma_pacing:
            completed = subprocess.run([str(args.plasma_pacing)],
                env=dict(env, NEKOMOTE_TEST_ASSETS_DIR=str(REPO / 'assets'),
                         QT_QPA_PLATFORM='xcb', NEKOMOTE_TEST_GL_API='desktop'),
                capture_output=True, text=True, timeout=100)
            (root / 'plasma-pacing.log').write_text(completed.stdout + completed.stderr)
            if completed.returncode:
                raise RuntimeError('Plasma pacing regression failed')
            results['cases'].append(dict(name='plasma-pacing', passed=True))
        if args.power_check:
            command = [str(args.layer), '--model', str(args.model), '--session', str(args.session),
                       '--output', output, '--mute', '--no-save', '--state', str(panel)]
            with (root / 'power-client.log').open('w') as log:
                child = subprocess.Popen(command, env=env, stdout=log, stderr=log)
                try:
                    time.sleep(2)
                    case = dict(name='virtual-output-dpms', active_cpu_seconds=sample(child, 3))
                    for mode in ['off', 'on']:
                        completed = subprocess.run(['kscreen-doctor', '--dpms', mode],
                            env=dict(env, WAYLAND_DEBUG='client'), capture_output=True, text=True, timeout=10)
                        (root / ('dpms-' + mode + '.log')).write_text(completed.stdout + completed.stderr)
                        if completed.returncode:
                            raise RuntimeError('private DPMS command failed: ' + mode)
                        time.sleep(1.5)
                        case[mode + '_cpu_seconds'] = sample(child, 3)
                    trace = (root / 'dpms-off.log').read_text()
                    case['supported'] = '.supported(1)' in trace
                    if not case['supported']:
                        case['skipped_reason'] = 'KWin virtual outputs report DPMS supported(0)'
                    results['cases'].append(case)
                finally:
                    stop(child)
        results['passed'] = True
    finally:
        stop(service)
        service_log.close()
        (root / 'results.json').write_text(json.dumps(results, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['layer', 'x11', 'probe', 'model', 'session', 'before-layer', 'before-x11']:
        parser.add_argument('--' + name, type=lambda value: Path(value).resolve(strict=True),
                            required=not name.startswith('before'))
    parser.add_argument('--present-metrics', type=lambda value: Path(value).resolve(strict=True))
    parser.add_argument('--frame-benchmark', action='store_true')
    parser.add_argument('--benchmark-rounds', type=int, choices=range(1, 21), default=3)
    parser.add_argument('--benchmark-shells', nargs='+', choices=['layer', 'x11'], default=['layer', 'x11'])
    parser.add_argument('--hotplug', action='store_true')
    parser.add_argument('--skip-native', action='store_true')
    parser.add_argument('--plasma-probe', type=lambda value: Path(value).resolve(strict=True))
    parser.add_argument('--plasma-pacing', type=lambda value: Path(value).resolve(strict=True))
    parser.add_argument('--power-check', action='store_true')
    parser.add_argument('--backends', nargs='+', choices=['opengl', 'vulkan'], default=['opengl', 'vulkan'])
    parser.add_argument('--compositor', type=Path, default=Path('/usr/bin/kwin_wayland'))
    parser.add_argument('--evidence-dir', type=Path, required=True)
    parser.add_argument('--private-root', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    args.evidence_dir = args.evidence_dir.resolve()
    if args.private_root:
        worker(args)
        return
    args.evidence_dir.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix='nkm-reliability-') as directory:
        private = Path(directory)
        env = private_environment(private)
        env['NEKOMOTE_RELIABILITY_PRIVATE_ROOT'] = str(private)
        env['DBUS_SYSTEM_BUS_ADDRESS'] = 'unix:path=' + str(private / 'no-system-bus')
        script = private / 'session.sh'
        command = [sys.executable, str(Path(__file__).resolve()), *sys.argv[1:], '--private-root', directory]
        script.write_text('#!/bin/sh\nexec ' + shlex.join(command) + '\n')
        script.chmod(0o700)
        lock_path = REPO / 'build/gpu-validation.lock'
        with lock_path.open('a') as lock, (args.evidence_dir / 'kwin.log').open('w') as log:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
            compositor = subprocess.Popen([
                'dbus-run-session', '--', str(args.compositor), '--virtual', '--xwayland',
                '--output-count', '2', '--width', '800', '--height', '600',
                '--socket', 'nekomote-reliability', '--no-lockscreen', '--no-global-shortcuts',
                '--no-kactivities', '--exit-with-session', str(script)], env=env,
                stdout=log, stderr=log, start_new_session=True)
            try:
                code = compositor.wait(timeout=1000 if args.frame_benchmark else 400)
            finally:
                try:
                    os.killpg(compositor.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                stop(compositor)
                # A portal activated on this private bus can leave its owned FUSE
                # endpoint disconnected after DBus exits. Never unmount user paths.
                if shutil.which('fusermount3'):
                    subprocess.run(['fusermount3', '-uz', str(private / 'runtime/doc')],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
            result_file = args.evidence_dir / 'results.json'
            if code or not result_file.exists() or not json.loads(result_file.read_text())['passed']:
                raise RuntimeError('audit failed; see ' + str(args.evidence_dir))


if __name__ == '__main__':
    main()
