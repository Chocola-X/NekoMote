"""audit_atlas_sources 合成正/负例测试。

不需要真实浏览器、GPU 或素材。覆盖：
- PNG RGBA8 解码正确性
- resx Resources 解析
- 图集关联：MATCH / MISMATCH / MISSING_UPLOAD / EXTRA_UPLOAD / EMPTY
- 翻转匹配
- 重复输出拒绝
- 重复报告拒绝
- 错误保留
"""
import importlib.util
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from PIL import Image

TOOL = pathlib.Path(__file__).resolve().parents[2] / 'tools/reference_capture/audit_atlas_sources.py'
SPEC = importlib.util.spec_from_file_location('audit_atlas_sources', TOOL)
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)

INDEX_TOOL = pathlib.Path(__file__).resolve().parents[2] / 'tools/import_emote/catalog_index.py'
INDEX_SPEC = importlib.util.spec_from_file_location('catalog_index2', INDEX_TOOL)
catalog_index = importlib.util.module_from_spec(INDEX_SPEC)
INDEX_SPEC.loader.exec_module(catalog_index)


def write_png(path, width, height, rgba_bytes):
    Image.frombytes('RGBA', (width, height), rgba_bytes).save(path)


def solid_rgba(width, height, r, g, b, a=255):
    return bytes([r, g, b, a]) * (width * height)


class PngDecodeTest(unittest.TestCase):
    def test_decode_known_rgba8(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            png = root / 'tex.png'
            data = solid_rgba(2, 2, 255, 0, 0)
            write_png(png, 2, 2, data)
            width, height, sha, flipped_sha, byte_count = audit.decode_png_rgba8(png)
            self.assertEqual((width, height), (2, 2))
            self.assertEqual(byte_count, 16)
            self.assertEqual(sha, hashlib_sha256(data))

    def test_flipped_hash_differs_for_asymmetric_image(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            png = root / 'tex.png'
            top = bytes([255, 0, 0, 255]) * 2
            bottom = bytes([0, 0, 255, 255]) * 2
            data = top + bottom
            write_png(png, 2, 2, data)
            _, _, sha, flipped_sha, _ = audit.decode_png_rgba8(png)
            self.assertNotEqual(sha, flipped_sha)

    def test_flipped_hash_matches_for_symmetric_image(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            png = root / 'tex.png'
            data = solid_rgba(2, 2, 128, 128, 128)
            write_png(png, 2, 2, data)
            _, _, sha, flipped_sha, _ = audit.decode_png_rgba8(png)
            self.assertEqual(sha, flipped_sha)

    def test_decode_error_for_missing_file(self):
        with self.assertRaises((OSError, FileNotFoundError)):
            audit.decode_png_rgba8(pathlib.Path('/nonexistent/tex.png'))


class ResxParseTest(unittest.TestCase):
    def test_resources_parsed_to_absolute_paths(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            resx = root / 'model.resx.json'
            (root / 'model-resources').mkdir()
            (root / 'model-resources/tex000.png').write_bytes(b'')
            (root / 'model-resources/tex001.png').write_bytes(b'')
            resx.write_text(json.dumps({
                'Resources': {
                    '0': 'model-resources/tex000.png',
                    '1': 'model-resources/tex001.png',
                }
            }))
            resources = audit.parse_resx_resources(resx)
            self.assertEqual(set(resources.keys()), {'0', '1'})
            self.assertTrue(resources['0'].is_absolute())
            self.assertEqual(resources['0'].name, 'tex000.png')

    def test_empty_resources_returns_empty_dict(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            resx = root / 'model.resx.json'
            resx.write_text(json.dumps({'Resources': {}}))
            resources = audit.parse_resx_resources(resx)
            self.assertEqual(resources, {})


class AssociateAtlasTest(unittest.TestCase):
    def _png(self, key, sha, flipped_sha=None):
        return {'resx_key': key, 'path': f'{key}.png', 'sha256': f'file_{key}',
                'width': 64, 'height': 64, 'rgba_sha256': sha,
                'flipped_sha256': flipped_sha or sha, 'rgba_bytes': 16384,
                'status': 'OK'}

    def _upload(self, sha):
        return {'width': 64, 'height': 64, 'bytes': 16384, 'rgba_sha256': sha}

    def test_all_match(self):
        pngs = [self._png('0', 'aaa'), self._png('1', 'bbb')]
        uploads = [self._upload('aaa'), self._upload('bbb')]
        status, matches, errors = audit.associate_atlas(pngs, uploads)
        self.assertEqual(status, 'MATCH')
        self.assertEqual(len(matches), 2)
        self.assertFalse(errors)

    def test_flipped_match(self):
        pngs = [self._png('0', 'normal', 'flipped')]
        uploads = [self._upload('flipped')]
        status, matches, errors = audit.associate_atlas(pngs, uploads)
        self.assertEqual(status, 'MATCH')
        self.assertEqual(matches[0]['orientation'], 'flipped')

    def test_missing_upload(self):
        pngs = [self._png('0', 'aaa'), self._png('1', 'bbb')]
        uploads = [self._upload('aaa')]
        status, matches, errors = audit.associate_atlas(pngs, uploads)
        self.assertEqual(status, 'MISSING_UPLOAD')
        statuses = [m['status'] for m in matches]
        self.assertIn('MATCH', statuses)
        self.assertIn('MISSING_UPLOAD', statuses)

    def test_extra_upload(self):
        pngs = [self._png('0', 'aaa')]
        uploads = [self._upload('aaa'), self._upload('zzz')]
        status, matches, errors = audit.associate_atlas(pngs, uploads)
        self.assertEqual(status, 'EXTRA_UPLOAD')
        statuses = [m['status'] for m in matches]
        self.assertIn('EXTRA_UPLOAD', statuses)

    def test_mismatch_both_sides(self):
        pngs = [self._png('0', 'aaa'), self._png('1', 'bbb')]
        uploads = [self._upload('xxx'), self._upload('yyy')]
        status, matches, errors = audit.associate_atlas(pngs, uploads)
        self.assertEqual(status, 'MISMATCH')

    def test_empty_both(self):
        status, matches, errors = audit.associate_atlas([], [])
        self.assertEqual(status, 'EMPTY')
        self.assertTrue(errors)

    def test_no_png(self):
        uploads = [self._upload('aaa')]
        status, matches, errors = audit.associate_atlas([], uploads)
        self.assertEqual(status, 'NO_PNG')

    def test_no_upload(self):
        pngs = [self._png('0', 'aaa')]
        status, matches, errors = audit.associate_atlas(pngs, [])
        self.assertEqual(status, 'NO_UPLOAD')

    def test_multiple_atlas_all_matched(self):
        pngs = [self._png('0', 'aaa'), self._png('1', 'bbb'), self._png('2', 'ccc')]
        uploads = [self._upload('ccc'), self._upload('aaa'), self._upload('bbb')]
        status, matches, errors = audit.associate_atlas(pngs, uploads)
        self.assertEqual(status, 'MATCH')
        self.assertEqual(len(matches), 3)


class AuditOneTest(unittest.TestCase):
    """audit_one 的关联与错误保留，mock 浏览器捕获。"""

    def _setup_model(self, png_count=1, png_data=None):
        root = pathlib.Path(tempfile.mkdtemp())
        reference = root / 'reference'
        char_dir = reference / 'model_conversion/toy'
        char_dir.mkdir(parents=True)
        source = char_dir / 'toy.json'
        source.write_text('{"source":{}}')
        resx = char_dir / 'toy.resx.json'
        res_dir = char_dir / 'toy-resources'
        res_dir.mkdir()
        resources = {}
        for i in range(png_count):
            key = str(i)
            png_name = f'tex{i}.png'
            data = png_data[i] if png_data else solid_rgba(4, 4, i * 50, 0, 0)
            write_png(res_dir / png_name, 4, 4, data)
            resources[key] = f'toy-resources/{png_name}'
        resx.write_text(json.dumps({'Resources': resources}))
        psb_zip = reference / 'data/toy.pure.psb.zip'
        psb_zip.parent.mkdir()
        psb_zip.write_bytes(b'fake psb')
        index = catalog_index.build_index(reference)
        return root, reference, index

    def test_match_when_upload_hashes_equal_png(self):
        png_data = solid_rgba(4, 4, 200, 100, 50)
        root, reference, index = self._setup_model(png_data=[png_data])
        _, _, sha, _, _ = audit.decode_png_rgba8(
            reference / 'model_conversion/toy/toy-resources/tex0.png')
        with mock.patch.object(audit, 'run_capture',
                               return_value=([{'width': 4, 'height': 4,
                                                'bytes': 64, 'rgba_sha256': sha}],
                                              {'returncode': 0})):
            row = audit.audit_one('toy', {}, reference, index,
                                  pathlib.Path('capture.py'), pathlib.Path('chrome'),
                                  root / 'output', 60)
        self.assertEqual(row['status'], 'MATCH')
        import shutil
        shutil.rmtree(root)

    def test_capture_error_preserved(self):
        root, reference, index = self._setup_model()
        with mock.patch.object(audit, 'run_capture',
                               return_value=(None, {'returncode': 1,
                                                    'reason': 'browser crash'})):
            row = audit.audit_one('toy', {}, reference, index,
                                  pathlib.Path('capture.py'), pathlib.Path('chrome'),
                                  root / 'output', 60)
        self.assertEqual(row['status'], 'CAPTURE_ERROR')
        self.assertIn('browser crash', row['errors'])
        import shutil
        shutil.rmtree(root)

    def test_missing_candidate_rejected(self):
        root = pathlib.Path(tempfile.mkdtemp())
        reference = root / 'reference'
        (reference / 'model_conversion').mkdir(parents=True)
        index = catalog_index.build_index(reference)
        row = audit.audit_one('absent', {}, reference, index,
                              pathlib.Path('capture.py'), pathlib.Path('chrome'),
                              root / 'output', 60)
        self.assertEqual(row['status'], 'MISSING')
        import shutil
        shutil.rmtree(root)

    def test_no_resx_rejected(self):
        root = pathlib.Path(tempfile.mkdtemp())
        reference = root / 'reference'
        char_dir = reference / 'model_conversion/toy'
        char_dir.mkdir(parents=True)
        source = char_dir / 'toy.json'
        source.write_text('{"source":{}}')
        index = catalog_index.build_index(reference)
        row = audit.audit_one('toy', {}, reference, index,
                              pathlib.Path('capture.py'), pathlib.Path('chrome'),
                              root / 'output', 60)
        self.assertEqual(row['status'], 'NO_RESX')
        import shutil
        shutil.rmtree(root)


class CliParseTest(unittest.TestCase):
    """纯 CLI 解析契约，不需要真实素材或 GPU。"""

    def test_existing_output_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            output = root / 'output'
            output.mkdir()
            with mock.patch('sys.argv', ['audit_atlas_sources.py',
                                         '--reference-root', str(root),
                                         '--capture-tool', str(root / 'c.py'),
                                         '--browser', str(root / 'chrome'),
                                         '--output', str(output),
                                         '--report', str(root / 'r.json')]):
                with self.assertRaises(SystemExit) as ctx:
                    audit.main()
            self.assertNotEqual(ctx.exception.code, 0)

    def test_existing_report_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            report = root / 'r.json'
            report.write_text('{}')
            with mock.patch('sys.argv', ['audit_atlas_sources.py',
                                         '--reference-root', str(root),
                                         '--capture-tool', str(root / 'c.py'),
                                         '--browser', str(root / 'chrome'),
                                         '--output', str(root / 'out'),
                                         '--report', str(report)]):
                with self.assertRaises(SystemExit) as ctx:
                    audit.main()
            self.assertNotEqual(ctx.exception.code, 0)

    def test_timeout_range_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            for value in ('0', '601'):
                with self.subTest(value=value):
                    with mock.patch('sys.argv', ['audit_atlas_sources.py',
                                                 '--reference-root', str(root),
                                                 '--capture-tool', str(root / 'c.py'),
                                                 '--browser', str(root / 'chrome'),
                                                 '--output', str(root / 'out'),
                                                 '--report', str(root / 'r.json'),
                                                 '--timeout', value]):
                        with self.assertRaises(SystemExit) as ctx:
                            audit.main()
                    self.assertNotEqual(ctx.exception.code, 0)

    def test_missing_catalog_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            with mock.patch('sys.argv', ['audit_atlas_sources.py',
                                         '--reference-root', str(root),
                                         '--capture-tool', str(root / 'c.py'),
                                         '--browser', str(root / 'chrome'),
                                         '--output', str(root / 'out'),
                                         '--report', str(root / 'r.json')]):
                with self.assertRaises(SystemExit) as ctx:
                    audit.main()
            self.assertNotEqual(ctx.exception.code, 0)


def hashlib_sha256(data):
    import hashlib
    return hashlib.sha256(data).hexdigest()


if __name__ == '__main__':
    unittest.main()
