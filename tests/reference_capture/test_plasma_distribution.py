"""Distribution checks use real synthetic ELF files, without Qt or a desktop."""

import importlib.util
import json
import os
import shlex
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

COMPILER = shlex.split(os.environ.get("CC", "cc"))
MISSING_TOOLS = []
if not COMPILER or not shutil.which(COMPILER[0]):
    MISSING_TOOLS.append(f"C compiler from CC={os.environ.get('CC', 'cc')!r}")
if not shutil.which("readelf"):
    MISSING_TOOLS.append("readelf (binutils)")


@unittest.skipIf(MISSING_TOOLS, "required tools unavailable: " + ", ".join(MISSING_TOOLS))
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
            subprocess.run([*COMPILER, "-shared", "-fPIC", str(self.source),
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

    def test_plasma5_baseline_requires_its_package_metadata(self):
        (self.stage / "qml/NekoMote/Wallpaper/nekomote_plasma_qml.qmltypes").unlink()
        desktop = self.stage / "share/plasma/wallpapers/uk.nekopara.nekomote/metadata.desktop"
        self.manifest()
        with self.assertRaisesRegex(ValueError, "missing required"):
            DIST.audit(self.stage, "5.12.8", ubuntu20=True)
        desktop.write_text("[Desktop Entry]\n")
        self.manifest()
        report = DIST.audit(self.stage, "5.12.8", ubuntu20=True)
        self.assertEqual(report["plasma_major"], 5)
        self.assertTrue(report["baseline"]["passed"])

    def test_baseline_cannot_be_used_for_the_other_qt_major(self):
        with self.assertRaisesRegex(ValueError, "baseline must match"):
            DIST.audit(self.stage, "5.12.8", True)
        with self.assertRaisesRegex(ValueError, "baseline must match"):
            DIST.audit(self.stage, "6.4.2", ubuntu20=True)

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
        subprocess.run([*COMPILER, "-shared", "-fPIC", str(self.source),
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
            self.assertFalse(any(Path(name).name.lower() == "readme.md" for name in names))
            self.assertIn("nekomote-plasma/stage/compatibility.json", names)
            self.assertIn("nekomote-plasma/tools/plasma_distribution.py", names)
            self.assertIn("nekomote-plasma/tools/plasma_deploy.py", names)

    def private_export_fixture(self, symbol, major=5):
        provider = self.directory / "provider.c"
        provider.write_text(f'void provider(void) __asm__("{symbol}");\nvoid provider(void) {{}}\n')
        script = self.directory / "symbols.map"
        script.write_text(f'Qt_{major}_PRIVATE_API {{ global: {symbol}; local: *; }};\n')
        soname = f"libQt{major}Quick.so.{major}"
        subprocess.run([*COMPILER, "-shared", "-fPIC", str(provider), f"-Wl,-soname,{soname}",
                        f"-Wl,--version-script={script}", "-o", str(self.directory / soname)],
                       check=True, capture_output=True)
        self.source.write_text(f'void provider(void) __asm__("{symbol}");\n'
                               'int fixture(void) { provider(); return 7; }\n')
        output = self.stage / DIST.ELFS[0]
        subprocess.run([*COMPILER, "-shared", "-fPIC", str(self.source), f"-L{self.directory}",
                        f"-l:{soname}", "-o", str(output)], check=True, capture_output=True)
        return output

    def test_public_qt5_renderer_export_is_recorded(self):
        symbol = "_ZN23QQuickFramebufferObject8RendererC2Ev"
        output = self.private_export_fixture(symbol)
        self.assertEqual(DIST.elf_info(output)["public_qt5_renderer_exports"], [symbol])

    def test_other_qt5_private_exports_still_rejected(self):
        output = self.private_export_fixture("unverified_private_implementation")
        with self.assertRaisesRegex(ValueError, "Qt private API dependency"):
            DIST.elf_info(output)

    def test_qt6_private_export_checks_are_not_relaxed(self):
        output = self.private_export_fixture("_ZN23QQuickFramebufferObject8RendererC2Ev", 6)
        with self.assertRaisesRegex(ValueError, "Qt private API dependency"):
            DIST.elf_info(output)


if __name__ == "__main__":
    unittest.main()
