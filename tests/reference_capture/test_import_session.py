"""Exercise offline rule conversion without external models or proprietary core."""
import json
import pathlib
import subprocess
import tempfile
import unittest
import wave

TOOL = pathlib.Path(__file__).resolve().parents[2] / 'tools/import_session/import_session.py'


class SessionImportTest(unittest.TestCase):
    def run_import(self, label='character/voice'):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'config').mkdir()
            (root / 'model_conversion/character/model').mkdir(parents=True)
            (root / 'audio').mkdir()
            (root / 'config/model-catalog.json').write_text(json.dumps({
                'model': {'character': 'character', 'config': 'character.js'}}))
            (root / 'config/model-presentation.json').write_text('{}')
            (root / 'model_conversion/character/model/model.json').write_text(json.dumps({
                'metadata': {'charaProfile': {'pixelMarker': {'eye': -100, 'mouth': -40}},
                             'timelineControl': [{'label': 'idle'}, {'label': '差分用_waiting_loop'}],
                             'variableList': [{'label': 'head_LR'}]}}))
            (root / 'config/reaction-library.js').write_text('')
            item = {'id': label, 'duration': 100, 'recoveryDuration': 180,
                    'reaction': {'audio': 'audio/voice.wav', 'mainTimelineLabel': 'absent',
                                 'variables': [{'name': 'head_LR', 'value': 10, 'duration': 120,
                                                'easing': -1}, {'name': 'absent', 'value': 3}]},
                    'recovery': {}, 'beats': [{'at': 50, 'mainTimelineLabel': 'idle'}]}
            config = {'bust': [item], 'head': [item]}
            (root / 'config/character.js').write_text('function getConfig(){return ' +
                                                     json.dumps(config) + ';}')
            with wave.open(str(root / 'audio/voice.wav'), 'wb') as voice:
                voice.setnchannels(1)
                voice.setsampwidth(2)
                voice.setframerate(44100)
                voice.writeframes(b'\0\0' * 8820)
            output = root / 'out/session.nks.json'
            result = subprocess.run(['python3', str(TOOL), '--reference-root', str(root),
                                     '--model', 'model', '--output', str(output)],
                                    capture_output=True, text=True)
            if label.startswith('..'):
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((root / 'voice.wav').exists())
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
                             (root / 'audio/voice.wav').read_bytes())
            self.assertEqual(len(json.loads(result.stdout)['warnings']), 2)

    def test_real_pcm_duration_and_rule_bindings(self):
        self.run_import()

    def test_destination_traversal_is_rejected(self):
        self.run_import('../voice')


if __name__ == '__main__':
    unittest.main()
