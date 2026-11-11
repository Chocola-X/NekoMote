"""Distribution checks use real synthetic ELF files, without Qt or a desktop."""

import importlib.util
import hashlib
import io
import json
import os
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import zipfile
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

    def test_kpackage_relocates_audited_module_and_preserves_payload(self):
        root = self.stage / "share/plasma/wallpapers/uk.nekopara.nekomote"
        (root / "metadata.json").write_text(json.dumps({"KPackageStructure": "Plasma/Wallpaper",
                                                                    "KPlugin": {"Id": "uk.nekopara.nekomote"}}))
        (root / "metadata.desktop").write_text("X-KDE-PluginInfo-Version=0.1.0\n")
        (self.stage / "qml/NekoMote/Wallpaper/qmldir").write_text(
            "module NekoMote.Wallpaper\nplugin nekomote_plasma_qmlplugin\n"
            "prefer :/qt/qml/NekoMote/Wallpaper/\n")
        for name in ("LICENSE", "THIRD_PARTY_NOTICES"):
            (self.stage / name).write_text("license fixture\n")
        for major, qt in ((5, "5.12.8"), (6, "6.4.2")):
            with self.subTest(major=major):
                (root / "contents/ui/main.qml").write_text(
                    "import QtQuick\nimport NekoMote.Wallpaper" + (" 1.0" if major == 5 else "") + "\n")
                (root / "contents/ui/config.qml").write_text("import NekoMote.Wallpaper 1.0\n")
                self.manifest()
                output = self.directory / "wallpaper.zip"
                DIST.package_kpackage(self.stage, qt, output, package_version="1.2.3")
                first = output.read_bytes()
                DIST.package_kpackage(self.stage, qt, output, package_version="1.2.3")
                self.assertEqual(first, output.read_bytes())
                with zipfile.ZipFile(output) as archive:
                    for page in ("main.qml", "config.qml"):
                        self.assertIn(b'import "NekoMote/Wallpaper"', archive.read("contents/ui/" + page))
                    if major == 5:
                        self.assertIn(b'import "NekoMote/Wallpaper" 1.0', archive.read("contents/ui/main.qml"))
                    self.assertIn(b'import "NekoMote/Wallpaper" 1.0', archive.read("contents/ui/config.qml"))
                    qmldir = archive.read("contents/ui/NekoMote/Wallpaper/qmldir")
                    self.assertNotIn(b"module ", qmldir)
                    self.assertNotIn(b"prefer ", qmldir)
                    self.assertIn(b"module NekoMote.Wallpaper",
                                  (self.stage / "qml/NekoMote/Wallpaper/qmldir").read_bytes())
                    self.assertIn(b"plugin nekomote_plasma_qmlplugin", qmldir)
                    self.assertEqual(archive.read("contents/lib/libnekomote_plasma_qml.so"),
                                     (self.stage / DIST.ELFS[0]).read_bytes())
                    self.assertEqual(archive.read("contents/ui/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so"),
                                     (self.stage / DIST.ELFS[1]).read_bytes())
                    self.assertEqual(json.loads(archive.read("metadata.json"))["KPlugin"]["Version"], "1.2.3")
                    self.assertIn(b"Version=1.2.3", archive.read("metadata.desktop"))
                    self.assertFalse(any(name.startswith(("tools/", "stage/")) for name in archive.namelist()))
        with self.assertRaisesRegex(ValueError, "package version"):
            DIST.package_kpackage(self.stage, "6.4.2", output, package_version="main")
        (self.stage / DIST.ELFS[0]).write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            DIST.package_kpackage(self.stage, "6.4.2", output)

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

    def test_reproducible_bundle_contains_exact_store_payload_and_installer(self):
        root = self.stage / "share/plasma/wallpapers/uk.nekopara.nekomote"
        (root / "metadata.json").write_text(json.dumps({"KPackageStructure": "Plasma/Wallpaper",
            "KPlugin": {"Id": "uk.nekopara.nekomote"}}))
        for page in ("main.qml", "config.qml"):
            (root / "contents/ui" / page).write_text("import NekoMote.Wallpaper\n")
        for license_name in ("LICENSE", "THIRD_PARTY_NOTICES"):
            (self.stage / license_name).write_text("license fixture\n")
        self.manifest()
        metadata = DIST.audit(self.stage, "6.4.2", True)
        name = "nekomote-plasma6-" + metadata["architecture"]
        first = self.directory / (name + ".tar.gz")
        DIST.package_bundle(self.stage, "6.4.2", first, True, package_version="1.2.3")
        before = first.read_bytes()
        DIST.package_bundle(self.stage, "6.4.2", first, True, package_version="1.2.3")
        self.assertEqual(before, first.read_bytes())
        self.assertTrue(DIST.verify_bundle(first, "1.2.3")["passed"])
        with self.assertRaisesRegex(ValueError, "identity/version"):
            DIST.verify_bundle(first, "1.2.4")
        with tarfile.open(first) as archive:
            zip_name = name + ".wallpaper.zip"
            self.assertEqual(set(archive.getnames()), {
                name + "/" + zip_name, name + "/" + zip_name + ".sha256", name + "/installer.py"})
            payload = archive.extractfile(name + "/" + zip_name).read()
            self.assertEqual(archive.extractfile(name + "/" + zip_name + ".sha256").read(),
                             (hashlib.sha256(payload).hexdigest() + "  " + zip_name + "\n").encode())
            self.assertEqual(archive.extractfile(name + "/installer.py").read(),
                             (REPO / "tools/installer.py").read_bytes())
            self.assertEqual(archive.getmember(name + "/installer.py").mode, 0o755)
        with zipfile.ZipFile(io.BytesIO(payload)) as archive:
            self.assertEqual(json.loads(archive.read("metadata.json"))["KPlugin"]["Version"], "1.2.3")

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
