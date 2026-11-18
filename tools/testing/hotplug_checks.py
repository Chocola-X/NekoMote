"""Output hotplug assertions called only inside run_reliability_test's private session."""
import hashlib
import json
import re
import subprocess
import time

from run_layer_shell_test import stop


def check_hotplug(args, env, output, xoutput, panel, results, cpu_seconds):
    root = args.evidence_dir
    def validate(path, backend):
        if backend == 'vulkan' and env.get('NEKOMOTE_VK_VALIDATION') == '1':
            text = path.read_text()
            if '[validation]' in text or 'validation errors=0 warnings=0' not in text:
                raise RuntimeError('Vulkan validation warning/error: ' + str(path))
    def change(target, enabled):
        completed = subprocess.run(['kscreen-doctor', 'output.' + target + ('.enable' if enabled else '.disable')],
                                   env=env, capture_output=True, text=True, timeout=10)
        if completed.returncode or re.search(r'not found|failed|error', completed.stdout, re.I):
            raise RuntimeError('private output change failed: ' + completed.stdout + completed.stderr)

    def await_log(child, log_path, text, count=1):
        deadline = time.monotonic() + 8
        while log_path.read_text().count(text) < count:
            if child.poll() is not None:
                raise RuntimeError('hotplug client exited: ' + str(log_path))
            if time.monotonic() > deadline:
                raise RuntimeError('missing ' + text + ': ' + str(log_path))
            time.sleep(.02)

    for shell, binary, target, frozen in [('layer', args.layer, output, args.before_layer),
                                           ('x11', args.x11, xoutput, args.before_x11)]:
        for backend in args.backends:
            if frozen:
                baseline_path = root / f'hotplug-{shell}-{backend}-baseline-removal.log'
                baseline_command = [str(frozen), '--backend', backend, '--model', str(args.model),
                    '--session', str(args.session), '--output', target, '--mute', '--no-save',
                    '--state', str(panel), '--input', 'passthrough']
                with baseline_path.open('w') as log:
                    child = subprocess.Popen(baseline_command, env=env, stdout=log, stderr=log)
                    try:
                        await_log(child, baseline_path, ' background ' if shell == 'layer' else 'wallpaper window=')
                        time.sleep(.3)
                        change(target, False)
                        if child.wait(timeout=8):
                            raise RuntimeError('unexpected baseline failure during output removal')
                        validate(baseline_path, backend)
                        results['cases'].append(dict(name=f'hotplug-{shell}-{backend}-baseline-removal',
                                                     exited_on_removal=True, passed=True))
                    finally:
                        stop(child)
                        change(target, True)
            for selection in ['named', 'default']:
                name = f'hotplug-{shell}-{backend}-{selection}'
                common = ['--backend', backend, '--model', str(args.model), '--session', str(args.session),
                          '--mute', '--no-save', '--state', str(panel), '--input', 'passthrough']
                if selection == 'named':
                    common += ['--output', target]
                before = root / (name + '-before.ppm')
                after = root / (name + '-after.ppm')
                with (root / (name + '-control.log')).open('w') as log:
                    subprocess.run([str(frozen or binary), *common, '--frames', '240', '--capture', str(before)],
                                   env=env, stdout=log, stderr=log, check=True, timeout=35)
                path = root / (name + '.log')
                with path.open('w') as log:
                    child = subprocess.Popen([str(binary), *common, '--no-session-pause',
                                              '--frames', '240', '--capture', str(after)],
                                             env=env, stdout=log, stderr=log)
                    chosen = target
                    try:
                        await_log(child, path, ' background ' if shell == 'layer' else 'wallpaper window=')
                        if shell == 'layer':
                            await_log(child, path, 'wallpaper target output=')
                            chosen = re.search(r'wallpaper target output=(.+)', path.read_text())[1]
                        else:
                            chosen = re.search(r'backend=\w+ output=(\S+)', path.read_text())[1]
                        time.sleep(.25)
                        for cycle in range(2):
                            change(chosen, False)
                            await_log(child, path, 'waiting for reconnect', cycle + 1)
                            time.sleep(.4)
                            change(chosen, True)
                            await_log(child, path, ' reconnected', cycle + 1)
                            time.sleep(.25)
                        if child.wait(timeout=30):
                            raise RuntimeError('hotplug capture client failed: ' + str(path))
                        events = path.read_text()
                        if events.count('waiting for reconnect') != 2 or events.count(' reconnected') != 2:
                            raise RuntimeError('unexpected output transition logs: ' + name)
                        validate(path, backend)
                        if before.read_bytes() != after.read_bytes():
                            raise RuntimeError('fixed-step image changed after reconnect: ' + name)
                        case = dict(name=name, cycles=2, passed=True,
                                    image_sha256=hashlib.sha256(after.read_bytes()).hexdigest())
                        results['cases'].append(case)
                        print(json.dumps(case), flush=True)
                    finally:
                        stop(child)
                        change(chosen, True)
            # Stop while the native output is absent; GL must still have a valid
            # context for resource destruction and Vulkan must finish its device.
            path = root / f'hotplug-{shell}-{backend}-shutdown.log'
            command = [str(binary), '--backend', backend, '--model', str(args.model),
                       '--session', str(args.session), '--output', target, '--mute', '--no-save',
                       '--state', str(panel), '--input', 'passthrough']
            with path.open('w') as log:
                child = subprocess.Popen(command, env=env, stdout=log, stderr=log)
                try:
                    await_log(child, path, ' background ' if shell == 'layer' else 'wallpaper window=')
                    time.sleep(.3)
                    change(target, False)
                    await_log(child, path, 'waiting for reconnect')
                    # No tracing, captures or profiler in this waiting sample.
                    before_cpu = cpu_seconds(child.pid)
                    time.sleep(3)
                    waiting_cpu = cpu_seconds(child.pid) - before_cpu
                    begin = time.monotonic()
                    child.terminate()
                    code = child.wait(timeout=3)
                    if code:
                        raise RuntimeError('shutdown while disconnected failed: ' + str(path))
                    validate(path, backend)
                    case = dict(name=f'hotplug-{shell}-{backend}-waiting-shutdown', passed=True,
                                waiting_cpu_seconds=round(waiting_cpu, 6), sample_seconds=3,
                                shutdown_seconds=round(time.monotonic() - begin, 4))
                    results['cases'].append(case)
                    print(json.dumps(case), flush=True)
                finally:
                    stop(child)
                    change(target, True)
