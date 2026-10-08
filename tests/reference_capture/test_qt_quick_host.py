#!/usr/bin/env python3
"""G14 Qt Quick host automatic verification entry points.

Tests frozen CLI errors, lifecycle capture, and DPI scaling against the
isolated Qt Quick diagnostic host. Requires GPU and display; skipped when
the Qt binary or real assets are unavailable.
"""
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
HOST = pathlib.Path(os.environ.get(
    'NEKOMOTE_QT_QUICK_HOST', REPO / 'build/qt-quick/bin/nekomote_qt_quick_host'))
MODEL = pathlib.Path(os.environ.get(
    'NEKOMOTE_MODEL', REPO / 'assets/characters/chocola-date-a/model.nkm.json'))
SESSION = pathlib.Path(os.environ.get(
    'NEKOMOTE_SESSION', REPO / 'assets/characters/chocola-date-a/session.nks.json'))
FRAISE_MODEL = REPO / 'assets/characters/fraise-france-maid-b/model.nkm.json'
FRAISE_SESSION = REPO / 'assets/characters/fraise-france-maid-b/session.nks.json'

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:
    HAVE_PIL = False


def run_host(args, timeout=30):
    return subprocess.run(
        [str(HOST)] + [str(a) for a in args],
        capture_output=True, timeout=timeout, text=True)


def have_assets():
    return MODEL.exists() and SESSION.exists()


@unittest.skipUnless(HOST.exists(), 'Qt Quick host binary not found')
class CliErrors(unittest.TestCase):
    def test_unsupported_graphics_api(self):
        r = run_host(['--model', MODEL, '--session', SESSION,
                      '--graphics-api', 'vulkan', '--steps', '1',
                      '--capture', '/tmp/_never.png'])
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('OpenGL', r.stderr)

    def test_width_zero(self):
        r = run_host(['--model', MODEL, '--session', SESSION,
                      '--width', '0', '--steps', '1',
                      '--capture', '/tmp/_never.png'])
        self.assertNotEqual(r.returncode, 0)

    def test_missing_model(self):
        r = run_host(['--model', '/nonexistent/model.json',
                      '--session', SESSION, '--steps', '1',
                      '--capture', '/tmp/_never.png'])
        self.assertNotEqual(r.returncode, 0)

    def test_existing_output_rejected(self):
        with tempfile.NamedTemporaryFile(suffix='.png', delete=False) as f:
            existing = pathlib.Path(f.name)
        try:
            r = run_host(['--model', MODEL, '--session', SESSION,
                          '--steps', '1', '--capture', existing])
            self.assertNotEqual(r.returncode, 0)
            self.assertIn('already exists', r.stderr)
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


@unittest.skipUnless(HOST.exists() and have_assets(), 'Qt binary or assets not found')
@unittest.skipUnless(HAVE_PIL, 'Pillow not available')
class LifecycleCapture(unittest.TestCase):
    def test_lifecycle_30_steps(self):
        with tempfile.TemporaryDirectory() as tmp:
            lifecycle = pathlib.Path(tmp) / 'lifecycle'
            env = dict(os.environ, QSG_RENDER_LOOP='threaded', QT_SCALE_FACTOR='1')
            r = subprocess.run(
                [str(HOST), '--model', str(MODEL), '--session', str(SESSION),
                 '--steps', '30', '--check-lifecycle', str(lifecycle)],
                capture_output=True, timeout=60, text=True, env=env)
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
            r = subprocess.run(
                [str(HOST), '--model', str(MODEL), '--session', str(SESSION),
                 '--steps', '0', '--capture', str(capture)],
                capture_output=True, timeout=30, text=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            img = Image.open(capture)
            self.assertEqual(img.size, (800, 1080))

    def test_capture_fraise_moved_scaled(self):
        if not FRAISE_MODEL.exists():
            self.skipTest('Fraise model not found')
        with tempfile.TemporaryDirectory() as tmp:
            capture = pathlib.Path(tmp) / 'fraise.png'
            env = dict(os.environ, QSG_RENDER_LOOP='threaded', QT_SCALE_FACTOR='1')
            r = subprocess.run(
                [str(HOST), '--model', str(FRAISE_MODEL), '--session', str(FRAISE_SESSION),
                 '--origin-x', '50', '--origin-y', '300', '--scale', '0.8',
                 '--steps', '0', '--capture', str(capture)],
                capture_output=True, timeout=30, text=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            img = Image.open(capture)
            self.assertEqual(img.size, (800, 1080))

    def test_dpi_1_5(self):
        with tempfile.TemporaryDirectory() as tmp:
            capture = pathlib.Path(tmp) / 'chocola_dpi.png'
            env = dict(os.environ, QSG_RENDER_LOOP='threaded', QT_SCALE_FACTOR='1.5')
            r = subprocess.run(
                [str(HOST), '--model', str(MODEL), '--session', str(SESSION),
                 '--width', '800', '--height', '1080',
                 '--steps', '0', '--capture', str(capture)],
                capture_output=True, timeout=30, text=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            img = Image.open(capture)
            self.assertEqual(img.size, (1200, 1620))


if __name__ == '__main__':
    unittest.main()
