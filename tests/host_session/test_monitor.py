#!/usr/bin/env python3
"""Session-observer contracts on an owned private D-Bus; no real session changes."""
import argparse
import contextlib
import os
from pathlib import Path
import select
import subprocess
import time


@contextlib.contextmanager
def process(command, **kwargs):
    child = subprocess.Popen(command, **kwargs)
    try:
        yield child
    finally:
        if child.poll() is None:
            child.terminate()
            child.wait(timeout=5)


def line(child, timeout=6):
    if not select.select([child.stdout], [], [], timeout)[0]:
        raise RuntimeError('timed out waiting for probe output')
    value = child.stdout.readline().strip()
    if not value:
        raise RuntimeError('probe exited unexpectedly')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', required=True, type=Path)
    args = parser.parse_args()
    probe = str(args.probe.resolve())
    with process(['dbus-daemon', '--session', '--nofork', '--print-address=1'],
                 stdout=subprocess.PIPE, text=True) as bus:
        address = line(bus)
        env = dict(os.environ, DBUS_SESSION_BUS_ADDRESS=address,
                   DBUS_SYSTEM_BUS_ADDRESS=address, NEKOMOTE_TEST_SESSION_BUS=address,
                   XDG_SESSION_ID='test')

        def set_state(action):
            subprocess.run([probe, '--set', action], env=env, check=True, timeout=5)

        with process([probe, '--serve'], env=env) as service:
            # Wait for the private service names without a fixed startup delay.
            for _ in range(100):
                result = subprocess.run([probe, '--set', 'noop'], env=env,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if result.returncode == 0:
                    break
                time.sleep(.02)
            else:
                raise RuntimeError('test service did not acquire its private name')
            set_state('lock')
            with process([probe], env=env, stdout=subprocess.PIPE, text=True, bufsize=1) as monitor:
                def await_state(expected):
                    deadline = time.monotonic() + 6
                    while time.monotonic() < deadline:
                        if line(monitor, max(.01, deadline - time.monotonic())) == str(expected):
                            return
                    raise RuntimeError('wrong availability state')

                await_state(1)  # Initial GetActive, without a new lock signal.
                set_state('unlock')
                await_state(0)
                set_state('hint-lock')
                await_state(1)
                set_state('sleep')
                set_state('hint-unlock')
                # Unlock must not override PrepareForSleep.
                time.sleep(2.2)
                if select.select([monitor.stdout], [], [], 0)[0]:
                    raise RuntimeError('sleep state was cleared by unlocking')
                set_state('wake')
                await_state(0)
                set_state('lock')
                await_state(1)
                service.terminate()
                service.wait(timeout=5)
                await_state(0)  # Owner loss must not leave a permanent cached lock.
                with process([probe, '--serve'], env=env):
                    time.sleep(.2)
                    set_state('lock')
                    await_state(1)
                    set_state('unlock')
                    await_state(0)
        print('PASS: initial state, lock/unlock, logind hint, sleep merge, owner loss and restart')


if __name__ == '__main__':
    main()
