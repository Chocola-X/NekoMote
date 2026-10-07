"""Protect existing bundles and keep batch audit writes outside source materials."""
import importlib.util
import pathlib
import tempfile
import types
import unittest
from unittest import mock

TOOL = pathlib.Path(__file__).resolve().parents[2] / 'tools/import_all_assets.py'
SPEC = importlib.util.spec_from_file_location('import_all_assets', TOOL)
batch = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(batch)


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
                    row = batch.import_one('toy', {}, root / 'absent-reference',
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

            def competing_import(command, **kwargs):
                bundle = pathlib.Path(command[command.index('--output') + 1])
                bundle.mkdir()
                (bundle / 'model.nkm.json').write_text('{"schema":5}')
                target.mkdir()
                (target / 'user-note').write_bytes(b'preserve me')
                return types.SimpleNamespace(returncode=0, stdout='', stderr='')

            with mock.patch.object(batch.subprocess, 'run', side_effect=competing_import):
                row = batch.import_one('toy', {}, root / 'reference', root / 'importer',
                                       None, {}, output, staging, None, 120)
            self.assertEqual(row['status']['structure'], 'FAIL')
            self.assertEqual((target / 'user-note').read_bytes(), b'preserve me')
            self.assertFalse((target / 'model.nkm.json').exists())

    def test_cpu_report_is_written_to_explicit_staging_path(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            source = root / 'reference/model.json'
            destination = root / 'staging/cpu.json'
            with mock.patch.object(batch.subprocess, 'run',
                                   return_value=types.SimpleNamespace(returncode=2)) as run:
                result = batch.run_cpu_audit(root / 'auditor', source,
                                            source.with_suffix('.resx.json'), {}, destination, 120)
            self.assertEqual(result, 'FAIL')
            command = run.call_args.args[0]
            self.assertEqual(pathlib.Path(command[command.index('--output') + 1]), destination)
            self.assertFalse(destination.is_relative_to(source.parent))


if __name__ == '__main__':
    unittest.main()
