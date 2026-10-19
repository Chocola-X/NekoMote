"""Exercise offline rule conversion without external models or proprietary core."""
import json
import pathlib
import subprocess
import tempfile
import unittest
import wave

TOOL = pathlib.Path(__file__).resolve().parents[2] / 'tools/import_session/import_session.py'


class SessionImportTest(unittest.TestCase):
    def run_import(self, label='character/voice', voice_namespace='character', shared=False,
                   reject_namespace=False):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'config').mkdir()
            (root / 'model_conversion/character/model').mkdir(parents=True)
            audio_source = root / 'sounds' / voice_namespace / 'voice.wav'
            audio_source.parent.mkdir(parents=True)
            (root / 'config/model-catalog.json').write_text(json.dumps({
                'model': {'character': 'character', 'config': 'character.js'}}))
            (root / 'config/model-presentation.json').write_text('{}')
            (root / 'model_conversion/character/model/model.json').write_text(json.dumps({
                'metadata': {'charaProfile': {'pixelMarker': {'eye': -100, 'mouth': -40}},
                             'timelineControl': [{'label': 'idle'}, {'label': '差分用_waiting_loop'}],
                             'variableList': [{'label': 'head_LR'}]}}))
            (root / 'config/reaction-library.js').write_text('')
            item = {'id': label, 'duration': 100, 'recoveryDuration': 180,
                    'reaction': {'audio': audio_source.relative_to(root).as_posix(), 'mainTimelineLabel': 'absent',
                                 'variables': [{'name': 'head_LR', 'value': 10, 'duration': 120,
                                                'easing': -1}, {'name': 'absent', 'value': 3}]},
                    'recovery': {}, 'beats': [{'at': 50, 'mainTimelineLabel': 'idle'}]}
            config = {'bust': [item], 'head': [item]}
            (root / 'config/character.js').write_text('function getConfig(){return ' +
                                                     json.dumps(config) + ';}')
            with wave.open(str(audio_source), 'wb') as voice:
                voice.setnchannels(1)
                voice.setsampwidth(2)
                voice.setframerate(44100)
                voice.writeframes(b'\0\0' * 8820)
            converter = root / 'converter'
            converter.write_text('#!/usr/bin/env python3\n'
                                 'import json,pathlib,sys\n'
                                 'pathlib.Path(sys.argv[2]).write_bytes(b\'fLaC\' + '
                                 'pathlib.Path(sys.argv[1]).read_bytes())\n'
                                 'print(json.dumps({\'ok\':True}))\n')
            converter.chmod(0o755)
            output = root / 'out/character/teenage/session.nks.json'
            command = ['python3', str(TOOL), '--reference-root', str(root),
                                     '--model', 'model', '--output', str(output),
                                     '--converter', str(converter)]
            if shared:
                command.extend(['--character-root', str(output.parent.parent)])
            result = subprocess.run(command,
                                    capture_output=True, text=True)
            if label.startswith('..') or reject_namespace:
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((root / 'voice.wav').exists())
                if reject_namespace:
                    self.assertIn('source voice namespace', result.stderr)
                return
            self.assertEqual(result.returncode, 0, result.stderr)
            data = json.loads(output.read_text())['session']
            reaction = data['reactions'][0]
            self.assertEqual(len(data['reactions']), 1)
            self.assertEqual(reaction['zones'], ['bust', 'head'])
            self.assertEqual(reaction['duration_ticks'], 12)  # Actual PCM exceeds rule duration.
            self.assertEqual(reaction['recovery_ticks'], 11)
            self.assertEqual(reaction['beats'][0]['author_tick'], 3)
            self.assertEqual(reaction['action']['main_timeline'], '')
            self.assertAlmostEqual(reaction['action']['variables'][0]['author_frames'], 7.2)
            self.assertEqual(reaction['action']['variables'][0]['easing'], -1)
            self.assertEqual(data['interaction']['regions']['face']['center'], [0, -70])
            self.assertEqual((output.parent / reaction['audio_path']).read_bytes(),
                             b'fLaC' + audio_source.read_bytes())
            self.assertEqual(reaction['audio_path'], 'audio/voice.flac')
            self.assertEqual(len(json.loads(result.stdout)['warnings']), 2)
            expected_group = 'little' if voice_namespace == 'kocharacter' else 'main'
            self.assertEqual(json.loads(result.stdout)['audio_group'], expected_group)
            if shared:
                self.assertEqual((output.parent / 'audio').readlink().as_posix(),
                                 '../audio/' + expected_group)

    def test_real_pcm_duration_and_rule_bindings(self):
        self.run_import()

    def test_destination_traversal_is_rejected(self):
        self.run_import('../voice')

    def test_shared_normal_and_kitten_voices_follow_source_directory(self):
        self.run_import(shared=True)
        self.run_import(label='kocharacter/voice', voice_namespace='kocharacter', shared=True)

    def test_voice_id_cannot_disagree_with_source_audio_group(self):
        self.run_import(voice_namespace='kocharacter', reject_namespace=True)


if __name__ == '__main__':
    unittest.main()
