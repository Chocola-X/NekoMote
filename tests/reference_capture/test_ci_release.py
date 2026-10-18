"""Exercise release gates without a token, network, or real GitHub writes."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "tools/ci_publish_release.sh"
PACKAGES = (
    "nekomote-native-ubuntu20-x86_64",
    "nekomote-native-ubuntu20-aarch64",
    "nekomote-plasma5-x86_64",
    "nekomote-plasma6-x86_64",
    "nekomote-plasma6-aarch64",
    "nekomote-layer-shell-debian12-x86_64",
    "nekomote-layer-shell-debian12-aarch64",
)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / "packages"
        self.assets.mkdir()
        for package in PACKAGES:
            archive = self.assets / f"{package}.tar.gz"
            archive.write_bytes(package.encode())
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            archive.with_suffix(".gz.sha256").write_text(f"{digest}  {archive.name}\n")
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
        expected = {f"{package}.tar.gz{suffix}" for package in PACKAGES
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
        unexpected = self.assets / "unexpected.tar.gz"
        unexpected.write_bytes(b"unexpected")
        self.assertNotEqual(self.run_release().returncode, 0)
        unexpected.unlink()
        archive = self.assets / f"{PACKAGES[0]}.tar.gz"
        archive.unlink()
        self.assertNotEqual(self.run_release().returncode, 0)
        unexpected.write_bytes(b"unexpected")
        self.assertNotEqual(self.run_release().returncode, 0)
        self.assertEqual(self.calls(), [])

    def test_invalid_versions_are_rejected_before_github_access(self):
        for tag in ("main", "v0.5", "v01.5.0", "v0.5.0-rc.1", "v0.5.0;false"):
            with self.subTest(tag=tag):
                self.assertNotEqual(self.run_release(tag).returncode, 0)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
