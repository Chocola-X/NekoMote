#!/usr/bin/env python3
"""G22 Plasma package probe integration tests.

Runs the actual KPackage probe in capture, lifecycle, recovery, bad-asset and
invalid-config modes. Tests requiring real assets are SKIPPED when
NEKOMOTE_TEST_MODEL/SESSION are unset. DPI 1 and 1.5 are both exercised.
CPU resource and peak RSS are reported separately from renderer counts.
"""
import hashlib
import json
import os
import resource
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
PROBE = REPO / "build/plasma/bin/nekomote_plasma_package_probe"
MODEL = os.environ.get("NEKOMOTE_TEST_MODEL", "")
SESSION = os.environ.get("NEKOMOTE_TEST_SESSION", "")
LOCK = REPO / "build/gpu-validation.lock"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def run_probe(args, timeout=120):
    if not PROBE.is_file():
        raise unittest.SkipTest("plasma package probe not built")
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = str(REPO / "build/plasma/lib") + ":" + env.get("LD_LIBRARY_PATH", "")
    start_rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    start = time.monotonic()
    result = subprocess.run(
        [str(PROBE)] + args, capture_output=True, text=True, env=env, timeout=timeout
    )
    elapsed = time.monotonic() - start
    peak_rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss - start_rss
    return result, elapsed, peak_rss


def new_output():
    base = REPO / "experiments"
    base.mkdir(exist_ok=True)
    d = base / f"g22-test-{os.getpid()}-{int(time.monotonic()*1e6)}"
    return d


class PackageProbeTests(unittest.TestCase):
    def tearDown(self):
        out = getattr(self, "_output", None)
        if out and out.exists():
            shutil.rmtree(out)

    def _require_assets(self):
        if not MODEL or not SESSION or not Path(MODEL).is_file() or not Path(SESSION).is_file():
            raise unittest.SkipTest("set NEKOMOTE_TEST_MODEL/SESSION for asset-dependent tests")

    def test_bad_asset(self):
        self._output = new_output()
        result, elapsed, peak_rss = run_probe(
            ["--bad-asset", "--output", str(self._output)])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("error test", result.stdout)
        self.assertTrue((self._output / "error_recovery.png").is_file())
        print(f"\nbad-asset: elapsed={elapsed:.2f}s peak_rss={peak_rss}KB")

    def test_invalid_config(self):
        self._output = new_output()
        result, elapsed, peak_rss = run_probe(
            ["--invalid-config", "--output", str(self._output)])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("error test", result.stdout)
        print(f"\ninvalid-config: elapsed={elapsed:.2f}s peak_rss={peak_rss}KB")

    def test_capture(self):
        self._require_assets()
        self._output = new_output()
        result, elapsed, peak_rss = run_probe(
            ["--model", MODEL, "--session", SESSION, "--output", str(self._output)])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("capture: screenshot saved", result.stdout)
        png = self._output / "package.png"
        self.assertTrue(png.is_file())
        print(f"\ncapture: elapsed={elapsed:.2f}s peak_rss={peak_rss}KB png_sha256={sha256_file(png)[:16]}")

    def test_lifecycle(self):
        self._require_assets()
        self._output = new_output()
        result, elapsed, peak_rss = run_probe(
            ["--lifecycle", "--model", MODEL, "--session", SESSION,
             "--output", str(self._output), "--rounds", "5", "--config-rounds", "5"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("lifecycle: configured checks passed", result.stdout)
        self.assertIn("teardown: renderer created=", result.stdout)
        for line in result.stdout.splitlines():
            if "teardown: renderer created=" in line:
                parts = line.split("created=")[1].split(" destroyed=")
                self.assertEqual(int(parts[0]), int(parts[1]),
                                 "renderer created != destroyed")
        self.assertTrue((self._output / "lifecycle_final.png").is_file())
        print(f"\nlifecycle: elapsed={elapsed:.2f}s peak_rss={peak_rss}KB")

    def test_recovery(self):
        self._require_assets()
        self._output = new_output()
        result, elapsed, peak_rss = run_probe(
            ["--recovery", "--model", MODEL, "--session", SESSION,
             "--output", str(self._output)])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("recovery: all bad->good->bad->good cycles passed", result.stdout)
        self.assertTrue((self._output / "recovery_final.png").is_file())
        print(f"\nrecovery: elapsed={elapsed:.2f}s peak_rss={peak_rss}KB")

    def test_lifecycle_dpi_1_5(self):
        self._require_assets()
        self._output = new_output()
        result, elapsed, peak_rss = run_probe(
            ["--lifecycle", "--model", MODEL, "--session", SESSION,
             "--output", str(self._output), "--dpi", "1.5",
             "--rounds", "3", "--config-rounds", "3"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("lifecycle: configured checks passed", result.stdout)
        print(f"\nlifecycle-dpi1.5: elapsed={elapsed:.2f}s peak_rss={peak_rss}KB")


if __name__ == "__main__":
    unittest.main(verbosity=2)
