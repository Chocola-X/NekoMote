"""Exercise launch persistence through the player process without a display server."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()


class PlayerLaunchTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="nekomote-player-config-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.environment = dict(os.environ, HOME=str(self.root / "home"),
                                XDG_CONFIG_HOME=str(self.root / "configuration"))
        self.environment.pop("DISPLAY", None)
        self.environment.pop("WAYLAND_DISPLAY", None)

    def run_player(self, *arguments, success=True):
        result = subprocess.run([str(BINARY), *map(str, arguments)], cwd=self.root,
                                env=self.environment, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def test_explicit_save_and_reload_without_a_display_or_default_assets(self):
        default = self.root / "configuration/nekomote/player.json"
        self.run_player(success=False)
        self.assertFalse(default.parent.exists())
        self.run_player("--assets", "large-assets", "--width", 640, "--height", 480,
                        "--save-config")
        saved = json.loads(default.read_text())
        self.assertEqual(saved["assets_dir"], str(self.root / "large-assets"))
        self.assertEqual((saved["width"], saved["height"]), (640, 480))
        self.assertNotIn("state_path", saved)
        self.assertFalse(default.with_name("player.panel.json").exists())
        self.run_player("--save-config")
        self.assertEqual(json.loads(default.read_text()), saved)

    def test_profiles_are_independent_and_state_override_is_normalized(self):
        one, two = self.root / "one.json", self.root / "two.json"
        self.run_player("--config", one, "--assets", "one-assets", "--save-config")
        before = one.read_bytes()
        self.run_player("--config", two, "--assets", "two-assets", "--state", "custom.json",
                        "--save-config")
        self.assertEqual(one.read_bytes(), before)
        self.assertEqual(json.loads(two.read_text())["state_path"], str(self.root / "custom.json"))
        self.assertNotIn("state_path", json.loads(one.read_text()))

    def test_source_switch_and_session_argument_order(self):
        profile = self.root / "switch.json"
        self.run_player("--config", profile, "--assets", "assets", "--save-config")
        self.run_player("--config", profile, "--session", "session.nks.json", "--model",
                        "model.nkm", "--save-config")
        value = json.loads(profile.read_text())
        self.assertNotIn("assets_dir", value)
        self.assertEqual(value["session_path"], str(self.root / "session.nks.json"))

    def test_corrupt_config_is_not_overwritten_and_help_does_not_load_it(self):
        profile = self.root / "damaged.json"
        profile.write_text("not JSON")
        self.run_player("--config", profile, "--assets", "assets", "--save-config", success=False)
        self.assertEqual(profile.read_text(), "not JSON")
        self.run_player("--config", profile, "--help")

    def test_conflicting_state_and_save_diagnostics_are_rejected(self):
        profile = self.root / "same.json"
        self.run_player("--config", profile, "--assets", "assets", "--state", profile,
                        "--save-config", success=False)
        self.assertFalse(profile.exists())
        self.run_player("--config", profile, "--assets", "assets", "--no-save",
                        "--save-config", success=False)
        self.assertFalse(profile.exists())


if __name__ == "__main__":
    unittest.main()
