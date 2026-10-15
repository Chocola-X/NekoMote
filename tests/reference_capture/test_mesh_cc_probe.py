"""Isolation and failure reporting for independently authored cc hypotheses."""
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / 'tools/reference_capture'))
from probe_mesh_cc import cases, nonfinite_counts


class MeshCcProbeTests(unittest.TestCase):
    def test_factorial_controls_hold_coord_constant_and_isolate_cc_endpoint(self):
        candidate = [0.0] * 32
        inputs = cases(candidate)
        self.assertEqual(len(inputs), 15)
        self.assertEqual(len({c['name'] for c in inputs}), 15)
        for case in inputs:
            frames = case['document']['object']['test']['motion']['root']['layer'][0]['frameList']
            first, last = [frame['content'] for frame in frames[:2]]
            self.assertEqual(first['coord'], last['coord'])
            self.assertEqual(first['coord'], [0.0, 0.0, 0.0])
            if case['bp_mode'] == 'absent':
                self.assertIsNone(first['mesh']['bp'])
                self.assertIsNone(last['mesh']['bp'])
            elif case['bp_mode'] == 'identity':
                self.assertEqual(first['mesh']['bp'], last['mesh']['bp'])
            else:
                self.assertNotEqual(first['mesh']['bp'], last['mesh']['bp'])
            self.assertEqual(first['mesh']['cc'], candidate if case['cc_side'] in
                             ('left', 'both') else None)
            self.assertEqual(last['mesh']['cc'], candidate if case['cc_side'] in
                             ('right', 'both') else None)

    def test_generate_only_never_reports_capture_or_semantic_acceptance(self):
        script = pathlib.Path(__file__).resolve().parents[2] / 'tools/reference_capture/probe_mesh_cc.py'
        with tempfile.TemporaryDirectory() as root:
            output = pathlib.Path(root) / 'probe'
            # Synthetic provenance also works in CI's source snapshot without .git.
            source_commit = '0' * 40
            subprocess.run([sys.executable, str(script), '--output', str(output),
                            '--generate-only', '--cc-json', '0',
                            '--source-commit', source_commit], check=True, cwd=root)
            report = json.loads((output/'results.json').read_text())
            self.assertFalse(report['accepted_semantics'])
            self.assertEqual(report['candidate'], 0)
            self.assertEqual(report['source_commit'], source_commit)
            self.assertTrue(all(c['status'] == 'not_captured' for c in report['cases']))
            self.assertTrue(all((output/c['name']/'model.psb').is_file()
                                for c in report['cases']))
            repeat = subprocess.run([sys.executable, str(script), '--output', str(output),
                                     '--generate-only', '--source-commit', source_commit],
                                    capture_output=True, cwd=root)
            self.assertNotEqual(repeat.returncode, 0)
            for index, invalid in enumerate(['', 'not-a-commit']):
                destination = pathlib.Path(root) / f'invalid-commit-{index}'
                rejected = subprocess.run([sys.executable, str(script), '--output',
                    str(destination), '--generate-only', '--source-commit', invalid],
                    capture_output=True)
                self.assertNotEqual(rejected.returncode, 0)
                self.assertFalse(destination.exists())
            for index, invalid in enumerate(['NaN', '1e400', '[1e400]']):
                destination = pathlib.Path(root) / f'invalid-{index}'
                rejected = subprocess.run([sys.executable, str(script), '--output',
                    str(destination), '--generate-only', '--cc-json', invalid,
                    '--source-commit', source_commit],
                    capture_output=True)
                self.assertNotEqual(rejected.returncode, 0)
                self.assertFalse(destination.exists())

    def test_nan_and_infinities_keep_distinct_failure_counts(self):
        trace = [{'attributes': {'a_pos': {'data': [[None, None, None]],
                  'nonfinite': {'nan': 1, 'positiveInfinity': 1, 'negativeInfinity': 1}},
                  'a_texCoord': {'data': [[0, 32]]}}},
                 {'attributes': {'a_pos': {'nonfinite': {'nan': 2}}}}]
        self.assertEqual(nonfinite_counts(trace), dict(nan=3, positiveInfinity=1,
                                                       negativeInfinity=1))

    @unittest.skipUnless(shutil.which('node'), 'Node.js required for GPU observer regression')
    def test_observer_classifies_submitted_bytes_without_shader_inspection(self):
        here = pathlib.Path(__file__).resolve().parent
        trace = here.parents[1] / 'tools/reference_capture/trace.js'
        subprocess.run(['node', str(here/'trace_nonfinite.cjs'), str(trace)], check=True)


if __name__ == '__main__':
    unittest.main()
