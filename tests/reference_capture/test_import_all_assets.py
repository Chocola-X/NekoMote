"""Protect existing bundles and keep batch audit writes outside source materials.

覆盖 G7 要求的保护契约与导入负例：
- 已有输出（partial/旧schema/文件/悬空链接）一律保留并 EXISTS_UNCHECKED
- 原子发布不替换已有目录
- 新 bundle 通过 rename 发布
- 竞争创建目标时保留用户文件
- CPU 报告写到显式 staging 路径，不写源目录
- 歧义/缺失候选显式拒绝
- 缺 resx/纹理/会话失败保留原因
- 导入失败/超时保留命令与原因
- 检查点原子写入
- summary 结构/CPU/视觉/交互/soak 分别统计，未运行保持 PENDING
"""
import importlib.util
import json
import pathlib
import tempfile
import types
import unittest
from unittest import mock

TOOL = pathlib.Path(__file__).resolve().parents[2] / 'tools/import_all_assets.py'
SPEC = importlib.util.spec_from_file_location('import_all_assets', TOOL)
batch = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(batch)

INDEX_TOOL = pathlib.Path(__file__).resolve().parents[2] / 'tools/import_emote/catalog_index.py'
INDEX_SPEC = importlib.util.spec_from_file_location('catalog_index', INDEX_TOOL)
catalog_index = importlib.util.module_from_spec(INDEX_SPEC)
INDEX_SPEC.loader.exec_module(catalog_index)


def make_index_with(reference_root, name, source_path):
    """构造一个包含单个候选的 index dict。"""
    index = {}
    key = name.removesuffix('.pure')
    index[key] = [source_path]
    return index


class BatchImportSafetyTest(unittest.TestCase):
    def test_existing_outputs_are_preserved_without_certification(self):
        for kind in ('partial', 'old_schema', 'file', 'dangling_symlink'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as name:
                root = pathlib.Path(name)
                target = root / 'output/toy'
                target.parent.mkdir()
                if kind == 'file':
                    target.write_bytes(b'user file')
                elif kind == 'dangling_symlink':
                    target.symlink_to(root / 'absent')
                else:
                    target.mkdir()
                    (target / 'user-note').write_bytes(b'preserve me')
                    if kind == 'old_schema':
                        (target / 'model.nkm.json').write_text('{"schema":0}')
                with mock.patch.object(batch.subprocess, 'run') as run:
                    row = batch.import_one('toy', {}, root / 'absent-reference', {},
                                           root / 'absent-importer', None, {}, target.parent,
                                           root / 'staging', None, 120)
                self.assertEqual(row['status']['structure'], 'EXISTS_UNCHECKED')
                run.assert_not_called()
                if kind == 'file':
                    self.assertEqual(target.read_bytes(), b'user file')
                elif kind == 'dangling_symlink':
                    self.assertTrue(target.is_symlink())
                else:
                    self.assertEqual((target / 'user-note').read_bytes(), b'preserve me')

    def test_atomic_publication_does_not_replace_existing_directory(self):
        for populated in (False, True):
            with self.subTest(populated=populated), tempfile.TemporaryDirectory() as name:
                root = pathlib.Path(name)
                staging, target = root / 'staging', root / 'target'
                staging.mkdir()
                target.mkdir()
                (staging / 'asset').write_bytes(b'new')
                if populated:
                    (target / 'user-note').write_bytes(b'old')
                with self.assertRaises(FileExistsError):
                    batch.publish_bundle(staging, target)
                self.assertTrue(target.is_dir())
                self.assertEqual((staging / 'asset').read_bytes(), b'new')
                self.assertFalse((target / 'asset').exists())
                if populated:
                    self.assertEqual((target / 'user-note').read_bytes(), b'old')

    def test_new_bundle_is_published_by_rename(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            staging, target = root / 'staging', root / 'target'
            staging.mkdir()
            (staging / 'asset').write_bytes(b'new')
            inode = staging.stat().st_ino
            batch.publish_bundle(staging, target)
            self.assertFalse(staging.exists())
            self.assertEqual(target.stat().st_ino, inode)
            self.assertEqual((target / 'asset').read_bytes(), b'new')

    def test_target_created_during_import_is_preserved(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            source = root / 'reference/model_conversion/toy/toy.json'
            source.parent.mkdir(parents=True)
            source.write_text('{"source":{}}')
            output, staging = root / 'output', root / 'staging'
            output.mkdir()
            staging.mkdir()
            target = output / 'toy'
            index = make_index_with(root / 'reference', 'toy', source)

            def competing_import(command, **kwargs):
                bundle = pathlib.Path(command[command.index('--output') + 1])
                bundle.mkdir()
                (bundle / 'model.nkm.json').write_text('{"schema":5}')
                target.mkdir()
                (target / 'user-note').write_bytes(b'preserve me')
                return types.SimpleNamespace(returncode=0, stdout='', stderr='')

            with mock.patch.object(batch.subprocess, 'run', side_effect=competing_import):
                row = batch.import_one('toy', {}, root / 'reference', index,
                                       root / 'importer', None, {}, output, staging, None, 120)
            self.assertEqual(row['status']['structure'], 'FAIL')
            self.assertEqual((target / 'user-note').read_bytes(), b'preserve me')
            self.assertFalse((target / 'model.nkm.json').exists())

    def test_cpu_report_is_written_to_explicit_staging_path(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            source = root / 'reference/model.json'
            destination = root / 'staging/cpu.json'
            with mock.patch.object(batch.subprocess, 'run',
                                   return_value=types.SimpleNamespace(
                                       returncode=2, stdout='', stderr='audit fail')) as run:
                result, detail = batch.run_cpu_audit(
                    root / 'auditor', source, source.with_suffix('.resx.json'),
                    {}, destination, 120)
            self.assertEqual(result, 'FAIL')
            command = run.call_args.args[0]
            self.assertEqual(pathlib.Path(command[command.index('--output') + 1]), destination)
            self.assertFalse(destination.is_relative_to(source.parent))


class CandidateResolutionTest(unittest.TestCase):
    """歧义和缺失候选显式拒绝，不静默选择首个。"""

    def test_ambiguous_candidates_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            reference = root / 'reference'
            source_a = reference / 'model_conversion/toy_a/toy.json'
            source_b = reference / 'model_conversion/toy_b/toy.json'
            source_a.parent.mkdir(parents=True)
            source_b.parent.mkdir(parents=True)
            source_a.write_text('{"source":{}}')
            source_b.write_text('{"source":{}}')
            index = catalog_index.build_index(reference)
            output, staging = root / 'output', root / 'staging'
            output.mkdir()
            staging.mkdir()
            row = batch.import_one('toy', {}, reference, index,
                                   root / 'importer', None, {}, output, staging, None, 120)
            self.assertEqual(row['status']['structure'], 'AMBIGUOUS')
            self.assertEqual(len(row['alternatives']), 2)
            self.assertTrue(row['errors'])

    def test_missing_candidate_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            reference = root / 'reference'
            (reference / 'model_conversion').mkdir(parents=True)
            index = catalog_index.build_index(reference)
            output, staging = root / 'output', root / 'staging'
            output.mkdir()
            staging.mkdir()
            row = batch.import_one('absent', {}, reference, index,
                                   root / 'importer', None, {}, output, staging, None, 120)
            self.assertEqual(row['status']['structure'], 'MISSING')
            self.assertTrue(row['errors'])

    def test_canonical_parent_name_preferred(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            reference = root / 'reference'
            canonical = reference / 'model_conversion/toy/toy.json'
            sibling = reference / 'model_conversion/group/toy.json'
            canonical.parent.mkdir(parents=True)
            sibling.parent.mkdir(parents=True)
            canonical.write_text('{"source":{}}')
            sibling.write_text('{"source":{}}')
            index = catalog_index.build_index(reference)
            candidates = catalog_index.resolve_candidates(index, 'toy')
            self.assertEqual(len(candidates), 1)
            self.assertEqual(candidates[0], canonical)


class ImportFailureRecordingTest(unittest.TestCase):
    """导入失败/超时保留命令、节点与原因；bundle 不发布。"""

    def _setup_single(self):
        root = pathlib.Path(tempfile.mkdtemp())
        reference = root / 'reference'
        source = reference / 'model_conversion/toy/toy.json'
        source.parent.mkdir(parents=True)
        source.write_text('{"source":{}}')
        index = make_index_with(reference, 'toy', source)
        output, staging = root / 'output', root / 'staging'
        output.mkdir()
        staging.mkdir()
        return root, reference, source, index, output, staging

    def test_importer_failure_records_command_and_error(self):
        root, reference, source, index, output, staging = self._setup_single()
        with mock.patch.object(batch.subprocess, 'run',
                               return_value=types.SimpleNamespace(returncode=1, stdout='', stderr='bad model')):
            row = batch.import_one('toy', {}, reference, index,
                                   root / 'importer', None, {}, output, staging, None, 120)
        self.assertEqual(row['status']['structure'], 'FAIL')
        self.assertTrue(row['commands'])
        self.assertEqual(row['commands'][0][0], str(root / 'importer'))
        self.assertIn('bad model', row['errors'][0])
        self.assertFalse((output / 'toy').exists())
        import shutil
        shutil.rmtree(root)

    def test_importer_timeout_records_command_and_reason(self):
        root, reference, source, index, output, staging = self._setup_single()
        with mock.patch.object(batch.subprocess, 'run',
                               side_effect=subprocess_timeout('importer timeout')):
            row = batch.import_one('toy', {}, reference, index,
                                   root / 'importer', None, {}, output, staging, None, 120)
        self.assertEqual(row['status']['structure'], 'TIMEOUT')
        self.assertTrue(row['commands'])
        self.assertIn('timeout', row['errors'][0].lower())
        self.assertFalse((output / 'toy').exists())
        import shutil
        shutil.rmtree(root)

    def test_missing_model_manifest_after_success_is_fail(self):
        root, reference, source, index, output, staging = self._setup_single()

        def succeed_without_manifest(command, **kwargs):
            bundle = pathlib.Path(command[command.index('--output') + 1])
            bundle.mkdir()
            return types.SimpleNamespace(returncode=0, stdout='', stderr='')

        with mock.patch.object(batch.subprocess, 'run', side_effect=succeed_without_manifest):
            row = batch.import_one('toy', {}, reference, index,
                                   root / 'importer', None, {}, output, staging, None, 120)
        self.assertEqual(row['status']['structure'], 'FAIL')
        self.assertIn('model.nkm.json missing', row['errors'][0])
        import shutil
        shutil.rmtree(root)

    def test_session_failure_records_command_and_error(self):
        root, reference, source, index, output, staging = self._setup_single()

        calls = []

        def import_then_session(command, **kwargs):
            calls.append(command)
            if len(calls) == 1:
                bundle = pathlib.Path(command[command.index('--output') + 1])
                bundle.mkdir()
                (bundle / 'model.nkm.json').write_text('{"schema":5}')
                return types.SimpleNamespace(returncode=0, stdout='', stderr='')
            return types.SimpleNamespace(returncode=1, stdout='', stderr='session parse error')

        with mock.patch.object(batch.subprocess, 'run', side_effect=import_then_session):
            row = batch.import_one('toy', {}, reference, index,
                                   root / 'importer', root / 'session_tool', {},
                                   output, staging, None, 120)
        self.assertEqual(row['status']['structure'], 'SESSION_FAIL')
        self.assertEqual(len(row['commands']), 2)
        self.assertIn('session parse error', row['errors'][0])
        self.assertFalse((output / 'toy').exists())
        import shutil
        shutil.rmtree(root)


class CheckpointAndSummaryTest(unittest.TestCase):
    """检查点原子写入；summary 结构/CPU/视觉/交互/soak 分别统计。"""

    def test_save_checkpoint_writes_valid_json(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            checkpoint = root / 'ledger.json'
            report = {'complete': False, 'models': []}
            batch.save_checkpoint(report, checkpoint)
            loaded = json.loads(checkpoint.read_text())
            self.assertFalse(loaded['complete'])
            self.assertEqual(loaded['models'], [])

    def test_save_checkpoint_replaces_existing(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            checkpoint = root / 'ledger.json'
            checkpoint.write_text('{"old": true}')
            batch.save_checkpoint({'complete': True, 'models': [1, 2]}, checkpoint)
            loaded = json.loads(checkpoint.read_text())
            self.assertTrue(loaded['complete'])
            self.assertEqual(loaded['models'], [1, 2])

    def test_summary_counts_all_five_status_fields(self):
        rows = [
            {'status': {'structure': 'OK', 'cpu': 'OK', 'visual': 'PENDING',
                        'interaction': 'PENDING', 'soak': 'PENDING'}},
            {'status': {'structure': 'FAIL', 'cpu': 'PENDING', 'visual': 'PENDING',
                        'interaction': 'PENDING', 'soak': 'PENDING'}},
            {'status': {'structure': 'OK', 'cpu': 'FAIL', 'visual': 'PENDING',
                        'interaction': 'PENDING', 'soak': 'PENDING'}},
        ]
        summary = batch.build_summary(rows)
        self.assertEqual(summary['total'], 3)
        self.assertEqual(summary['structure']['OK'], 2)
        self.assertEqual(summary['structure']['FAIL'], 1)
        self.assertEqual(summary['cpu']['OK'], 1)
        self.assertEqual(summary['cpu']['FAIL'], 1)
        self.assertEqual(summary['cpu']['PENDING'], 1)
        self.assertEqual(summary['visual']['PENDING'], 3)
        self.assertEqual(summary['interaction']['PENDING'], 3)
        self.assertEqual(summary['soak']['PENDING'], 3)

    def test_successful_import_records_schema_and_bundle_hash(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            reference = root / 'reference'
            source = reference / 'model_conversion/toy/toy.json'
            source.parent.mkdir(parents=True)
            source.write_text('{"source":{}}')
            index = make_index_with(reference, 'toy', source)
            output, staging = root / 'output', root / 'staging'
            output.mkdir()
            staging.mkdir()

            def fake_import(command, **kwargs):
                bundle = pathlib.Path(command[command.index('--output') + 1])
                bundle.mkdir()
                (bundle / 'model.nkm.json').write_text('{"schema":5}')
                (bundle / 'texture.png').write_bytes(b'\x89PNG')
                return types.SimpleNamespace(returncode=0, stdout='', stderr='')

            with mock.patch.object(batch.subprocess, 'run', side_effect=fake_import):
                row = batch.import_one('toy', {}, reference, index,
                                       root / 'importer', None, {}, output, staging, None, 120)
            self.assertEqual(row['status']['structure'], 'OK')
            self.assertEqual(row['schema'], 5)
            self.assertIn('model.nkm.json', row['bundle_sha256'])
            self.assertIn('texture.png', row['bundle_sha256'])


class CatalogIndexTest(unittest.TestCase):
    """共享 catalog_index 模块的核心契约。"""

    def test_load_catalog_rejects_missing_file(self):
        with tempfile.TemporaryDirectory() as name:
            with self.assertRaises(FileNotFoundError):
                catalog_index.load_catalog(pathlib.Path(name))

    def test_build_index_skips_resx_files(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            conv = root / 'model_conversion/toy'
            conv.mkdir(parents=True)
            (conv / 'toy.json').write_text('{}')
            (conv / 'toy.resx.json').write_text('{}')
            index = catalog_index.build_index(root)
            self.assertEqual(len(index['toy']), 1)
            self.assertTrue(index['toy'][0].name.endswith('toy.json'))

    def test_build_index_strips_pure_suffix(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            conv = root / 'model_conversion/toy'
            conv.mkdir(parents=True)
            (conv / 'toy.pure.json').write_text('{}')
            index = catalog_index.build_index(root)
            self.assertIn('toy', index)

    def test_find_external_textures_returns_none_on_ambiguity(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            source = root / 'model.json'
            source.write_text(json.dumps({
                'source': {'tex000': {'texture': {'pixel': None}}}}))
            (root / 'atlas0-tex000.png').write_bytes(b'')
            (root / 'atlas1-tex000.png').write_bytes(b'')
            external = catalog_index.find_external_textures(source, root)
            self.assertIsNone(external['tex000'])

    def test_find_external_textures_skips_inline_pixel(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            source = root / 'model.json'
            source.write_text(json.dumps({
                'source': {'tex000': {'texture': {'pixel': 'base64data'}}}}))
            external = catalog_index.find_external_textures(source, root)
            self.assertEqual(external, {})

    def test_relative_to_fallback_for_external_path(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            external = pathlib.Path('/tmp/elsewhere')
            self.assertEqual(catalog_index.relative_to(external, root), '/tmp/elsewhere')


def subprocess_timeout(reason):
    import subprocess as _sp
    return _sp.TimeoutExpired(cmd=['x'], timeout=120)


class CliParseTest(unittest.TestCase):
    """纯 CLI 解析契约，不需要真实素材或 GPU。

    覆盖 G10 要求：缺素材环境的纯 CLI 解析测试也应执行，
    只有真实 GPU/素材检查 Skipped，不得把整类跳过误报为测试通过。
    """

    def test_runtime_steps_range_rejects_zero(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            catalog = root / 'config/model-catalog.json'
            catalog.parent.mkdir(parents=True)
            catalog.write_text('{}')
            importer = root / 'importer'
            importer.write_bytes(b'')
            with mock.patch('sys.argv', ['import_all_assets.py',
                                         '--reference-root', str(root),
                                         '--importer', str(importer),
                                         '--output', str(root / 'out'),
                                         '--runtime-steps', '0']):
                with self.assertRaises(SystemExit) as ctx:
                    batch.main()
            self.assertNotEqual(ctx.exception.code, 0)

    def test_runtime_steps_range_rejects_too_large(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            catalog = root / 'config/model-catalog.json'
            catalog.parent.mkdir(parents=True)
            catalog.write_text('{}')
            importer = root / 'importer'
            importer.write_bytes(b'')
            with mock.patch('sys.argv', ['import_all_assets.py',
                                         '--reference-root', str(root),
                                         '--importer', str(importer),
                                         '--output', str(root / 'out'),
                                         '--runtime-steps', '36001']):
                with self.assertRaises(SystemExit) as ctx:
                    batch.main()
            self.assertNotEqual(ctx.exception.code, 0)

    def test_missing_catalog_rejects_with_nonzero(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            importer = root / 'importer'
            importer.write_bytes(b'')
            with mock.patch('sys.argv', ['import_all_assets.py',
                                         '--reference-root', str(root),
                                         '--importer', str(importer),
                                         '--output', str(root / 'out')]):
                with self.assertRaises(SystemExit) as ctx:
                    batch.main()
            self.assertNotEqual(ctx.exception.code, 0)

    def test_missing_importer_rejects_with_nonzero(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            catalog = root / 'config/model-catalog.json'
            catalog.parent.mkdir(parents=True)
            catalog.write_text('{}')
            with mock.patch('sys.argv', ['import_all_assets.py',
                                         '--reference-root', str(root),
                                         '--importer', str(root / 'absent'),
                                         '--output', str(root / 'out')]):
                with self.assertRaises(SystemExit) as ctx:
                    batch.main()
            self.assertNotEqual(ctx.exception.code, 0)

    def test_tool_hash_returns_none_for_missing(self):
        self.assertIsNone(batch.tool_hash(None))
        self.assertIsNone(batch.tool_hash(pathlib.Path('/nonexistent/path')))

    def test_tool_hash_returns_sha256_for_file(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            tool = root / 'tool'
            tool.write_bytes(b'binary content')
            digest = batch.tool_hash(tool)
            self.assertIsNotNone(digest)
            self.assertEqual(len(digest), 64)

    def test_head_commit_returns_string_or_none(self):
        commit = batch.head_commit()
        if commit is not None:
            self.assertEqual(len(commit), 40)
            self.assertTrue(all(c in '0123456789abcdef' for c in commit))


if __name__ == '__main__':
    import subprocess
    unittest.main()
