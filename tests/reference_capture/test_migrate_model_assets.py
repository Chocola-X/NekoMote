"""Current model container migration preserves all author data and sibling assets."""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

TOOLS = pathlib.Path(__file__).resolve().parents[2] / 'tools'
sys.path.insert(0, str(TOOLS))
import asset_resources as resources
import migrate_model_assets as migration
from model_container import read_model, write_model


class ModelMigrationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)
        self.source = self.root / 'assets'
        self.source.mkdir()
        self.output = self.root / 'new-assets'
        self.converter = self.root / 'converter'
        self.converter.write_bytes(b'native converter identity')
        self.documents = {}
        for costume in ('a', 'b'):
            bundle = self.source / 'characters/cat' / costume
            bundle.mkdir(parents=True)
            document = {'schema': 6, 'model': {'label': 'α/角色', 'mirror': False,
                        'large': 18446744073709551615, 'negative': -9223372036854775808,
                        'tracks': [[0, 0.125, None, True], [], {'unknown': 'author text'}],
                        'textures': [{'path': 'atlas.png', 'width': 2, 'height': 2}]},
                        'unknown_root': {'preserve': ['all', 'fields']}}
            (bundle / 'model.nkm.json').write_text(json.dumps(document, ensure_ascii=False))
            (bundle / 'atlas.png').write_bytes(b'original PNG bytes')
            (bundle / 'session.nks.json').write_bytes(b'{ "audio_path": "audio/voice.flac" }\n')
            (bundle / 'source.json').write_bytes(b'{ "source": "original.json" }\n')
            (bundle / 'audio').symlink_to('../audio/main')
            self.documents[costume] = document
        (self.source / 'characters/cat/audio/main').mkdir(parents=True)
        (self.source / 'characters/cat/audio/main/voice.flac').write_bytes(b'original FLAC')
        (self.source / 'characters/cat/audio/main/alias.flac').symlink_to('voice.flac')
        (self.source / 'unknown.bin').write_bytes(b'opaque user data')

    def native(self, command, **kwargs):
        source, target = map(pathlib.Path, command[1:])
        write_model(target, json.loads(source.read_text()))
        return types.SimpleNamespace(stdout='{"ok":true}')

    def run_migration(self, side_effect=None):
        with mock.patch.object(migration.subprocess, 'run', side_effect=side_effect or self.native):
            return migration.convert_tree(self.source, self.output, self.converter)

    def test_real_container_decode_matches_every_json_field_and_preserves_other_assets(self):
        before = resources.tree_ledger(self.source)
        result = self.run_migration()
        self.assertEqual(result['models'], 2)
        self.assertEqual(resources.tree_ledger(self.source), before)
        for costume, document in self.documents.items():
            source = self.source / 'characters/cat' / costume
            target = self.output / 'characters/cat' / costume
            self.assertEqual(read_model(target / 'model.nkm'), document)
            self.assertFalse((target / 'model.nkm.json').exists())
            for name in ('atlas.png', 'source.json', 'session.nks.json'):
                self.assertEqual((target / name).read_bytes(), (source / name).read_bytes())
            self.assertEqual(os.readlink(target / 'audio'), '../audio/main')
        self.assertEqual(os.readlink(self.output / 'characters/cat/audio/main/alias.flac'), 'voice.flac')
        self.assertEqual((self.output / 'unknown.bin').read_bytes(), b'opaque user data')
        resources.validate_links(self.output)
        moved = self.root / 'moved'
        self.output.rename(moved)
        self.assertEqual((moved / 'characters/cat/a/audio/alias.flac').read_bytes(), b'original FLAC')

    def test_any_changed_or_missing_field_prevents_publication(self):
        before = resources.tree_ledger(self.source)
        def changed(command, **kwargs):
            source, target = map(pathlib.Path, command[1:])
            document = json.loads(source.read_text())
            document.pop('unknown_root')
            write_model(target, document)
            return types.SimpleNamespace(stdout='{"ok":true}')
        with self.assertRaisesRegex(ValueError, 'complete original JSON'):
            self.run_migration(changed)
        self.assertEqual(resources.tree_ledger(self.source), before)
        self.assertFalse(os.path.lexists(self.output))
        self.assertFalse(list(self.root.glob('.nekomote-model-*')))

    def test_bool_and_numeric_kinds_are_not_silently_changed(self):
        def changed_kind(command, **kwargs):
            source, target = map(pathlib.Path, command[1:])
            document = json.loads(source.read_text())
            document['model']['mirror'] = 0  # False == 0 in Python, but not in the asset schema.
            write_model(target, document)
            return types.SimpleNamespace(stdout='{"ok":true}')
        with self.assertRaisesRegex(ValueError, 'complete original JSON'):
            self.run_migration(changed_kind)
        self.assertFalse(self.output.exists())

    def test_malformed_container_is_rejected_even_when_converter_claims_success(self):
        def corrupt(command, **kwargs):
            pathlib.Path(command[2]).write_bytes(b'NKMMPK01bad')
            return types.SimpleNamespace(stdout='{"ok":true}')
        with self.assertRaisesRegex(ValueError, 'container'):
            self.run_migration(corrupt)
        self.assertFalse(self.output.exists())

    def test_only_schema_6_is_supported(self):
        model = self.source / 'characters/cat/a/model.nkm.json'
        for schema in (5, 7, True):
            with self.subTest(schema=schema):
                model.write_text(json.dumps({'schema': schema, 'model': {}}))
                with self.assertRaisesRegex(ValueError, 'expected schema 6'):
                    self.run_migration()
                self.assertFalse(self.output.exists())

    def test_failed_converter_cleans_staging_and_keeps_source_unchanged(self):
        before = resources.tree_ledger(self.source)
        def failed(command, **kwargs):
            pathlib.Path(command[2]).write_bytes(b'partial output')
            raise subprocess.CalledProcessError(1, command)
        with self.assertRaises(subprocess.CalledProcessError):
            self.run_migration(failed)
        self.assertEqual(resources.tree_ledger(self.source), before)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob('.nekomote-model-*')))

    def test_existing_or_competing_output_is_never_replaced(self):
        self.output.symlink_to('absent')
        with self.assertRaisesRegex(ValueError, 'already exists'):
            self.run_migration()
        self.assertEqual(os.readlink(self.output), 'absent')
        self.output.unlink()
        def compete(command, **kwargs):
            if not self.output.exists():
                self.output.mkdir()
                (self.output / 'note').write_bytes(b'concurrent user data')
            return self.native(command, **kwargs)
        with self.assertRaises(FileExistsError):
            self.run_migration(compete)
        self.assertEqual((self.output / 'note').read_bytes(), b'concurrent user data')
        self.assertFalse((self.output / 'characters').exists())

    def test_existing_current_model_and_nested_output_are_rejected(self):
        current = self.source / 'characters/cat/a/model.nkm'
        current.write_bytes(b'preexisting current model')
        with self.assertRaisesRegex(ValueError, 'current model already exists'):
            self.run_migration()
        self.assertEqual(current.read_bytes(), b'preexisting current model')
        current.unlink()
        self.output = self.source / 'nested'
        with self.assertRaisesRegex(ValueError, 'outside'):
            self.run_migration()

    def test_concurrent_source_change_is_reported_and_preserved(self):
        def source_changed(command, **kwargs):
            (self.source / 'unknown.bin').write_bytes(b'user changed source')
            return self.native(command, **kwargs)
        with self.assertRaisesRegex(ValueError, 'source assets changed'):
            self.run_migration(source_changed)
        self.assertEqual((self.source / 'unknown.bin').read_bytes(), b'user changed source')
        self.assertFalse(self.output.exists())

    def test_user_report_filename_is_preserved(self):
        (self.source / 'model-migration-report.json').write_bytes(b'user report')
        result = self.run_migration()
        self.assertEqual((self.output / 'model-migration-report.json').read_bytes(), b'user report')
        self.assertNotEqual(result['report_file'], 'model-migration-report.json')
        self.assertTrue((self.output / result['report_file']).is_file())


if __name__ == '__main__':
    unittest.main()
