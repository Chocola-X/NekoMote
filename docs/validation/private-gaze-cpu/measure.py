"""Single-winit-output fallback. Never use DISPLAY or PIDs supplied by a caller."""
import collections
import csv
import ctypes as C
import hashlib
import os
from pathlib import Path
import re
import subprocess as sp
import sys
import threading
import time

APP = '/build/nekomote_layer_shell'
PROBE = '/build/nekomote_host_session_probe'
CFG = '/work/panel.json'
EVENT = re.compile(rb'(?:ext_image_copy_capture_manager_v1[@#]\d+\.(create_pointer_cursor_session)|ext_image_copy_capture_cursor_session_v1[@#]\d+\.(position|enter|leave|destroy))\(')


def application(mode, trace=False):
    env = os.environ.copy()
    if trace:
        env['WAYLAND_DEBUG'] = 'client'
    return sp.Popen([APP, '--backend', 'opengl', '--output', 'winit',
                     '--global-gaze', 'on' if mode == 'desktop' else 'off',
                     '--input', 'passthrough', '--no-session-pause', '--config', '/work/launch.json', '--state', CFG,
                     '--mute', '--assets', '/work/assets'],
                    stderr=sp.PIPE if trace else None, env=env)


def stop(p):
    p.terminate()
    try:
        p.wait(2)
    except sp.TimeoutExpired:
        p.kill()
        p.wait()


def monitor(p):
    counts = collections.Counter()
    def drain():
        for line in p.stderr:
            event = EVENT.search(line)
            if event:
                counts[(event[1] or event[2]).decode()] += 1
            elif not line.startswith(b'[') and b'error' in line.lower():
                print(line.decode(errors='replace').strip(), flush=True)
            elif b'wl_surface' in line and b'.commit()' in line:
                counts['commit'] += 1
    threading.Thread(target=drain, daemon=True).start()
    return counts


def ticks(p):
    if p.poll() is not None:
        raise RuntimeError(f'{p.args[0]} exited with {p.returncode}')
    stat = Path(f'/proc/{p.pid}/stat').read_text().rsplit(')', 1)[1].split()
    return int(stat[11]) + int(stat[12])


class Motion:
    def __init__(self, lib):
        self.x = C.CDLL(lib)
        self.x.XOpenDisplay.restype = C.c_void_p
        self.d = C.c_void_p(self.x.XOpenDisplay(b':42'))
        assert self.d
        self.x.XDefaultRootWindow.restype = C.c_ulong
        self.root = C.c_ulong(self.x.XDefaultRootWindow(self.d))

    def move(self, i):
        # Along the same horizontal triangle entirely inside the private winit window.
        x = 200 + 8 * abs(i % 120 - 60)
        self.x.XWarpPointer(self.d, C.c_ulong(0), self.root, 0, 0, 0, 0, x, 300)
        self.x.XFlush(self.d)

    def phase(self, moving, seconds):
        start = time.monotonic()
        for i in range(60 * seconds):
            if moving:
                self.move(i)
            time.sleep(max(0, start + (i+1)/60 - time.monotonic()))


def experiment(comp, backend, lib):
    assert backend == 'niri' and Path('registry.txt').read_text().count("interface: 'wl_output'") == 1
    assert "name: 'winit'" in Path('registry.txt').read_text()
    for f in [APP, PROBE, *sorted(Path('assets').rglob('*'))]:
        if Path(f).is_file():
            print(hashlib.sha256(Path(f).read_bytes()).hexdigest(), f, flush=True)
    Path(CFG).write_text('{"version":1,"fps":30}')
    motion = Motion(lib)
    motion.move(0)
    smoke = sys.argv[1] == 'smoke'
    # No real login1 or activation directories exist. No-server case first.
    if smoke:
        app = application('desktop', True)
        counts = monitor(app)
        try:
            time.sleep(3)
            ticks(app)
            assert counts['commit'] > 1 and not counts['create_pointer_cursor_session'], counts
            print('no-service:', dict(counts), flush=True)
        finally:
            stop(app)
    probe = sp.Popen([PROBE, '--serve'])
    try:
        for _ in range(50):
            if sp.run(['dbus-send', '--session', '--print-reply', '--dest=org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus.NameHasOwner', 'string:org.freedesktop.login1'], capture_output=True).stdout.endswith(b'boolean true\n'):
                break
            time.sleep(.05)
        else:
            raise RuntimeError('fake login1 not ready')
        if smoke:
            check = lambda action: sp.run([PROBE, '--set', action], check=True, timeout=3)
            check('hint-lock')
            app = application('desktop', True)
            counts = monitor(app)
            try:
                time.sleep(3)
                ticks(app)
                assert counts['commit'] > 1 and not counts['create_pointer_cursor_session'], counts
                print('startup locked:', dict(counts), flush=True)
                check('hint-unlock')
                motion.phase(True, 4)
                assert counts['create_pointer_cursor_session'] and counts['position'] > 1, counts
                print('unlocked:', dict(counts), flush=True)
                destroyed = counts['destroy']
                check('hint-lock')
                time.sleep(3)
                ticks(app)
                assert counts['destroy'] > destroyed, counts
                print('relocked:', dict(counts), flush=True)
            finally:
                stop(app)
        else:
            # Separate, untimed protocol witness. Never time WAYLAND_DEBUG output.
            witness = application('desktop', True)
            counts = monitor(witness)
            try:
                motion.phase(True, 4)
                ticks(witness)
                assert counts['create_pointer_cursor_session'] and counts['position'] > 1 and counts['commit'] > 1, counts
                print('sample witness:', dict(counts), flush=True)
            finally:
                stop(witness)
            hz = os.sysconf('SC_CLK_TCK')
            with open('cpu.csv', 'w', buffering=1) as f:
                writer = csv.writer(f, lineterminator='\n')
                writer.writerow(['pair','mode','phase','seconds','hz','wp_ticks','comp_ticks','wp_cpu_pct','wp_total_pct','comp_cpu_pct'])
                for r in range(4):
                    order = ['local', 'desktop'][::1 if r%2 == 0 else -1]
                    for gaze in order:
                        app = application(gaze)
                        try:
                            for moving in ([False, True] if r%2 == 0 else [True, False]):
                                motion.move(0)
                                motion.phase(moving, 3)
                                a, b = ticks(app), ticks(comp)
                                t = time.monotonic()
                                motion.phase(moving, 6)
                                elapsed = time.monotonic() - t
                                da, db = ticks(app)-a, ticks(comp)-b
                                cpu = 100*da/(hz*elapsed)
                                writer.writerow([r+1,gaze,'motion' if moving else 'idle', elapsed,hz,da,db,cpu,cpu,100*db/(hz*elapsed)])
                        finally:
                            stop(app)
    finally:
        stop(probe)
