"""V8 evidence acceptance contracts with synthetic observations; no assets or GPU."""
import copy
import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest


TOOL = pathlib.Path(__file__).resolve().parents[2] / 'tools/reference_capture/build_v8_evidence.py'
SPEC = importlib.util.spec_from_file_location('build_v8_evidence', TOOL)
v8 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(v8)


def observations():
    samples = [{'step': step, 'values': {'hair_UD': float(step), 'head_LR': 0.0}}
               for step in range(3)]
    return ({'steps': 2, 'variableSamples': samples},
            {'steps': 2, 'samples': copy.deepcopy(samples)})


class ScalarContractTest(unittest.TestCase):
    def test_updated_states_pass_with_explicit_initialization_boundary(self):
        reference, native = observations()
        native['samples'][0]['values']['hair_UD'] = .1
        result = v8.compare_scalar_samples(reference, native, 'ordinary')
        self.assertTrue(result['passed'])
        self.assertEqual(result['samples'], 4)
        self.assertFalse(result['initialization_boundary']['compared'])
        self.assertIn('Update', result['initialization_boundary']['reason'])
        native['samples'][1]['values']['hair_UD'] += .1
        result = v8.compare_scalar_samples(reference, native, 'ordinary')
        self.assertFalse(result['passed'])
        self.assertAlmostEqual(result['max_error'], .1)

    def test_initial_state_still_requires_valid_finite_values(self):
        reference, native = observations()
        native['samples'][0]['values']['hair_UD'] = float('nan')
        self.assertFalse(v8.compare_scalar_samples(reference, native, 'ordinary')['passed'])

    def test_truncated_native_and_reference_cannot_pass(self):
        for truncated in ('native', 'reference', 'both'):
            with self.subTest(truncated=truncated):
                reference, native = observations()
                if truncated in ('reference', 'both'):
                    reference['variableSamples'].pop()
                if truncated in ('native', 'both'):
                    native['samples'].pop()
                result = v8.compare_scalar_samples(reference, native, 'ordinary')
                self.assertFalse(result['passed'])
                self.assertIn('error', result)

    def test_absent_empty_and_incomplete_observations_fail(self):
        for mutation in ('missing_samples', 'empty_samples', 'empty_values',
                         'missing_native_key', 'dropped_reference_key', 'missing_requested_key'):
            with self.subTest(mutation=mutation):
                reference, native = observations()
                if mutation == 'missing_samples':
                    reference.pop('variableSamples')
                elif mutation == 'empty_samples':
                    reference['variableSamples'] = []
                elif mutation == 'empty_values':
                    reference['variableSamples'][1]['values'] = {}
                elif mutation == 'missing_native_key':
                    native['samples'][1]['values'].pop('hair_UD')
                elif mutation == 'dropped_reference_key':
                    reference['variableSamples'][1]['values'].pop('hair_UD')
                else:
                    reference['observeVariables'] = ['missing']
                self.assertFalse(v8.compare_scalar_samples(reference, native, 'ordinary')['passed'])

    def test_steps_and_step_order_must_match(self):
        for mutation in ('count', 'order', 'boolean_step', 'duplicate_step'):
            with self.subTest(mutation=mutation):
                reference, native = observations()
                if mutation == 'count':
                    native['steps'] = 1
                elif mutation == 'order':
                    native['samples'].reverse()
                elif mutation == 'boolean_step':
                    native['samples'][1]['step'] = True
                else:
                    reference['variableSamples'][2]['step'] = 1
                self.assertFalse(v8.compare_scalar_samples(reference, native, 'ordinary')['passed'])

    def test_nonfinite_or_non_numeric_values_fail(self):
        for value in (float('nan'), float('inf'), float('-inf'), None, '0', True):
            for side in ('native', 'reference'):
                with self.subTest(value=value, side=side):
                    reference, native = observations()
                    samples = native['samples'] if side == 'native' else reference['variableSamples']
                    samples[1]['values']['hair_UD'] = value
                    self.assertFalse(v8.compare_scalar_samples(reference, native, 'ordinary')['passed'])

    def test_only_two_documented_public_cache_exceptions_are_skipped(self):
        for case in v8.PUBLIC_SCALAR_SKIPS:
            reference, native = observations()
            native['samples'][1]['values']['head_LR'] = 20.0
            result = v8.compare_scalar_samples(reference, native, case)
            self.assertTrue(result['passed'])
            self.assertTrue(all(item['reason'] and item['variable'] == 'head_LR'
                                for item in result['skipped']))
            self.assertFalse(v8.compare_scalar_samples(reference, native, 'other')['passed'])
            native['samples'][1]['values']['hair_UD'] += .1
            self.assertFalse(v8.compare_scalar_samples(reference, native, case)['passed'])


class EvidenceAcceptanceTest(unittest.TestCase):
    def test_pose_needs_real_vertices_and_indices_and_finite_error(self):
        good = {'frame_error': 0, 'missing_primitives': [], 'frame_vertices': 3,
                'frame_indices': 3, 'frame_max_error_px': 0.0,
                'missing_reference_triangles': 0, 'unmatched_reference_vertices': 0,
                'unused_native_triangles': 0, 'unused_native_vertices': 0}
        self.assertTrue(v8.pose_geometry_passed(good))
        for key, value in (('frame_vertices', 0), ('frame_indices', 0),
                           ('frame_max_error_px', float('nan')),
                           ('missing_reference_triangles', 1)):
            with self.subTest(key=key):
                bad = dict(good, **{key: value})
                self.assertFalse(v8.pose_geometry_passed(bad))

    def test_counterexample_reproduction_is_a_failed_acceptance(self):
        case = {'type': 'playback', 'is_counterexample': True,
                'geometry': {'passed': True}, 'image': {'passed': True},
                'scalar': {'status': 'FAIL', 'passed': False, 'max_error': 1.7e-5}}
        result = v8.finalize_case(case)
        self.assertFalse(result['passed'])
        self.assertTrue(result['expected_failure_reproduced'])
        self.assertEqual(result['verdict'], 'DOCUMENTED_COUNTEREXAMPLE')
        self.assertFalse(v8.suite_acceptance([result]))
        result['scalar']['max_error'] = None
        self.assertFalse(v8.finalize_case(result)['expected_failure_reproduced'])

    def test_static_scalar_is_not_an_observation_and_dynamic_cannot_use_it(self):
        case = {'type': 'static', 'is_counterexample': False,
                'geometry': {'passed': True}, 'image': {'passed': True},
                'scalar': {'status': 'NOT_APPLICABLE', 'passed': None,
                           'samples': 0, 'max_error': None}}
        self.assertTrue(v8.finalize_case(case)['passed'])
        case['type'] = 'playback'
        self.assertFalse(v8.finalize_case(case)['passed'])
        self.assertFalse(v8.suite_acceptance([]))

    def test_cli_requires_new_output_without_touching_existing_evidence(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            marker = root / 'reference.png'
            marker.write_bytes(b'preserve this frozen input')
            result = subprocess.run([sys.executable, str(TOOL), '--output', name],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('existing evidence cannot be overwritten', result.stderr)
            self.assertEqual(marker.read_bytes(), b'preserve this frozen input')
            self.assertEqual(list(root.iterdir()), [marker])
            output = root / 'new-output'
            self.assertEqual(v8.new_output_directory(output), output)
            with self.assertRaises(FileExistsError):
                v8.new_output_directory(output)


if __name__ == '__main__':
    unittest.main()
