"""Audio-only migration publication, source namespace grouping and relative links."""
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
import migrate_audio_assets as migration


def fake_convert(converter, source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(b'fLaC' + source.read_bytes())
    return {'ok': True}


class AudioMigrationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)
        self.source = self.root / 'assets'
        self.source.mkdir()
        self.output = self.root / 'new-assets'
        self.converter = self.root / 'converter'
        self.converter.write_bytes(b'converter identity')

    def model(self, costume, namespace='cat', special=False):
        bundle = self.source / 'characters/cat' / costume
        (bundle / 'textures').mkdir(parents=True)
        (bundle / 'textures/0.png').write_bytes(b'original PNG pixels')
        (bundle / 'model.nkm.json').write_bytes(b'{ "schema": 6, "author": "voice.wav" }\n')
        (bundle / 'source.json').write_bytes(b'{ "path": "original.wav" }\n')
        (bundle / 'audio').mkdir()
        (bundle / 'audio/a.wav').write_bytes(b'common voice')
        (bundle / 'audio/b.wav').write_bytes(b'special voice' if special else b'common voice')
        (bundle / 'session.nks.json').write_text(json.dumps({
            'session': {'reactions': [
                {'audio_path': 'audio/a.wav', 'duration_ticks': 51, 'label': namespace + '/a'},
                {'audio_path': 'audio/b.wav', 'duration_ticks': 21, 'label': namespace + '/b'}]}}))
        return bundle

    def run_migration(self):
        with mock.patch.object(migration, 'convert_audio', side_effect=fake_convert) as converter:
            result = migration.convert_tree(self.source, self.output, self.converter)
        return result, converter

    def test_shares_main_little_and_identical_files_without_modifying_png_or_model(self):
        one = self.model('kitten', 'cat')  # Costume intentionally contradicts voice namespace.
        self.model('teenage', 'kocat', True)
        self.model('maid', 'cat')
        (self.source / 'backgrounds').mkdir()
        (self.source / 'backgrounds/room.png').write_bytes(b'background PNG')
        (self.source / 'config.json').write_bytes(b'{ "background": "room.png" }\n')
        before = resources.tree_ledger(self.source)
        result, converter = self.run_migration()
        self.assertEqual(resources.tree_ledger(self.source), before)
        self.assertEqual(result['models'], 3)
        self.assertEqual(result['audio_files'], 6)
        self.assertEqual(result['unique_audio_conversions'], 2)
        self.assertEqual(converter.call_count, 2)
        role = self.output / 'characters/cat'
        self.assertEqual(os.readlink(role / 'kitten/audio'), '../audio/main')
        self.assertEqual(os.readlink(role / 'teenage/audio'), '../audio/little')
        self.assertEqual((role / 'maid/audio').resolve(), (role / 'kitten/audio').resolve())
        self.assertEqual(len([p for p in (role / 'audio').rglob('*.flac') if not p.is_symlink()]), 2)
        for relative in ('model.nkm.json', 'source.json', 'textures/0.png'):
            self.assertEqual((role / 'kitten' / relative).read_bytes(), (one / relative).read_bytes())
        for relative in ('backgrounds/room.png', 'config.json'):
            self.assertEqual((self.output / relative).read_bytes(), (self.source / relative).read_bytes())
        reaction = json.loads((role / 'kitten/session.nks.json').read_text())['session']['reactions'][0]
        self.assertEqual(reaction, {'audio_path': 'audio/a.flac', 'duration_ticks': 51, 'label': 'cat/a'})
        self.assertFalse(list(self.output.rglob('*.wav')))
        resources.validate_links(self.output)
        moved = self.root / 'moved'
        self.output.rename(moved)
        resources.validate_links(moved)
        self.assertEqual((moved / 'characters/cat/kitten/audio/a.flac').read_bytes(), b'fLaCcommon voice')
        self.assertIn('link', result['sha256']['characters/cat/kitten/audio'])

    def test_conflict_is_rejected_without_source_changes_or_partial_output(self):
        self.model('one')
        bundle = self.model('two')
        (bundle / 'audio/b.wav').write_bytes(b'different same namespace')
        before = resources.tree_ledger(self.source)
        with self.assertRaisesRegex(ValueError, 'audio group content mismatch'):
            self.run_migration()
        self.assertEqual(resources.tree_ledger(self.source), before)
        self.assertFalse(os.path.lexists(self.output))
        self.assertFalse(list(self.root.glob('.nekomote-audio-*')))

    def test_missing_audio_and_wrong_namespace_are_rejected(self):
        bundle = self.model('one', namespace='other')
        with self.assertRaisesRegex(ValueError, 'voice namespace'):
            self.run_migration()
        (bundle / 'audio/a.wav').unlink()
        with self.assertRaisesRegex(ValueError, 'missing audio'):
            self.run_migration()

    def test_existing_dangling_output_is_preserved(self):
        self.model('one')
        self.output.symlink_to('missing')
        with self.assertRaisesRegex(ValueError, 'exists'):
            self.run_migration()
        self.assertEqual(os.readlink(self.output), 'missing')

    def test_nested_output_and_escaping_input_links_are_rejected(self):
        self.model('one')
        self.output = self.source / 'nested'
        with self.assertRaisesRegex(ValueError, 'outside'):
            self.run_migration()
        self.output = self.root / 'new-assets'
        (self.source / 'outside').symlink_to('../converter')
        with self.assertRaisesRegex(ValueError, 'escapes'):
            self.run_migration()

    def test_path_traversal_is_rejected(self):
        bundle = self.model('one')
        document = json.loads((bundle / 'session.nks.json').read_text())
        document['session']['reactions'][0]['audio_path'] = 'audio/../../outside.wav'
        (bundle / 'session.nks.json').write_text(json.dumps(document))
        with self.assertRaisesRegex(ValueError, 'unsafe asset path'):
            self.run_migration()

    def test_existing_flac_collision_is_preserved(self):
        bundle = self.model('one')
        (bundle / 'audio/a.flac').write_bytes(b'preserve existing')
        with self.assertRaisesRegex(ValueError, 'non-WAV'):
            self.run_migration()
        self.assertEqual((bundle / 'audio/a.flac').read_bytes(), b'preserve existing')

    def test_voice_namespace_is_strict(self):
        self.assertEqual(resources.audio_group_name('cat', ['cat']), 'main')
        self.assertEqual(resources.audio_group_name('cat', ['kocat']), 'little')
        for values in ([], ['cat', 'kocat'], ['cat-little'], ['other']):
            with self.subTest(values=values), self.assertRaises(ValueError):
                resources.audio_group_name('cat', values)


class ConverterContractTests(unittest.TestCase):
    def test_failed_native_tool_does_not_delete_another_writer_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = pathlib.Path(temporary) / 'voice.flac'
            def failure(command, **kwargs):
                output.write_bytes(b'other writer')
                raise subprocess.CalledProcessError(1, command)
            with mock.patch.object(resources.subprocess, 'run', side_effect=failure):
                with self.assertRaises(subprocess.CalledProcessError):
                    resources.convert_audio('converter', 'voice.wav', output)
            self.assertEqual(output.read_bytes(), b'other writer')

    def test_invalid_success_metadata_is_rejected_and_owned_file_removed(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = pathlib.Path(temporary) / 'voice.flac'
            def malformed(command, **kwargs):
                output.write_bytes(b'incomplete')
                return types.SimpleNamespace(stdout='{"ok":false}')
            with mock.patch.object(resources.subprocess, 'run', side_effect=malformed):
                with self.assertRaisesRegex(ValueError, 'verify'):
                    resources.convert_audio('converter', 'voice.wav', output)
            self.assertFalse(output.exists())

    def test_nested_link_targets_cannot_escape_character(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / 'role/model').mkdir(parents=True)
            (root / 'role/audio/main').mkdir(parents=True)
            (root / 'outside.flac').write_bytes(b'outside')
            (root / 'role/model/audio').symlink_to('../audio/main')
            (root / 'role/audio/main/voice.flac').symlink_to('../../../outside.flac')
            with self.assertRaisesRegex(ValueError, 'escapes'):
                resources.tree_ledger(root / 'role/model', root / 'role')


if __name__ == '__main__':
    unittest.main()
