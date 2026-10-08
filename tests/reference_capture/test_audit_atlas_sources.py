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
import contextlib
import io
import json
import pathlib
import tempfile
import unittest
import zipfile
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

    def test_rgb_png_decodes_to_rgba8_and_non_png_is_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            png = root / 'rgb.png'
            Image.new('RGB', (1, 1), (12, 34, 56)).save(png)
            self.assertEqual(audit.decode_png_rgba8(png)[2],
                             hashlib_sha256(bytes([12, 34, 56, 255])))
            fake_png = root / 'fake.png'
            Image.new('RGB', (1, 1)).save(fake_png, format='JPEG')
            with self.assertRaisesRegex(ValueError, 'not a PNG'):
                audit.decode_png_rgba8(fake_png)


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

    def test_malformed_resource_shape_or_path_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            resx = pathlib.Path(name) / 'model.resx.json'
            for document in ([], {}, {'Resources': []}, {'Resources': {'0': 10}}):
                with self.subTest(document=document):
                    resx.write_text(json.dumps(document))
                    with self.assertRaises(ValueError):
                        audit.parse_resx_resources(resx)


class AssociateAtlasTest(unittest.TestCase):
    def _png(self, key, sha, flipped_sha=None):
        return {'resx_key': key, 'path': f'{key}.png', 'sha256': f'file_{key}',
                'width': 64, 'height': 64, 'rgba_sha256': hashlib_sha256(sha.encode()),
                'flipped_sha256': hashlib_sha256((flipped_sha or sha).encode()),
                'rgba_bytes': 16384,
                'status': 'OK'}

    def _upload(self, sha):
        return {'width': 64, 'height': 64, 'bytes': 16384,
                'rgba_sha256': hashlib_sha256(sha.encode())}

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

    def test_same_bytes_different_dimensions_rejected(self):
        upload = self._upload('aaa')
        upload.update(width=128, height=32)
        status, _, _ = audit.associate_atlas([self._png('0', 'aaa')], [upload])
        self.assertEqual(status, 'MISMATCH')

    def test_invalid_byte_count_or_upload_shape_rejected(self):
        for upload in (None, {}, {**self._upload('aaa'), 'bytes': 1},
                       {**self._upload('aaa'), 'width': True},
                       {**self._upload('aaa'), 'rgba_sha256': 'not-a-sha'}):
            with self.subTest(upload=upload):
                self.assertEqual(audit.associate_atlas([self._png('0', 'aaa')],
                                                      [upload])[0], 'INVALID_UPLOAD')

    def test_error_resource_cannot_be_filtered_to_success(self):
        self.assertEqual(audit.associate_atlas(
            [self._png('0', 'aaa'), {'status': 'PNG_MISSING'}],
            [self._upload('aaa')])[0], 'PNG_ERROR')


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
        with zipfile.ZipFile(psb_zip, 'w') as archive:
            archive.writestr('toy.psb', b'fake psb')
        (reference / 'driver').mkdir()
        for driver in ('FreeMoteDriver.js', 'emoteplayer.js'):
            (reference / 'driver' / driver).write_bytes(b'fake driver')
        index = catalog_index.build_index(reference)
        return root, reference, index

    def _capture_detail(self):
        return {'returncode': 0, 'source_hashes': {
            'psb': hashlib_sha256(b'fake psb'), 'original_psb': hashlib_sha256(b'fake psb'),
            'FreeMoteDriver.js': hashlib_sha256(b'fake driver'),
            'emoteplayer.js': hashlib_sha256(b'fake driver')}}

    def test_match_when_upload_hashes_equal_png(self):
        png_data = solid_rgba(4, 4, 200, 100, 50)
        root, reference, index = self._setup_model(png_data=[png_data])
        _, _, sha, _, _ = audit.decode_png_rgba8(
            reference / 'model_conversion/toy/toy-resources/tex0.png')
        with mock.patch.object(audit, 'run_capture',
                               return_value=([{'width': 4, 'height': 4,
                                                'bytes': 64, 'rgba_sha256': sha}],
                                              self._capture_detail())):
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
        root, reference, index = self._setup_model()
        (reference / 'model_conversion/toy/toy.resx.json').unlink()
        row = audit.audit_one('toy', {}, reference, index,
                              pathlib.Path('capture.py'), pathlib.Path('chrome'),
                              root / 'output', 60)
        self.assertEqual(row['status'], 'NO_RESX')
        import shutil
        shutil.rmtree(root)

    def test_partial_missing_png_is_error_without_capture(self):
        root, reference, index = self._setup_model(png_count=2)
        (reference / 'model_conversion/toy/toy-resources/tex1.png').unlink()
        with mock.patch.object(audit, 'run_capture') as capture:
            row = audit.audit_one('toy', {}, reference, index, pathlib.Path('capture.py'),
                                  pathlib.Path('chrome'), root / 'output', 60)
        self.assertEqual(row['status'], 'PNG_ERROR')
        self.assertIn('PNG missing for resx key 1', row['errors'])
        capture.assert_not_called()
        import shutil
        shutil.rmtree(root)

    def test_partial_corrupt_png_is_error_without_capture(self):
        root, reference, index = self._setup_model(png_count=2)
        (reference / 'model_conversion/toy/toy-resources/tex1.png').write_bytes(b'broken')
        with mock.patch.object(audit, 'run_capture') as capture:
            row = audit.audit_one('toy', {}, reference, index, pathlib.Path('capture.py'),
                                  pathlib.Path('chrome'), root / 'output', 60)
        self.assertEqual(row['status'], 'PNG_ERROR')
        self.assertEqual(row['png_resources'][1]['status'], 'PNG_DECODE_ERROR')
        capture.assert_not_called()
        import shutil
        shutil.rmtree(root)

    def test_empty_resx_uses_exact_external_texture_mapping(self):
        root, reference, index = self._setup_model()
        directory = reference / 'model_conversion/toy'
        (directory / 'toy.resx.json').write_text('{"Resources":{}}')
        (directory / 'toy.json').write_text('{"source":{"tex":{"texture":{"pixel":null}}}}')
        png = directory / 'toy_tex000.png'
        (directory / 'toy-resources/tex0.png').rename(png)
        sha = audit.decode_png_rgba8(png)[2]
        with mock.patch.object(audit, 'run_capture', return_value=(
                [{'width': 4, 'height': 4, 'bytes': 64, 'rgba_sha256': sha}],
                self._capture_detail())):
            row = audit.audit_one('toy', {}, reference, index, pathlib.Path('capture.py'),
                                  pathlib.Path('chrome'), root / 'output', 60)
        self.assertEqual(row['status'], 'MATCH')
        self.assertEqual(row['resource_mapping'], 'model_external_textures')
        self.assertEqual(row['png_resources'][0]['resx_key'], 'tex')
        import shutil
        shutil.rmtree(root)

    def test_empty_resx_missing_or_ambiguous_mapping_not_inferred_embedded(self):
        root, reference, index = self._setup_model()
        directory = reference / 'model_conversion/toy'
        (directory / 'toy.resx.json').write_text('{"Resources":{}}')
        for source in ({'source': {}}, {'source': {'tex': {'texture': {'pixel': None}}}}):
            (directory / 'toy.json').write_text(json.dumps(source))
            with mock.patch.object(audit, 'run_capture') as capture:
                row = audit.audit_one('toy', {}, reference, index, pathlib.Path('capture.py'),
                                      pathlib.Path('chrome'), root / 'output', 60)
            self.assertEqual(row['status'], 'SOURCE_MAPPING_UNRESOLVED')
            capture.assert_not_called()
        for name in ('a_tex000.png', 'b_tex000.png'):
            (directory / name).write_bytes(b'not decoded due to ambiguity')
        row = audit.audit_one('toy', {}, reference, index, pathlib.Path('capture.py'),
                              pathlib.Path('chrome'), root / 'output', 60)
        self.assertEqual(row['status'], 'SOURCE_MAPPING_UNRESOLVED')
        import shutil
        shutil.rmtree(root)

    def test_capture_with_wrong_psb_provenance_is_error(self):
        root, reference, index = self._setup_model()
        detail = self._capture_detail()
        detail['source_hashes']['psb'] = hashlib_sha256(b'other psb')
        sha = audit.decode_png_rgba8(reference / 'model_conversion/toy/toy-resources/tex0.png')[2]
        with mock.patch.object(audit, 'run_capture', return_value=(
                [{'width': 4, 'height': 4, 'bytes': 64, 'rgba_sha256': sha}], detail)):
            row = audit.audit_one('toy', {}, reference, index, pathlib.Path('capture.py'),
                                  pathlib.Path('chrome'), root / 'output', 60)
        self.assertEqual(row['status'], 'CAPTURE_PROVENANCE_ERROR')
        import shutil
        shutil.rmtree(root)


class CliParseTest(unittest.TestCase):
    """纯 CLI 解析契约，不需要真实素材或 GPU。"""

    def _catalog(self, root):
        (root / 'config').mkdir()
        (root / 'config/model-catalog.json').write_text('{"toy":{}}')

    def test_existing_output_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            self._catalog(root)
            output = root / 'output'
            output.mkdir()
            with mock.patch('sys.argv', ['audit_atlas_sources.py',
                                         '--reference-root', str(root),
                                         '--capture-tool', str(root / 'c.py'),
                                         '--browser', str(root / 'chrome'),
                                         '--output', str(output),
                                         '--report', str(root / 'r.json')]):
                stderr = io.StringIO()
                with contextlib.redirect_stderr(stderr), self.assertRaises(SystemExit) as ctx:
                    audit.main()
            self.assertNotEqual(ctx.exception.code, 0)
            self.assertIn('output already exists', stderr.getvalue())

    def test_existing_report_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            self._catalog(root)
            report = root / 'r.json'
            report.write_text('{}')
            with mock.patch('sys.argv', ['audit_atlas_sources.py',
                                         '--reference-root', str(root),
                                         '--capture-tool', str(root / 'c.py'),
                                         '--browser', str(root / 'chrome'),
                                         '--output', str(root / 'out'),
                                         '--report', str(report)]):
                stderr = io.StringIO()
                with contextlib.redirect_stderr(stderr), self.assertRaises(SystemExit) as ctx:
                    audit.main()
            self.assertNotEqual(ctx.exception.code, 0)
            self.assertIn('report already exists', stderr.getvalue())
            self.assertEqual(report.read_text(), '{}')
            self.assertFalse((root / 'out').exists())

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

    def test_final_nonmatch_exit_keeps_all_catalog_records(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            self._catalog(root)
            (root / 'config/model-catalog.json').write_text('{"bad":{},"good":{}}')
            capture = root / 'capture.py'
            browser = root / 'chrome'
            capture.write_text('synthetic')
            browser.write_text('synthetic')
            provenance = {'tool_hashes': {key: 'present' for key in (
                'capture_tool', 'browser_binary', 'FreeMoteDriver.js', 'emoteplayer.js',
                'capture.cjs', 'reference.html', 'trace.js', 'psb_fixture.py',
                'package.json', 'package-lock.json', 'node_binary')}}
            arguments = ['audit.py', '--reference-root', str(root), '--capture-tool', str(capture),
                         '--browser', str(browser), '--output', str(root / 'out'),
                         '--report', str(root / 'report.json')]
            rows = [OSError('unreadable selected source'),
                    {'model': 'good', 'status': 'MATCH', 'errors': [], 'commands': []}]
            with mock.patch('sys.argv', arguments), \
                    mock.patch.object(audit, 'collect_provenance', return_value=provenance), \
                    mock.patch.object(audit, 'audit_one', side_effect=rows), \
                    contextlib.redirect_stderr(io.StringIO()), \
                    contextlib.redirect_stdout(io.StringIO()), self.assertRaises(SystemExit) as ctx:
                audit.main()
            self.assertEqual(ctx.exception.code, 1)
            report = json.loads((root / 'report.json').read_text())
            self.assertTrue(report['complete'])
            self.assertEqual(report['catalog_records'], 2)
            self.assertEqual([r['model'] for r in report['models']], ['bad', 'good'])
            self.assertEqual(report['summary'], {'total': 2, 'ERROR': 1, 'MATCH': 1})
            self.assertIn('unreadable selected source', report['models'][0]['errors'][0])
            self.assertFalse((root / 'report.json.checkpoint').exists())


class CaptureResultTest(unittest.TestCase):
    def _result(self):
        return {'model': 'toy', 'glError': 0, 'atlasUploadHashes': [],
                'source_hashes': {'psb': hashlib_sha256(b'psb')},
                'renderer': 'synthetic', 'version': 'synthetic'}

    def test_capture_metadata_and_artifact_hashes_preserved(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            prefix = root / 'toy.capture'
            json_file = pathlib.Path(str(prefix) + '.json')
            json_file.write_text(json.dumps(self._result()))
            png_file = pathlib.Path(str(prefix) + '.png')
            png_file.write_bytes(b'synthetic image artifact')
            process = audit.subprocess.CompletedProcess([], 0, '', '')
            with mock.patch.object(audit.subprocess, 'run', return_value=process):
                uploads, detail = audit.run_capture(root / 'capture.py', root, 'toy',
                                                    root / 'chrome', prefix, 60)
            self.assertEqual(uploads, [])
            self.assertEqual(detail['metadata_sha256'], hashlib_sha256(json_file.read_bytes()))
            self.assertEqual(detail['png_sha256'], hashlib_sha256(png_file.read_bytes()))
            self.assertEqual(detail['renderer'], 'synthetic')
            self.assertEqual(detail['source_hashes'], self._result()['source_hashes'])
            self.assertIn('--trace-atlas-hashes', detail['command'])

    def test_missing_atlas_field_wrong_model_or_gl_error_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            prefix = root / 'toy.capture'
            process = audit.subprocess.CompletedProcess([], 0, '', '')
            invalid = [[], {**self._result(), 'model': 'other'},
                       {**self._result(), 'glError': 1281},
                       {**self._result(), 'atlasUploadHashes': None},
                       {**self._result(), 'source_hashes': None}]
            missing = self._result()
            del missing['atlasUploadHashes']
            invalid.append(missing)
            for result in invalid:
                with self.subTest(result=result):
                    pathlib.Path(str(prefix) + '.json').write_text(json.dumps(result))
                    with mock.patch.object(audit.subprocess, 'run', return_value=process):
                        uploads, detail = audit.run_capture(root / 'capture.py', root, 'toy',
                                                            root / 'chrome', prefix, 60)
                    self.assertIsNone(uploads)
                    self.assertIn('cannot parse capture output', detail['reason'])

    def test_dependency_and_browser_hashes_and_actual_command_recorded(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            capture = root / 'capture.py'
            browser = root / 'chrome'
            browser.write_bytes(b'fake browser')
            capture.write_bytes(b'fake capture')
            for filename in ('capture.cjs', 'reference.html', 'trace.js', 'psb_fixture.py',
                             'package.json', 'package-lock.json'):
                (root / filename).write_bytes(filename.encode())
            (root / 'driver').mkdir()
            for filename in ('FreeMoteDriver.js', 'emoteplayer.js'):
                (root / 'driver' / filename).write_bytes(filename.encode())
            process = audit.subprocess.CompletedProcess([], 0, 'version synthetic', '')
            with mock.patch.object(audit.subprocess, 'run', return_value=process), \
                    mock.patch('sys.argv', ['audit.py', '--timeout', '10']):
                provenance = audit.collect_provenance(capture, browser, root)
            for filename in ('capture.cjs', 'reference.html', 'trace.js', 'psb_fixture.py',
                             'package.json', 'package-lock.json'):
                self.assertEqual(provenance['tool_hashes'][filename],
                                 hashlib_sha256(filename.encode()))
            self.assertEqual(provenance['tool_hashes']['browser_binary'],
                             hashlib_sha256(b'fake browser'))
            self.assertEqual(provenance['tool_hashes']['FreeMoteDriver.js'],
                             hashlib_sha256(b'FreeMoteDriver.js'))
            self.assertEqual(provenance['command'][-2:], ['--timeout', '10'])


def hashlib_sha256(data):
    import hashlib
    return hashlib.sha256(data).hexdigest()


if __name__ == '__main__':
    unittest.main()
