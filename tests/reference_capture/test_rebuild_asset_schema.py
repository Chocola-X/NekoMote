import importlib.util
import json
import pathlib
import tempfile
import unittest
from unittest import mock
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('rebuild_schema', ROOT / 'tools/rebuild_asset_schema.py')
rebuild = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(rebuild)


class SchemaMigrationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        self.assets, self.rebuilt, self.backup = [self.root / n for n in ('assets', 'new', 'backup')]
        self.original = {}
        for name in ('a', 'b'):
            for folder, schema in ((self.assets, 5), (self.rebuilt, 6)):
                bundle = folder / name
                bundle.mkdir(parents=True)
                Image.new('RGBA', (2, 2), (1, 2, 3, 255)).save(bundle / 'atlas.png')
                model = dict(mirror=False, textures=[dict(path='atlas.png')])
                if schema == 6:
                    model['mirrored_parameters'] = []
                (bundle / 'model.nkm.json').write_text(json.dumps(dict(schema=schema, model=model)))
            self.original[name] = (self.assets / name / 'model.nkm.json').read_bytes()

    def test_publishes_only_models_and_retains_backups(self):
        texture = (self.assets / 'a/atlas.png').read_bytes()
        self.assertEqual(rebuild.migrate(self.assets, self.rebuilt, self.backup), 2)
        self.assertEqual((self.assets / 'a/atlas.png').read_bytes(), texture)
        for name in ('a', 'b'):
            self.assertEqual((self.backup / (name + '.nkm.json')).read_bytes(), self.original[name])
            self.assertEqual(json.loads((self.assets / name / 'model.nkm.json').read_text())['schema'], 6)
        self.assertTrue(json.loads((self.backup / 'migration.json').read_text())['complete'])

    def test_changed_author_data_prevents_all_publication(self):
        p = self.rebuilt / 'b/model.nkm.json'
        data = json.loads(p.read_text())
        data['model']['mirror'] = True
        p.write_text(json.dumps(data))
        with self.assertRaises(ValueError):
            rebuild.migrate(self.assets, self.rebuilt, self.backup)
        self.assertFalse(self.backup.exists())
        self.assertEqual((self.assets / 'a/model.nkm.json').read_bytes(), self.original['a'])

    def test_changed_pixels_prevent_publication(self):
        Image.new('RGBA', (2, 2), (4, 5, 6, 255)).save(self.rebuilt / 'b/atlas.png')
        with self.assertRaises(ValueError):
            rebuild.migrate(self.assets, self.rebuilt, self.backup)
        self.assertFalse(self.backup.exists())

    def test_partial_publication_rolls_back(self):
        replace = rebuild.os.replace
        def fail_second(source, target):
            if target.parent.name == 'b':
                raise OSError('simulated publication failure')
            replace(source, target)
        with mock.patch.object(rebuild.os, 'replace', side_effect=fail_second):
            with self.assertRaises(OSError):
                rebuild.migrate(self.assets, self.rebuilt, self.backup)
        for name in ('a', 'b'):
            self.assertEqual((self.assets / name / 'model.nkm.json').read_bytes(), self.original[name])
