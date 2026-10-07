"""Verify batch exit status and source failures at the CLI boundary."""
import contextlib
import importlib.util
import io
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

TOOL = pathlib.Path(__file__).resolve().parents[2] / 'tools/import_all_assets.py'
SPEC = importlib.util.spec_from_file_location('batch_handoff', TOOL)
batch = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(batch)


class BatchHandoffTest(unittest.TestCase):
    def run_batch(self, root, catalog, **patches):
        reference = root / 'reference'
        (reference / 'config').mkdir(parents=True, exist_ok=True)
        (reference / 'config/model-catalog.json').write_text(json.dumps(catalog))
        importer = root / 'importer'
        importer.touch()
        report = root / 'report.json'
        args = [str(TOOL), '--reference-root', str(reference), '--importer', str(importer),
                '--output', str(root / 'output'), '--report', str(report)]
        with contextlib.ExitStack() as stack:
            stack.enter_context(mock.patch.object(sys, 'argv', args))
            stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
            stack.enter_context(contextlib.redirect_stderr(io.StringIO()))
            for name, value in patches.items():
                stack.enter_context(mock.patch.object(batch, name, return_value=value))
            with self.assertRaises(SystemExit) as error:
                batch.main()
        self.assertNotEqual(error.exception.code, 0)
        return json.loads(report.read_text()) if report.exists() else None

    def test_existing_output_is_a_nonzero_unchecked_result(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            target = root / 'output/toy'
            target.mkdir(parents=True)
            (target / 'user-note').write_text('preserve me')
            report = self.run_batch(root, {'toy': {}})
            self.assertEqual(report['models'][0]['status']['structure'], 'EXISTS_UNCHECKED')
            self.assertEqual((target / 'user-note').read_text(), 'preserve me')

    def test_cpu_timeout_fails_the_batch(self):
        row = {'model': 'toy', 'status': {'structure': 'OK', 'cpu': 'TIMEOUT',
               'visual': 'PENDING', 'interaction': 'PENDING', 'soak': 'PENDING'},
               'errors': ['CPU timeout']}
        with tempfile.TemporaryDirectory() as directory:
            report = self.run_batch(pathlib.Path(directory), {'toy': {}}, import_one=row)
            self.assertTrue(report['complete'])
            self.assertEqual(report['summary']['cpu']['TIMEOUT'], 1)

    def test_source_parse_failure_does_not_drop_remaining_rows(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / 'reference/model_conversion/bad/bad.json'
            source.parent.mkdir(parents=True)
            source.write_text('{broken')
            report = self.run_batch(root, {'bad': {}, 'missing': {}})
            self.assertTrue(report['complete'])
            self.assertEqual(len(report['models']), 2)
            self.assertEqual(report['models'][0]['status']['structure'], 'ERROR')
            self.assertIn('JSONDecodeError', report['models'][0]['errors'][0])
            self.assertEqual(report['models'][1]['status']['structure'], 'MISSING')

    def test_catalog_ids_cannot_escape_output_directory(self):
        for name in ('../outside', '/absolute', 'bad\\path', '.pure', '..'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                self.assertIsNone(self.run_batch(root, {name: {}}))
                self.assertFalse((root / 'output').exists())


if __name__ == '__main__':
    unittest.main()
