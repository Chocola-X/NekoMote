"""Inner driver: only run via run.sh in the private namespace."""
import os
import subprocess as sp
import sys
import time
from pathlib import Path

mode, backend, info, libx11 = sys.argv[1:]
assert Path.cwd() == Path('/work') and not any(k in os.environ for k in ('DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'))
assert not any(Path(p).exists() for p in ('/run/user', '/run/dbus', '/dev/input', '/dev/dri'))
children = []

def start(args, log, **kw):
    with open(log, 'ab', buffering=0) as f:
        p = sp.Popen(args, stdout=f, stderr=f, **kw)
    children.append(p)
    return p

def wait(p, check):
    for _ in range(150):
        if p.poll() is not None:
            raise RuntimeError(f'{p.args[0]} exited: {p.returncode}')
        if check():
            return
        time.sleep(.05)
    raise TimeoutError(p.args)

def run():
    addr = 'unix:path=/work/runtime/bus'
    os.environ.update(DBUS_SESSION_BUS_ADDRESS=addr, DBUS_SYSTEM_BUS_ADDRESS=addr,
                      NEKOMOTE_TEST_SESSION_BUS=addr, XDG_SESSION_ID='test')
    Path('bus.conf').write_text(f'<busconfig><type>session</type><listen>{addr}</listen><policy context="default"><allow receive_sender="*"/><allow own="*"/><allow send_destination="*"/></policy></busconfig>')
    bus = start(['dbus-daemon', '--config-file=bus.conf', '--nofork'], 'bus.log')
    wait(bus, Path('runtime/bus').is_socket)
    xvfb = start(['Xvfb', ':42', '-screen', '0', '1600x900x24', '-nolisten', 'tcp', '-noreset'], 'x.log')
    wait(xvfb, Path('/tmp/.X11-unix/X42').is_socket)
    os.environ['DISPLAY'] = ':42'
    with open('renderer.txt', 'w') as f:
        sp.run(['glxinfo', '-B'], stdout=f, check=True)
    if backend == 'niri':
        Path('niri.kdl').write_text('hotkey-overlay { skip-at-startup; }\nxwayland-satellite { off; }\n')
        comp = start(['niri', '-c', '/work/niri.kdl'], 'comp.log', env=dict(os.environ, LD_LIBRARY_PATH='/run/current-system/sw/share/nix-ld/lib'))
    else:
        platform = ['--virtual'] if backend == 'kwin-virtual' else ['--x11-display', ':42']
        comp = start(['kwin_wayland', *platform, '--output-count', '2', '--width', '640', '--height', '480', '--no-lockscreen', '--no-global-shortcuts', '--no-kactivities'], 'comp.log', env=dict(os.environ, QT_FORCE_STDERR_LOGGING='1', XDG_DATA_DIRS='/run/current-system/sw/share', LD_LIBRARY_PATH='/run/current-system/sw/share/nix-ld/lib'))
    wait(comp, lambda: any(s.is_socket() for s in Path('runtime').glob('wayland-*')))
    os.environ['WAYLAND_DISPLAY'], = [s.name for s in Path('runtime').glob('wayland-*') if s.is_socket()]
    del os.environ['DISPLAY']
    registry = sp.check_output([info], text=True, timeout=5)
    Path('registry.txt').write_text(registry)
    for interface in ['zwlr_layer_shell_v1', 'ext_output_image_capture_source_manager_v1', 'ext_image_copy_capture_manager_v1']:
        print(interface, interface in registry, flush=True)
        if interface not in registry:
            raise RuntimeError(f'missing {interface}; not a desktop-gaze benchmark')
    if mode != 'preflight':
        from measure import experiment
        experiment(comp, backend, libx11)

try:
    run()
finally:
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=2)
            except sp.TimeoutExpired:
                child.kill()
                child.wait()
