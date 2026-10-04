import json
import pathlib
import subprocess
import tempfile
import unittest

from PIL import Image


class ImageEvidenceTests(unittest.TestCase):
    def test_decimal_case_name_keeps_its_complete_evidence_prefix(self):
        tool = pathlib.Path(__file__).resolve().parents[2] / 'tools/reference_capture/compare_images.py'
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source, native = root / 'source.png', root / 'native.png'
            Image.new('RGBA', (2, 2), (120, 80, 40, 255)).save(source)
            Image.new('RGBA', (2, 2), (120, 80, 40, 255)).save(native)
            prefix = root / 'weight-0.5-comparison'
            subprocess.run(['python3', str(tool), str(source), str(native),
                            '--output', str(prefix), '--tolerance', '2'],
                           check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertTrue(json.loads(pathlib.Path(str(prefix) + '.json').read_text())['passed'])
            self.assertTrue(pathlib.Path(str(prefix) + '.png').is_file())
            self.assertFalse((root / 'weight-0.json').exists())


if __name__ == '__main__':
    unittest.main()
