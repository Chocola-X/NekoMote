#!/usr/bin/env python3
"""Synthetic request, provenance and response tests; no real assets or GPU."""
import copy
import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('g18_validator', ROOT / 'tools/touch_audit/g18_point_validator.py')
v = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(v)


class TestG18PointValidator(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.assets = self.root / 'assets'
        model_dir = self.assets / 'characters/fixture'
        model_dir.mkdir(parents=True)
        self.model = model_dir / 'model.nkm.json'
        self.session = model_dir / 'session.nks.json'
        self.probe = self.root / 'probe'
        self.probe.write_text('synthetic executable')
        self.texture = model_dir / 'texture.bin'
        self.texture.write_bytes(b'synthetic texture')
        self.author = {z: {'center': [0, y], 'radius': [r, r]} for z, y, r in
                       [('bust', 0, 30), ('eye', -100, 15), ('face', -100, 30),
                        ('head', -170, 30), ('pant', 150, 30)]}
        self.session.write_text(json.dumps({'session': {'model': 'fixture',
            'interaction': {'regions': self.author},
            'reactions': [{'label': f'fixture/{z}', 'zones': [z]} for z in v.ZONES]}}))
        self.model.write_text(json.dumps({'model': {'mirror': True,
            'author_scale': {'state': 2, 'value': 1}, 'textures': [{'path': 'texture.bin'}]}}))
        self.spec = {'model': 'fixture', 'condition': 'default_idle', 'png_point': [400, 940],
            'width': 800, 'height': 1080, 'origin': [0, 400], 'scale': 1,
            'steps': 0, 'seed': 1, 'reaction_seed': 1, 'expected_zone': 'bust',
            'expected_phase_in': [1, 2], 'expected_reaction_prefix': 'fixture/'}
        self.ledger = self.root / 'ledger.json'
        self.ledger.write_text(json.dumps({'probe': v.audit.file_record(self.probe), 'models': [{
            'model': 'fixture', 'status': 'OK', 'assets': [
                {**v.audit.file_record(path), 'role': role} for path, role in
                [(self.model, 'model_asset'), (self.session, 'session_asset'), (self.texture, 'texture_asset')]],
            'conditions': [{'name': name, 'pass': True, 'params': {'steps': steps,
                'origin': [0, 400], 'scale': 1, 'random_seed': 1, 'reaction_seed': 1}}
                for name, steps in [('default_idle', 0), ('idle_120', 120)]]}]}))
        self.output = self.root / 'output'
        self.mutate = lambda data: None

    def runner(self, command, **kwargs):
        request = json.loads(Path(command[-2]).read_text())
        step = request['steps']
        data = {'presentation_mirror': True, 'steps': step, 'samples': [
            {'step': i, 'phase': 1 if i == step else 0,
             'reaction': 'fixture/bust' if i == step else ''} for i in range(step + 1)],
            'touch_regions': v.audit.projected_regions(self.author,
                {z: [f'fixture/{z}'] for z in v.ZONES}, True, 1, request)}
        self.mutate(data)
        Path(command[-1]).write_text(json.dumps(data))
        return subprocess.CompletedProcess(command, 0, b'synthetic stdout', b'')

    def run_point(self, spec=None, runner=None):
        with patch.object(v.audit.subprocess, 'run', side_effect=runner or self.runner):
            return v.validate_point(spec or self.spec, self.output,
                probe=self.probe, assets=self.assets, ledger=self.ledger)

    def assert_rejected(self, message):
        result = self.run_point()
        self.assertFalse(result['ok'])
        self.assertTrue(any(message in error for error in result['errors']), result['errors'])
        return result

    def test_valid_hit_retains_request_response_process_and_hashes(self):
        result = self.run_point()
        self.assertTrue(result['ok'], result['errors'])
        self.assertTrue(result['validated'])
        for name in ('request.json', 'response.json', 'probe.command.json', 'probe.stdout.log',
                     'probe.stderr.log', 'result.json'):
            self.assertTrue((self.output / name).is_file())
        self.assertEqual(result['request']['sha256'], v.audit.sha256_file(self.output / 'request.json'))
        self.assertEqual(result['response']['sha256'], v.audit.sha256_file(self.output / 'response.json'))
        self.assertEqual(result['probe']['returncode'], 0)
        self.assertFalse(result['probe']['timeout'])

    def test_idle_120_dispatches_at_121_after_120_idle_advances(self):
        spec = {**self.spec, 'condition': 'idle_120', 'steps': 120}
        result = self.run_point(spec)
        self.assertTrue(result['ok'], result['errors'])
        request = json.loads((self.output / 'request.json').read_text())
        self.assertEqual(request['steps'], 121)
        self.assertEqual(request['events'][0]['step'], 121)
        self.assertEqual(request['random_seed'], 1)
        self.assertEqual(request['reaction_seed'], 1)

    def test_sample_99_cannot_substitute_for_target(self):
        self.mutate = lambda d: d.update(samples=[{'step': 99, 'phase': 1, 'reaction': 'fixture/bust'}])
        self.assert_rejected('incomplete sample sequence')

    def test_wrong_ordered_sample_cannot_pass(self):
        self.mutate = lambda d: d['samples'][1].update(step=99)
        self.assert_rejected('invalid sample sequence')

    def test_wrong_response_step_count_cannot_pass(self):
        self.mutate = lambda d: d.update(steps=99)
        self.assert_rejected('step count differs')

    def test_empty_region_set_cannot_pass(self):
        self.mutate = lambda d: d.update(touch_regions={})
        self.assert_rejected('incomplete projected region set')

    def test_null_expected_region_cannot_pass(self):
        self.mutate = lambda d: d['touch_regions'].update(bust=None)
        self.assert_rejected('projected region differs: bust')

    def test_bad_region_radius_rejected(self):
        self.mutate = lambda d: d['touch_regions']['bust'].update(radius=[0, 1])
        self.assert_rejected('invalid projected ellipse')

    def test_other_zone_reaction_with_correct_prefix_cannot_pass(self):
        self.mutate = lambda d: d['samples'][1].update(reaction='fixture/head')
        self.assert_rejected('allowed reactions')

    def test_eye_priority_is_not_face(self):
        spec = {**self.spec, 'png_point': [400, 840], 'expected_zone': 'face'}
        result = self.run_point(spec)
        self.assertFalse(result['ok'])
        self.assertIn('expected_zone differs from geometric hit priority', result['errors'])

    def test_wrong_phase_cannot_pass(self):
        self.mutate = lambda d: d['samples'][1].update(phase=0)
        self.assert_rejected('allowed reactions')

    def test_invalid_phase_enum_rejected(self):
        self.mutate = lambda d: d['samples'][1].update(phase=3)
        self.assert_rejected('invalid sample sequence')

    def test_busy_process_rejected_and_stderr_retained(self):
        result = self.run_point(runner=lambda c, **kw: subprocess.CompletedProcess(c, 1, b'', b'Busy'))
        self.assertFalse(result['ok'])
        self.assertEqual((self.output / 'probe.stderr.log').read_bytes(), b'Busy')
        self.assertEqual(result['probe']['returncode'], 1)

    def test_timeout_rejected_and_partial_logs_retained(self):
        def timeout(c, **kw):
            raise subprocess.TimeoutExpired(c, 30, output=b'partial', stderr=b'timeout')
        result = self.run_point(runner=timeout)
        self.assertFalse(result['ok'])
        self.assertTrue(result['probe']['timeout'])
        self.assertEqual((self.output / 'probe.stdout.log').read_bytes(), b'partial')

    def test_success_without_response_is_not_pass(self):
        result = self.run_point(runner=lambda c, **kw: subprocess.CompletedProcess(c, 0, b'', b''))
        self.assertFalse(result['ok'])
        self.assertFalse(result['validated'])

    def test_corrupt_response_not_pass(self):
        def corrupt(c, **kw):
            Path(c[-1]).write_text('bad JSON')
            return subprocess.CompletedProcess(c, 0, b'', b'')
        self.assertFalse(self.run_point(runner=corrupt)['ok'])

    def test_wrong_mirror_rejected(self):
        self.mutate = lambda d: d.update(presentation_mirror=False)
        self.assert_rejected('mirror differs')

    def test_nonidle_pre_touch_state_rejected(self):
        self.mutate = lambda d: d['samples'][0].update(phase=1, reaction='fixture/bust')
        self.assert_rejected('initial state not idle')

    def test_valid_miss_has_idle_phase_empty_reaction(self):
        spec = {**self.spec, 'png_point': [0, 0], 'expected_zone': None,
                'expected_phase_in': [0], 'expected_reaction_prefix': None}
        self.mutate = lambda d: d['samples'][1].update(phase=0, reaction='')
        result = self.run_point(spec)
        self.assertTrue(result['ok'], result['errors'])

    def test_miss_with_reaction_is_rejected(self):
        spec = {**self.spec, 'png_point': [0, 0], 'expected_zone': None,
                'expected_phase_in': [0], 'expected_reaction_prefix': None}
        result = self.run_point(spec)
        self.assertFalse(result['ok'])
        self.assertIn('miss requires idle phase and empty reaction', result['errors'])

    def test_existing_output_is_preserved(self):
        self.output.mkdir()
        (self.output / 'marker').write_text('keep')
        with self.assertRaises(FileExistsError):
            self.run_point()
        self.assertEqual((self.output / 'marker').read_text(), 'keep')

    def test_changed_frozen_probe_prevents_execution(self):
        self.probe.write_text('changed')
        with patch.object(v.audit.subprocess, 'run') as runner:
            result = v.validate_point(self.spec, self.output, probe=self.probe,
                                     assets=self.assets, ledger=self.ledger)
            runner.assert_not_called()
        self.assertFalse(result['ok'])
        self.assertIn('probe differs from frozen ledger hash', result['errors'])

    def test_changed_asset_prevents_execution(self):
        self.texture.write_text('changed')
        self.assert_rejected('texture_asset differs')

    def test_request_mutation_during_execution_rejected(self):
        def mutate(c, **kw):
            result = self.runner(c, **kw)
            Path(c[-2]).write_text('{}')
            return result
        result = self.run_point(runner=mutate)
        self.assertFalse(result['ok'])
        self.assertTrue(any('input changed' in e for e in result['errors']))

    def test_asset_mutation_during_execution_rejected(self):
        def mutate(c, **kw):
            result = self.runner(c, **kw)
            self.texture.write_bytes(b'mutated')
            return result
        result = self.run_point(runner=mutate)
        self.assertFalse(result['ok'])
        self.assertTrue(any('input changed' in e for e in result['errors']))

    def test_missing_spec_fields_rejected(self):
        for key in self.spec:
            if key == 'expected_reaction_prefix':
                continue
            spec = copy.deepcopy(self.spec)
            del spec[key]
            self.assertTrue(v.validate_spec(spec), key)

    def test_type_bounds_bool_nonfinite_and_enum_negatives(self):
        changes = [('width', True), ('height', 1.5), ('width', 0), ('height', 8193),
            ('width', '800'), ('scale', True), ('scale', float('nan')), ('scale', 0),
            ('steps', True), ('steps', -1), ('steps', 120), ('seed', 2**32),
            ('seed', False), ('reaction_seed', 1.2), ('origin', [0, float('inf')]),
            ('png_point', [True, 0]), ('png_point', [0, float('nan')]),
            ('condition', 'unknown'), ('expected_zone', 'unknown'),
            ('expected_phase_in', [True]), ('expected_phase_in', []),
            ('expected_phase_in', [0, 0]), ('model', '../fixture'), ('expected_reaction_prefix', 1)]
        for key, value in changes:
            with self.subTest(key=key, value=value):
                self.assertTrue(v.validate_spec({**self.spec, key: value}))
        self.assertTrue(v.validate_spec(None))

    def test_invalid_nonfinite_spec_produces_saved_rejection(self):
        result = self.run_point({**self.spec, 'png_point': [float('nan'), 0]})
        self.assertFalse(result['ok'])
        self.assertTrue((self.output / 'result.json').is_file())


if __name__ == '__main__':
    unittest.main()
