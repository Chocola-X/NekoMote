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

        @contextlib.contextmanager
        def sampled(environment=env, before_lookup=None):
            with process([probe, '--sample'], env=environment, stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, text=True, bufsize=1) as child:
                assert line(child) == '00', 'construction must not grant permission'
                if before_lookup:
                    before_lookup()
                child.stdin.write('!')
                child.stdin.flush()

                def expect(state, stable=False):
                    deadline = time.monotonic() + (.18 if stable else 4)
                    while True:
                        child.stdin.write('?')
                        child.stdin.flush()
                        actual = line(child)
                        if stable and actual != state:
                            raise AssertionError(f'expected stable {state}, received {actual}')
                        if not stable and actual == state:
                            return
                        if time.monotonic() >= deadline:
                            if stable:
                                return
                            raise AssertionError(f'expected {state}, last seen {actual}')
                        time.sleep(.01)
                yield expect

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
            # Samples are (legacy pause, metadata allowed). Only bus fd readiness
            # dispatches messages; requesting a sample merely reads cached state.
            # AddMatch completed; Lock is queued *before* the owner lookup, with
            # no LockedHint=true to rescue an observer that drops the request.
            set_state('lock')
            with sampled(before_lookup=lambda: set_state('request-lock')) as expect:
                expect('10')
                set_state('unlock')  # ScreenSaver only; logind veto must survive.
                expect('00')
                expect('00', stable=True)
                set_state('request-unlock')
                expect('01')
            set_state('hint-lock')
            with sampled() as expect:
                expect('10')
                set_state('hint-unlock')
                expect('01')
                set_state('unrelated')  # Requerying LockedHint here would hang the query.
                expect('01', stable=True)
                set_state('hint-pending')
                expect('00')
                set_state('hint-unlock')
                expect('01')
                set_state('request-lock')
                expect('00')
                expect('00', stable=True)  # Post-request false alone cannot acknowledge Lock.
                set_state('hint-lock')
                expect('10')
                set_state('hint-unlock')  # Locker never sent Unlock.
                expect('01')
                set_state('hint-error')
                expect('00')
                set_state('hint-unlock')
                expect('01')
                set_state('inactive')
                expect('00')
                set_state('active')
                expect('01')
                set_state('sleep')
                expect('10')
                set_state('spoof')  # Unmatched unicast still reaches the private connection.
                expect('10', stable=True)
                set_state('unrelated')  # Hold next LockedHint reply over resume.
                set_state('wake')
                expect('00')
                expect('00', stable=True)
                set_state('hint-unlock')
                expect('01')
                set_state('lock')
                expect('10')
                set_state('unlock')
                expect('01')
            for session_id in (None, 'stale', 'auto', 'bogus'):
                environment = env.copy()
                if session_id is None:
                    del environment['XDG_SESSION_ID']
                else:
                    environment['XDG_SESSION_ID'] = session_id
                with sampled(environment) as expect:
                    expect('00', stable=True)
            set_state('saver-canonical-path')
            with sampled() as expect:
                expect('01')
            set_state('saver-error')
            with sampled() as expect:
                set_state('unlock')
                expect('00', stable=True)
            set_state('saver-unsupported')
            with sampled() as expect:
                expect('01')
                set_state('lock')
                expect('10')
                set_state('unlock')
                expect('01')
                set_state('drop-login')
                expect('00')
            service.terminate()
            service.wait(timeout=5)

        with process([probe, '--serve'], env=env) as service:
            for _ in range(100):
                if subprocess.run([probe, '--set', 'lock'], env=env, capture_output=True).returncode == 0:
                    break
                time.sleep(.02)
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
                    set_state('direct-pid')
                    with sampled(dict(env, XDG_SESSION_ID='stale')) as expect:
                        expect('01')  # PID binding takes priority over any environment ID.
                        set_state('lock')
                        expect('10')
                        set_state('drop-screen')
                        expect('01')  # Bus daemon confirmed that no ScreenSaver exists.
                        bus.terminate()
                        bus.wait(timeout=5)
                        expect('00')  # Disconnected, not equivalent to NameHasNoOwner.
        print('PASS: private-bus playback, fd dispatch, fail-closed gaze and session binding')


if __name__ == '__main__':
    main()
