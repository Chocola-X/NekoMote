"""Package notices and standalone ELF gates, without a desktop or GitHub writes."""

import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.ci_package_licenses import write_licenses

REPO = Path(__file__).resolve().parents[2]
with patch.object(sys, "path", [str(REPO / "tools"), *sys.path]):
    SPEC = importlib.util.spec_from_file_location("ci_native_audit", REPO / "tools/ci_native_audit.py")
    AUDIT = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(AUDIT)


class NoticeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.stage = self.root / "stage"
        for name, filename in (("nlohmann_json", "LICENSE.MIT"), ("fast_float", "LICENSE-MIT"),
                               ("stb", "LICENSE"), ("glad", "LICENSE"), ("glfw", "LICENSE.md"),
                               ("flac", "COPYING.Xiph")):
            path = self.build / f"_deps/{name}-src/{filename}"
            path.parent.mkdir(parents=True)
            path.write_text(f"Copyright and permission for fixture {name}\n")

    def test_layer_shell_preserves_dependency_and_protocol_notices_without_readme(self):
        write_licenses(REPO, self.build, self.stage, "layer-shell")
        self.assertEqual((self.stage / "LICENSE").read_bytes(), (REPO / "LICENSE").read_bytes())
        notices = (self.stage / "THIRD_PARTY_NOTICES").read_text()
        for text in ("fixture nlohmann_json", "fixture fast_float", "fixture stb", "fixture glad",
                     "fixture flac", "Apache License", "CC0 1.0 Universal", "Drew DeVault",
                     "Kristian Høgsberg"):
            self.assertIn(text, notices)
        self.assertNotIn("fixture glfw", notices)
        self.assertEqual({path.name for path in self.stage.iterdir()},
                         {"LICENSE", "THIRD_PARTY_NOTICES"})

    def test_x11_notices_keep_static_dependencies_without_glfw_or_wayland(self):
        write_licenses(REPO, self.build, self.stage, "x11")
        notices = (self.stage / "THIRD_PARTY_NOTICES").read_text()
        for text in ("fixture nlohmann_json", "fixture fast_float", "fixture stb", "fixture glad",
                     "fixture flac", "Apache License", "CC0 1.0 Universal"):
            self.assertIn(text, notices)
        self.assertNotIn("fixture glfw", notices)
        self.assertNotIn("Drew DeVault", notices)
        self.assertNotIn("Kristian Høgsberg", notices)
        self.assertEqual({path.name for path in self.stage.iterdir()},
                         {"LICENSE", "THIRD_PARTY_NOTICES"})

    def test_only_native_package_includes_glfw(self):
        for mode in ("native20", "plasma5", "plasma6", "x11"):
            with self.subTest(mode=mode):
                write_licenses(REPO, self.build, self.stage, mode)
                self.assertEqual("fixture glfw" in (self.stage / "THIRD_PARTY_NOTICES").read_text(),
                                 mode == "native20")

    def test_missing_required_notice_fails_before_creating_package(self):
        (self.build / "_deps/glad-src/LICENSE").unlink()
        with self.assertRaises(FileNotFoundError):
            write_licenses(REPO, self.build, self.stage, "layer-shell")
        self.assertFalse(self.stage.exists())


COMPILER = shlex.split(os.environ.get("CXX", "c++"))
AVAILABLE = COMPILER and shutil.which(COMPILER[0]) and shutil.which("readelf") and shutil.which("ldd")


@unittest.skipUnless(AVAILABLE, "C++ compiler, readelf and ldd required")
class StandaloneAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.stage = self.root / "stage"
        (self.stage / "bin").mkdir(parents=True)
        source = self.root / "fixture.cpp"
        source.write_text("#include <exception>\nint main() { std::terminate(); }\n")
        subprocess.run([*COMPILER, str(source), "-o", str(self.stage / "bin/nekomote_layer_shell")],
                       check=True, capture_output=True)

    def test_real_layer_shell_elf_uses_debian12_limits(self):
        report = AUDIT.audit(self.stage, "layer-shell")
        self.assertEqual(report["baseline"], "debian12")
        self.assertEqual(report["limits"]["GLIBC"], "2.36")
        self.assertEqual(set(report["binaries"]), {"nekomote_layer_shell"})

    def test_newer_symbols_and_qt_dependencies_are_rejected(self):
        actual = AUDIT.elf_info(self.stage / "bin/nekomote_layer_shell")
        actual["versions"]["GLIBC"] = "2.37"
        with patch.object(AUDIT, "elf_info", return_value=actual):
            with self.assertRaisesRegex(ValueError, "exceeds debian12"):
                AUDIT.audit(self.stage, "layer-shell")
        actual["versions"]["GLIBC"] = "2.36"
        actual["needed"].append("libQt6Core.so.6")
        with patch.object(AUDIT, "elf_info", return_value=actual):
            with self.assertRaisesRegex(ValueError, "unexpected desktop/Qt"):
                AUDIT.audit(self.stage, "layer-shell")

    def test_unexpected_program_rejected(self):
        (self.stage / "bin/unexpected").write_text("extra")
        with self.assertRaisesRegex(ValueError, "exactly"):
            AUDIT.audit(self.stage, "layer-shell")

    def test_x11_package_uses_ubuntu20_limits_and_single_program(self):
        fixture = self.stage / "bin/nekomote_layer_shell"
        shutil.copyfile(fixture, self.stage / "bin/nekomote_x11")
        (self.stage / "bin/nekomote_x11").chmod(0o755)
        fixture.unlink()
        # The host compiler may target a newer glibc; freeze versions at the
        # declared x11 baseline while keeping real ELF parsing and ldd checks.
        info = AUDIT.elf_info(self.stage / "bin/nekomote_x11")
        info["versions"] = {"GLIBC": "2.31", "GLIBCXX": "3.4.28", "CXXABI": "1.3.11"}
        with patch.object(AUDIT, "elf_info", return_value=info):
            report = AUDIT.audit(self.stage, "x11")
            self.assertEqual(report["baseline"], "ubuntu20")
            self.assertEqual(report["limits"]["GLIBC"], "2.31")
            self.assertEqual(report["limits"]["GLIBCXX"], "3.4.28")
            self.assertEqual(set(report["binaries"]), {"nekomote_x11"})
            info["versions"]["GLIBC"] = "2.32"
            with self.assertRaisesRegex(ValueError, "exceeds ubuntu20"):
                AUDIT.audit(self.stage, "x11")
        (self.stage / "bin/unexpected").write_text("extra")
        with self.assertRaisesRegex(ValueError, "exactly"):
            AUDIT.audit(self.stage, "x11")

    def test_native_package_requires_png_converter_and_keeps_elf_gates(self):
        fixture = self.stage / "bin/nekomote_layer_shell"
        programs = {"nekomote_player", "nekomote_import", "nekomote_audio_convert",
                    "nekomote_model_convert", "nekomote_png_recompress", "nekomote_abi_host"}
        for name in programs:
            shutil.copyfile(fixture, self.stage / "bin" / name)
            (self.stage / "bin" / name).chmod(0o755)
        fixture.unlink()
        # The host compiler may target a newer glibc. Keep real ELF dependency
        # parsing and ldd, but freeze versions at the declared native baseline.
        info = AUDIT.elf_info(self.stage / "bin/nekomote_player")
        info["versions"] = {"GLIBC": "2.31", "GLIBCXX": "3.4.28", "CXXABI": "1.3.11"}
        with patch.object(AUDIT, "elf_info", return_value=info):
            report = AUDIT.audit(self.stage, "native20")
            self.assertEqual(report["baseline"], "ubuntu20")
            self.assertEqual(set(report["binaries"]), programs)
            info["needed"].append("libFLAC.so.12")
            with self.assertRaisesRegex(ValueError, "statically link pinned FLAC"):
                AUDIT.audit(self.stage, "native20")
        (self.stage / "bin/nekomote_png_recompress").unlink()
        with self.assertRaisesRegex(ValueError, "exactly"):
            AUDIT.audit(self.stage, "native20")


if __name__ == "__main__":
    unittest.main()
