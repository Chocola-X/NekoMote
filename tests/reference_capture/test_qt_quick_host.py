#!/usr/bin/env python3
"""G14 Qt Quick host automatic verification entry points.

Tests explicitly opt in through NEKOMOTE_QT_QUICK_HOST. An existing build
does not enable GPU tests during ordinary unittest discovery. Captures require
real assets, Pillow and a display, and hold the shared GPU validation lock.
"""
import fcntl
import os
import pathlib
import subprocess
import tempfile
import unittest
from unittest import mock

REPO = pathlib.Path(__file__).resolve().parents[2]


def configured_host(environment):
    value = environment.get('NEKOMOTE_QT_QUICK_HOST', '').strip()
    return pathlib.Path(value) if value else None


HOST = configured_host(os.environ)
HOST_ENABLED = HOST is not None and HOST.is_file()
MODEL = pathlib.Path(os.environ.get(
    'NEKOMOTE_MODEL', REPO / 'assets/characters/chocola/date-a/model.nkm.json'))
SESSION = pathlib.Path(os.environ.get(
    'NEKOMOTE_SESSION', REPO / 'assets/characters/chocola/date-a/session.nks.json'))
FRAISE_MODEL = REPO / 'assets/characters/fraise/france-maid-b/model.nkm.json'
FRAISE_SESSION = REPO / 'assets/characters/fraise/france-maid-b/session.nks.json'

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:
    HAVE_PIL = False


def run_host(args, timeout=30, *, gpu=False, env=None):
    if HOST is None:
        raise RuntimeError('NEKOMOTE_QT_QUICK_HOST must be explicitly set')
    command = [str(HOST)] + [str(a) for a in args]
    if not gpu:
        return subprocess.run(command, capture_output=True, timeout=timeout,
                              text=True, env=env)
    lock_path = REPO / 'build/gpu-validation.lock'
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    with lock_path.open('a') as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        return subprocess.run(command, capture_output=True, timeout=timeout,
                              text=True, env=env)


def have_assets():
    return MODEL.is_file() and SESSION.is_file()


class ExplicitOptIn(unittest.TestCase):
    def test_existing_binary_does_not_enable_tests_without_configuration(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = pathlib.Path(tmp) / 'nekomote_qt_quick_host'
            binary.touch()
            self.assertTrue(binary.is_file())
            for environment in ({}, {'NEKOMOTE_QT_QUICK_HOST': ''},
                                {'NEKOMOTE_QT_QUICK_HOST': '  '}):
                with self.subTest(environment=environment):
                    host = configured_host(environment)
                    self.assertIsNone(host)
                    with mock.patch(__name__ + '.HOST', host), \
                            mock.patch(__name__ + '.subprocess.run') as launch:
                        with self.assertRaisesRegex(RuntimeError, 'explicitly set'):
                            run_host(['--help'])
                        launch.assert_not_called()

    def test_explicit_binary_is_selected(self):
        self.assertEqual(configured_host({'NEKOMOTE_QT_QUICK_HOST': '/chosen/host'}),
                         pathlib.Path('/chosen/host'))


@unittest.skipUnless(HOST_ENABLED,
                     'Set NEKOMOTE_QT_QUICK_HOST to an existing binary to opt in')
class CliErrors(unittest.TestCase):
    def test_unsupported_graphics_api(self):
        r = run_host(['--model', MODEL, '--session', SESSION,
                      '--graphics-api', 'vulkan', '--steps', '1',
                      '--capture', '/tmp/_never.png'])
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('only the OpenGL Qt Quick backend is supported', r.stderr)

    def test_width_zero(self):
        r = run_host(['--model', MODEL, '--session', SESSION,
                      '--width', '0', '--steps', '1',
                      '--capture', '/tmp/_never.png'])
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('integer option outside its accepted range: 0', r.stderr)

    def test_missing_model(self):
        with tempfile.TemporaryDirectory() as tmp:
            r = run_host(['--model', pathlib.Path(tmp) / 'missing-model.json',
                          '--session', SESSION, '--steps', '1',
                          '--capture', pathlib.Path(tmp) / 'never.png'])
            self.assertNotEqual(r.returncode, 0)
            self.assertIn('load model:', r.stderr)

    def test_existing_output_rejected(self):
        with tempfile.NamedTemporaryFile(suffix='.png', delete=False) as f:
            existing = pathlib.Path(f.name)
            original = b'preserve existing output\x00\xff'
            f.write(original)
        try:
            r = run_host(['--model', MODEL, '--session', SESSION,
                          '--steps', '1', '--capture', existing])
            self.assertNotEqual(r.returncode, 0)
            self.assertIn('diagnostic output already exists; choose a new path', r.stderr)
            self.assertEqual(existing.read_bytes(), original)
        finally:
            existing.unlink(missing_ok=True)

    def test_help(self):
        r = run_host(['--help'])
        self.assertEqual(r.returncode, 0)
        self.assertIn('model', r.stdout)

    def test_missing_required_option(self):
        r = run_host(['--session', SESSION, '--steps', '1',
                      '--capture', '/tmp/_never.png'])
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('model/session and positive scale are required', r.stderr)


@unittest.skipUnless(HOST_ENABLED,
                     'Set NEKOMOTE_QT_QUICK_HOST to an existing binary to opt in')
@unittest.skipUnless(have_assets(), 'Chocola model/session assets not found')
@unittest.skipUnless(HAVE_PIL, 'Pillow not available')
class LifecycleCapture(unittest.TestCase):
    def test_lifecycle_30_steps(self):
        with tempfile.TemporaryDirectory() as tmp:
            lifecycle = pathlib.Path(tmp) / 'lifecycle'
            env = dict(os.environ, QSG_RENDER_LOOP='threaded', QT_SCALE_FACTOR='1')
            r = run_host(
                ['--model', str(MODEL), '--session', str(SESSION),
                 '--steps', '30', '--check-lifecycle', str(lifecycle)],
                timeout=60, gpu=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            initial = Image.open(lifecycle / 'initial.png')
            resized = Image.open(lifecycle / 'resized.png')
            recreated = Image.open(lifecycle / 'recreated.png')
            self.assertEqual(initial.size, (800, 1080))
            self.assertEqual(resized.size, (640, 480))
            self.assertEqual(recreated.size, (800, 1080))
            self.assertEqual(
                initial.tobytes(), recreated.tobytes(),
                'recreated frame differs from initial')
            self.assertIn('created=2', r.stdout)
            self.assertIn('destroyed=2', r.stdout)

    def test_capture_chocola(self):
        with tempfile.TemporaryDirectory() as tmp:
            capture = pathlib.Path(tmp) / 'chocola.png'
            env = dict(os.environ, QSG_RENDER_LOOP='threaded', QT_SCALE_FACTOR='1')
            r = run_host(
                ['--model', str(MODEL), '--session', str(SESSION),
                 '--steps', '0', '--capture', str(capture)],
                gpu=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            img = Image.open(capture)
            self.assertEqual(img.size, (800, 1080))

    def test_capture_fraise_moved_scaled(self):
        if not FRAISE_MODEL.is_file() or not FRAISE_SESSION.is_file():
            self.skipTest('Fraise model/session assets not found')
        with tempfile.TemporaryDirectory() as tmp:
            capture = pathlib.Path(tmp) / 'fraise.png'
            env = dict(os.environ, QSG_RENDER_LOOP='threaded', QT_SCALE_FACTOR='1')
            r = run_host(
                ['--model', str(FRAISE_MODEL), '--session', str(FRAISE_SESSION),
                 '--origin-x', '50', '--origin-y', '300', '--scale', '0.8',
                 '--steps', '0', '--capture', str(capture)],
                gpu=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            img = Image.open(capture)
            self.assertEqual(img.size, (800, 1080))

    def test_dpi_1_5(self):
        with tempfile.TemporaryDirectory() as tmp:
            capture = pathlib.Path(tmp) / 'chocola_dpi.png'
            env = dict(os.environ, QSG_RENDER_LOOP='threaded', QT_SCALE_FACTOR='1.5')
            r = run_host(
                ['--model', str(MODEL), '--session', str(SESSION),
                 '--width', '800', '--height', '1080',
                 '--steps', '0', '--capture', str(capture)],
                gpu=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            img = Image.open(capture)
            self.assertEqual(img.size, (1200, 1620))


if __name__ == '__main__':
    unittest.main()
