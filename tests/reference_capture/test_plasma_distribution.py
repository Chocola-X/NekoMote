"""Distribution checks use real synthetic ELF files, without Qt or a desktop."""

import importlib.util
import json
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "plasma_distribution", REPO / "tools/plasma_distribution.py"
)
DIST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DIST)


@unittest.skipUnless(shutil.which("cc") and shutil.which("readelf"), "cc/binutils required")
class PlasmaDistributionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.stage = self.directory / "stage"
        self.source = self.directory / "fixture.c"
        self.source.write_text("int fixture(void) { return 7; }\n")
        for relative in DIST.ELFS:
            output = self.stage / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["cc", "-shared", "-fPIC", str(self.source),
                            "-Wl,-rpath,$ORIGIN/../../../lib", "-o", str(output)],
                           check=True, capture_output=True)
        for relative in set(DIST.REQUIRED) - set(DIST.ELFS):
            output = self.stage / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text("synthetic packaging fixture\n")
        self.manifest()

    def tearDown(self):
        self.temp.cleanup()

    def manifest(self):
        files = {str(path.relative_to(self.stage)): {"sha256": DIST.digest(path)}
                 for path in self.stage.rglob("*")
                 if path.is_file() and path.name != "manifest.json"}
        (self.stage / "manifest.json").write_text(json.dumps({"files": files}))

    def test_real_elf_baseline_and_relative_paths(self):
        report = DIST.audit(self.stage, "6.4.2", True)
        self.assertTrue(report["baseline"]["passed"])
        self.assertEqual(len(report["binaries"]), 2)

    def test_higher_qt_rejected_by_baseline(self):
        with self.assertRaisesRegex(ValueError, "Qt 6.9.1 > 6.4.2"):
            DIST.audit(self.stage, "6.9.1", True)

    def test_corrupt_binary_rejected(self):
        with (self.stage / DIST.ELFS[0]).open("ab") as output:
            output.write(b"changed")
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            DIST.audit(self.stage, "6.4.2")

    def test_unrecorded_file_rejected(self):
        (self.stage / "unexpected.txt").write_text("not in manifest")
        with self.assertRaisesRegex(ValueError, "exactly"):
            DIST.audit(self.stage, "6.4.2")

    def test_missing_qml_package_rejected_after_valid_manifest(self):
        (self.stage / DIST.REQUIRED[-1]).unlink()
        self.manifest()
        with self.assertRaisesRegex(ValueError, "missing required"):
            DIST.audit(self.stage, "6.4.2")

    def test_bundled_qt_rejected_after_valid_manifest(self):
        shutil.copyfile(self.stage / DIST.ELFS[0], self.stage / "lib/libQt6Core.so.6")
        self.manifest()
        with self.assertRaisesRegex(ValueError, "unexpected bundled ELF"):
            DIST.audit(self.stage, "6.4.2")

    def test_absolute_rpath_rejected_after_valid_manifest(self):
        subprocess.run(["cc", "-shared", "-fPIC", str(self.source),
                        "-Wl,-rpath,/tmp/build/lib", "-o", str(self.stage / DIST.ELFS[0])],
                       check=True, capture_output=True)
        self.manifest()
        with self.assertRaisesRegex(ValueError, "non-relative RPATH"):
            DIST.audit(self.stage, "6.4.2")

    def test_reproducible_archive_contains_preflight_and_manifest(self):
        first = self.directory / "first.tar.gz"
        second = self.directory / "second.tar.gz"
        DIST.package(self.stage, "6.4.2", first, True)
        DIST.package(self.stage, "6.4.2", second, True)
        self.assertEqual(DIST.digest(first), DIST.digest(second))
        with tarfile.open(first) as archive:
            names = archive.getnames()
            self.assertIn("nekomote-plasma/stage/compatibility.json", names)
            self.assertIn("nekomote-plasma/tools/plasma_distribution.py", names)
            self.assertIn("nekomote-plasma/tools/plasma_deploy.py", names)


if __name__ == "__main__":
    unittest.main()
