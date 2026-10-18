"""Missing system dependencies must fail without populating vendored sources."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('cmake') and shutil.which('ninja'), 'CMake and Ninja required')
class SystemBuildTests(unittest.TestCase):
    def test_missing_required_system_json_never_uses_fetchcontent(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory) / 'build'
            result = subprocess.run([
                'cmake', '-S', str(REPO), '-B', str(build), '-G', 'Ninja',
                '-DNEKOMOTE_USE_SYSTEM_DEPS=ON',
                '-DCMAKE_DISABLE_FIND_PACKAGE_nlohmann_json=ON',
                '-DFETCHCONTENT_FULLY_DISCONNECTED=ON',
                '-DNEKOMOTE_BUILD_PREVIEW=OFF', '-DNEKOMOTE_BUILD_PLAYER=OFF',
                '-DNEKOMOTE_BUILD_ABI=OFF', '-DNEKOMOTE_BUILD_TESTS=OFF',
                '-DNEKOMOTE_BUILD_TOOLS=OFF',
            ], capture_output=True, text=True, timeout=45)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('CMAKE_DISABLE_FIND_PACKAGE_nlohmann_json', result.stdout + result.stderr)
            self.assertFalse((build / '_deps').exists())


if __name__ == '__main__':
    unittest.main()
