#!/usr/bin/env python3
"""Exercise playback and recovery against an owned PulseAudio null sink only.

Requires a PulseAudio executable and its modules. No user services are modified.
Example: tools/test_audio_recovery.py --pulseaudio /usr/bin/pulseaudio \
    --modules /usr/lib/pulse-17.0/modules --tests build/release/bin/nekomote_audio_device_tests
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pulseaudio', required=True, type=Path)
    parser.add_argument('--modules', required=True, type=Path)
    parser.add_argument('--tests', required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='nekomote-pulse-') as temporary:
        root = Path(temporary)
        address = 'unix:' + str(root / 'native')
        env = dict(os.environ, PULSE_SERVER=address,
                   NEKOMOTE_TEST_PULSE_SERVER=address,
                   NEKOMOTE_TEST_PULSE_CONTROL=temporary,
                   PULSE_RUNTIME_PATH=temporary, PULSE_STATE_PATH=temporary,
                   DBUS_SESSION_BUS_ADDRESS='unix:path=' + str(root / 'no-session-bus'))
        daemon = None
        test = None
        with (root / 'server.log').open('w+') as log:
            def stop():
                nonlocal daemon
                if daemon is not None:
                    daemon.terminate()
                    daemon.wait(timeout=5)
                    daemon = None

            def start():
                nonlocal daemon
                daemon = subprocess.Popen([
                    str(args.pulseaudio.resolve()), '-n', '--daemonize=no',
                    '--exit-idle-time=-1', '--use-pid-file=no',
                    '--disallow-exit', '--disable-shm',
                    '--dl-search-path=' + str(args.modules.resolve()),
                    '-L', 'module-native-protocol-unix socket=' + str(root / 'native') +
                    ' auth-anonymous=1',
                    '-L', 'module-null-sink sink_name=nekomote_test rate=8000 channels=1',
                ], env=env, stdout=log, stderr=log)
                deadline = time.monotonic() + 5
                while not (root / 'native').exists():
                    if daemon.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('private PulseAudio did not start')
                    time.sleep(.01)

            try:
                # Existing playback, pause-clock, natural-end and ready/paused teardown coverage.
                start()
                subprocess.run([str(args.tests.resolve()),
                                '[pulse-integration]~[pulse-recovery]'], env=env, check=True)
                stop()
                test = subprocess.Popen([str(args.tests.resolve()), '[pulse-recovery]'], env=env)
                for action, operation in [('start', start), ('stop', stop),
                                          ('restart', start), ('stop_again', stop)]:
                    deadline = time.monotonic() + 15
                    while not (root / action).exists():
                        if test.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError('missing handshake: ' + action)
                        time.sleep(.01)
                    operation()
                    (root / (action + '.done')).touch()
                if test.wait(timeout=15) != 0:
                    raise RuntimeError('audio recovery assertions failed')
            except Exception:
                log.seek(0)
                print(log.read())
                raise
            finally:
                if test is not None and test.poll() is None:
                    test.terminate()
                    test.wait(timeout=5)
                stop()


if __name__ == '__main__':
    main()
