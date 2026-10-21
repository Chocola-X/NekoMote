"""One-time migration preserves the source and cannot overwrite another instance."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import migrate_wallpaper_config as migration


class WallpaperMigrationTests(unittest.TestCase):
    def test_preserves_preferences_and_source_removes_launch_dimensions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / "legacy.json", root / "private/panel.json"
            source.write_text(json.dumps({"character": "azuki/casual", "fps": 30,
                                          "mute": True, "width": 800, "height": 600}))
            before = source.read_bytes()
            migration.migrate(source, output)
            self.assertEqual(source.read_bytes(), before)
            self.assertEqual(json.loads(output.read_text()), {
                "version": 1, "character": "azuki/casual", "fps": 30, "mute": True})
            with self.assertRaisesRegex(ValueError, "already exists"):
                migration.migrate(source, output)
            self.assertFalse(list(output.parent.glob(".nekomote-migrate-*")))

    def test_rejects_invalid_or_new_format_before_creating_output_directory(self):
        invalid = ({"version": 1}, {"unknown": 1}, {"fps": 900}, {"mute": 1},
                   {"transform_saved": True}, {"scale": float("nan")},
                   {"origin_x": 1e100}, {"character": "bad\0name"})
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for value in invalid:
                source, output = root / "legacy.json", root / "absent/panel.json"
                source.write_text(json.dumps(value))
                with self.assertRaises(ValueError):
                    migration.migrate(source, output)
                self.assertFalse(output.parent.exists())

    def test_refuses_dangling_output_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / "legacy.json", root / "panel.json"
            source.write_text("{}")
            output.symlink_to("absent")
            with self.assertRaisesRegex(ValueError, "already exists"):
                migration.migrate(source, output)
            self.assertTrue(output.is_symlink())
