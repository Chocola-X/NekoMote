"""Exercise release gates without a token, network, or real GitHub writes."""

import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tarfile
import zipfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "tools/ci_publish_release.sh"
PACKAGES = (
    "nekomote-native-ubuntu20-x86_64",
    "nekomote-native-ubuntu20-aarch64",
    "nekomote-plasma5-x86_64",
    "nekomote-plasma5-aarch64",
    "nekomote-plasma6-x86_64",
    "nekomote-plasma6-aarch64",
    "nekomote-layer-shell-debian12-x86_64",
    "nekomote-layer-shell-debian12-aarch64",
    "nekomote-x11-ubuntu20-x86_64",
    "nekomote-x11-ubuntu20-aarch64",
)

ARCHIVES = [f"{p}.tar.gz" for p in PACKAGES]


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / "packages"
        self.assets.mkdir()
        for name in ARCHIVES:
            archive = self.assets / name
            if "-plasma" in name:
                self.plasma_bundle(archive)
            else:
                archive.write_bytes(name.encode())
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            archive.with_name(archive.name + ".sha256").write_text(f"{digest}  {archive.name}\n")
        self.log = self.root / "calls.jsonl"
        mock = self.root / "gh"
        mock.write_text(f"#!{sys.executable}\n" + '''
import json, os, sys
with open(os.environ["MOCK_GH_LOG"], "a") as stream:
    stream.write(json.dumps(sys.argv[1:]) + "\\n")
operation = sys.argv[1:3]
if operation == ["release", "view"]:
    state = os.environ.get("MOCK_GH_STATE", "missing")
    if state == "missing":
        sys.exit(1)
    print("true" if state == "draft" else "false")
elif operation == ["release", "upload"] and os.environ.get("MOCK_GH_UPLOAD_FAIL"):
    sys.exit(1)
''')
        mock.chmod(0o755)
        self.env = dict(os.environ, PATH=f"{self.root}{os.pathsep}{os.environ['PATH']}",
                        GH_REPO="test/nekomote", GH_TOKEN="offline-test-token",
                        MOCK_GH_LOG=str(self.log), MOCK_GH_STATE="missing",
                        MOCK_GH_UPLOAD_FAIL="")

    def plasma_bundle(self, archive, version="0.5.0", architecture=None, corrupt_binary=False,
                      corrupt_checksum=False):
        name = archive.name[:-7]
        major = int(name.split("-")[1][-1])
        payload = io.BytesIO()
        library = b"synthetic native library"
        metadata = {"KPackageStructure": "Plasma/Wallpaper", "KPlugin": {
            "Id": "uk.nekopara.nekomote", "Version": version}}
        binaries = {source: {"sha256": hashlib.sha256(library).hexdigest()} for source in (
            "lib/libnekomote_plasma_qml.so", "qml/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so")}
        compatibility = {"plasma_major": major, "architecture": architecture or name.split("-")[-1],
                         "binaries": binaries}
        with zipfile.ZipFile(payload, "w") as inner:
            inner.writestr("metadata.json", json.dumps(metadata))
            inner.writestr("contents/compatibility.json", json.dumps(compatibility))
            inner.writestr("contents/lib/libnekomote_plasma_qml.so", library)
            inner.writestr("contents/ui/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so",
                           b"damaged" if corrupt_binary else library)
        zip_name = name + ".wallpaper.zip"
        files = {zip_name: payload.getvalue(), "installer.py": b"# installer fixture\n",
                 zip_name + ".sha256": (("0" * 64 if corrupt_checksum else
                                        hashlib.sha256(payload.getvalue()).hexdigest()) +
                                       "  " + zip_name + "\n").encode()}
        with tarfile.open(archive, "w:gz") as outer:
            for relative, data in files.items():
                info = tarfile.TarInfo(name + "/" + relative)
                info.size = len(data)
                outer.addfile(info, io.BytesIO(data))
        archive.with_name(archive.name + ".sha256").write_text(
            hashlib.sha256(archive.read_bytes()).hexdigest() + "  " + archive.name + "\n")

    def run_release(self, tag="v0.5.0"):
        return subprocess.run(["bash", str(SCRIPT), tag, str(self.assets)],
                              env=self.env, capture_output=True, text=True)

    def calls(self):
        if not self.log.exists():
            return []
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def test_complete_set_is_uploaded_before_publishing(self):
        result = self.run_release()
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        self.assertEqual([call[1] for call in calls], ["view", "create", "upload", "edit"])
        self.assertIn("--draft", calls[1])
        self.assertIn("--verify-tag", calls[1])
        self.assertIn("--generate-notes", calls[1])
        expected = {f"{name}{suffix}" for name in ARCHIVES
                    for suffix in ("", ".sha256")}
        self.assertEqual(set(calls[2][3:-1]), expected)
        self.assertIn("--draft=false", calls[3])

    def test_draft_can_resume_without_creating_another_release(self):
        self.env["MOCK_GH_STATE"] = "draft"
        result = self.run_release()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([call[1] for call in self.calls()], ["view", "upload", "edit"])

    def test_published_release_is_never_overwritten(self):
        self.env["MOCK_GH_STATE"] = "published"
        self.assertNotEqual(self.run_release().returncode, 0)
        self.assertEqual([call[1] for call in self.calls()], ["view"])

    def test_upload_failure_leaves_release_unpublished(self):
        self.env["MOCK_GH_UPLOAD_FAIL"] = "1"
        self.assertNotEqual(self.run_release().returncode, 0)
        self.assertEqual([call[1] for call in self.calls()], ["view", "create", "upload"])

    def test_corrupted_package_is_rejected_before_github_access(self):
        (self.assets / f"{PACKAGES[0]}.tar.gz").write_bytes(b"corrupted")
        self.assertNotEqual(self.run_release().returncode, 0)
        self.assertEqual(self.calls(), [])

    def test_missing_or_unexpected_files_are_rejected_before_github_access(self):
        # Every package in the ten-package set is mandatory: dropping any one
        # of them (for example after adding a new adapter) must fail the gate.
        for package in PACKAGES:
            archive = self.assets / f"{package}.tar.gz"
            saved = archive.read_bytes()
            archive.unlink()
            with self.subTest(missing=package):
                self.assertNotEqual(self.run_release().returncode, 0)
            archive.write_bytes(saved)
        unexpected = self.assets / "unexpected.tar.gz"
        unexpected.write_bytes(b"unexpected")
        self.assertNotEqual(self.run_release().returncode, 0)
        unexpected.unlink()
        digest = hashlib.sha256((self.assets / f"{PACKAGES[0]}.tar.gz").read_bytes()).hexdigest()
        (self.assets / f"{PACKAGES[0]}.tar.gz.sha256").write_text(f"{digest}  stale-name.tar.gz\n")
        self.assertNotEqual(self.run_release().returncode, 0)
        self.assertEqual(self.calls(), [])

    def test_inner_version_platform_and_binary_hash_rejected_before_github_access(self):
        archive = self.assets / "nekomote-plasma6-x86_64.tar.gz"
        for kwargs in ({"version": "0.0.0"}, {"architecture": "aarch64"},
                       {"corrupt_binary": True}, {"corrupt_checksum": True}):
            with self.subTest(kwargs=kwargs):
                self.plasma_bundle(archive, **kwargs)
                self.assertNotEqual(self.run_release().returncode, 0)
                self.assertEqual(self.calls(), [])

    def test_invalid_versions_are_rejected_before_github_access(self):
        for tag in ("main", "v0.5", "v01.5.0", "v0.5.0-rc.1", "v0.5.0;false"):
            with self.subTest(tag=tag):
                self.assertNotEqual(self.run_release(tag).returncode, 0)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
