"""Qt major deployment checks in temporary paths; never control a desktop."""
import importlib.util
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "plasma_deploy", Path(__file__).resolve().parents[2] / "tools/plasma_deploy.py")
DEPLOY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DEPLOY)


class PlatformDeployTests(unittest.TestCase):
    def test_plasma5_install_and_uninstall_without_qmltypes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stage = root / "stage"
            package = stage / "share/plasma/wallpapers" / DEPLOY.PACKAGE_ID
            package.mkdir(parents=True)
            (package / "metadata.desktop").write_text("[Desktop Entry]\n")
            for relative in ("lib/libnekomote_plasma_qml.so", "qml/NekoMote/Wallpaper/qmldir",
                             "qml/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so"):
                path = stage / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("fixture")
            (stage / "manifest.json").write_text(json.dumps({"files": {}}))
            prefix = root / "data"
            with patch.dict(os.environ, {"XDG_CONFIG_HOME": str(root / "config")}):
                self.assertEqual(DEPLOY.do_install(stage, prefix), 0)
                env = DEPLOY.environment_file()
                self.assertEqual(env.read_text(), f'QML2_IMPORT_PATH="{prefix}/nekomote/qml"\n')
                self.assertEqual(DEPLOY.do_verify(prefix), 0)
                self.assertEqual(DEPLOY.do_uninstall(prefix), 0)
                self.assertFalse(env.exists())

    def test_qt6_environment_retains_its_contract(self):
        self.assertEqual(DEPLOY.environment_content(Path("/home/test/.local/share"), 6),
                         'QML_IMPORT_PATH="/home/test/.local/share/nekomote/qml"\n')

    def test_each_platform_uses_its_cache_tool(self):
        for major in (5, 6):
            with self.subTest(major=major), patch.object(DEPLOY.shutil, "which", return_value=None) as which:
                DEPLOY.refresh_package_cache(major)
                which.assert_called_once_with(f"kbuildsycoca{major}")
