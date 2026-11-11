"""Installer transactions against temporary user roots and mocked host commands."""
import hashlib
import importlib.util
import json
import os
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / 'tools/installer.py'
SPEC = importlib.util.spec_from_file_location('installer', SCRIPT)
INSTALLER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSTALLER)


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.prefix = self.base / 'data'
        self.config = self.base / 'config'
        self.config.mkdir()
        self.prefix.mkdir()
        self.calls = []
        self.fail = False
        self.package = self.base / 'nekomote.wallpaper.zip'
        self.payload = {
            'metadata.json': json.dumps({'KPackageStructure': 'Plasma/Wallpaper',
                                         'KPlugin': {'Id': INSTALLER.PACKAGE_ID, 'Version': '1.2.3'}}).encode(),
            'contents/ui/main.qml': b'import "NekoMote/Wallpaper" 1.0\n',
            'contents/ui/config.qml': b'import "NekoMote/Wallpaper" 1.0\n',
            'contents/config/main.xml': b'<kcfg/>',
            'contents/ui/NekoMote/Wallpaper/qmldir': b'plugin fixture\n',
        }
        binaries = {}
        for original, relative in INSTALLER.BINARIES.items():
            self.payload[relative] = relative.encode()
            binaries[original] = {'sha256': hashlib.sha256(self.payload[relative]).hexdigest()}
        self.compatibility = {'schema': 1, 'architecture': 'x86_64', 'plasma_major': 6,
                              'qt_build_version': '6.4.2', 'binaries': binaries}
        self.write_package()
        self.addCleanup(patch.stopall)
        patch.dict(os.environ, {'XDG_DATA_HOME': str(self.prefix),
                                'XDG_CONFIG_HOME': str(self.config)}).start()
        patch.object(INSTALLER.platform, 'machine', return_value='x86_64').start()
        patch.object(INSTALLER.shutil, 'which', side_effect=lambda name: '/bin/' + name).start()
        patch.object(INSTALLER, 'command', side_effect=self.host_command).start()

    def write_package(self, extra=None):
        self.payload['contents/compatibility.json'] = json.dumps(self.compatibility).encode()
        with zipfile.ZipFile(self.package, 'w') as archive:
            for name, data in {**self.payload, **(extra or {})}.items():
                archive.writestr(name, data)
        Path(str(self.package) + '.sha256').write_text(
            INSTALLER.digest(self.package) + '  ' + self.package.name + '\n')

    @property
    def root(self):
        return self.prefix / 'plasma/wallpapers' / INSTALLER.PACKAGE_ID

    def host_command(self, *args):
        args = tuple(map(str, args))
        self.calls.append(args)
        major = self.compatibility['plasma_major']
        if args[-1] == '--version':
            return 'plasmashell ' + str(major) + '.4.0\n'
        if args[0] == 'ldd':
            if '-r' in args:
                return 'dependencies resolved\n'
            return (f'libQt{major}Core.so.{major} => /lib/libQt{major}Core.so.'
                    + ('5.12.8' if major == 5 else '6.4.2') + ' (0x123)\n')
        self.assertIn('--packageroot', args)
        if '--upgrade' in args:
            raise ValueError('same-version upgrade refused')
        if '--remove' in args:
            if self.root.exists():
                INSTALLER.shutil.rmtree(self.root)
        else:
            self.root.mkdir(parents=True, exist_ok=True)
            (self.root / 'partial').write_text('partial operation')
            if not self.fail:
                INSTALLER.shutil.rmtree(self.root)
                with zipfile.ZipFile(self.package) as archive:
                    archive.extractall(self.root)
        if self.fail and '--remove' not in args:
            raise ValueError('mock KPackage failure')
        return 'KPackage success\n'

    def legacy(self, files=None):
        files = files or {'nekomote/lib/libnekomote_plasma_qml.so': b'old library'}
        record = {'prefix': str(self.prefix), 'files': {}}
        for relative, content in files.items():
            path = self.prefix / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
            record['files'][relative] = {'sha256': INSTALLER.digest(path)}
        env = self.config / 'environment.d/nekomote.conf'
        env.parent.mkdir()
        env.write_text('QML_IMPORT_PATH="' + str(self.prefix / 'nekomote/qml') + '"\n')
        record['session_environment'] = {'path': str(env), 'sha256': INSTALLER.digest(env)}
        path = self.prefix / INSTALLER.RECORD
        path.parent.mkdir(exist_ok=True)
        path.write_text(json.dumps(record))
        return path, record

    def test_install_reinstall_status_uninstall_preserves_assets_and_config(self):
        asset = self.prefix / 'assets/characters/example/model.nkm'
        asset.parent.mkdir(parents=True)
        asset.write_bytes(b'user asset')
        panel = self.config / 'panel.json'
        panel.write_text('user panel configuration')
        INSTALLER.operate('install', self.prefix, self.package)
        INSTALLER.operate('status', self.prefix)
        INSTALLER.operate('reinstall', self.prefix, self.package)
        self.assertEqual(sum('--install' in call for call in self.calls), 2)
        self.assertTrue(any('--remove' in call for call in self.calls))
        INSTALLER.operate('uninstall', self.prefix)
        self.assertFalse(self.root.exists())
        self.assertEqual(asset.read_bytes(), b'user asset')
        self.assertEqual(panel.read_text(), 'user panel configuration')
        self.assertTrue(any('--remove' in call for call in self.calls))

    def test_legacy_install_failure_restores_every_old_file_and_environment(self):
        record_path, record = self.legacy()
        self.fail = True
        with self.assertRaisesRegex(ValueError, 'KPackage failure'):
            INSTALLER.operate('install', self.prefix, self.package)
        for relative, info in record['files'].items():
            self.assertEqual(INSTALLER.digest(self.prefix / relative), info['sha256'])
        self.assertTrue(record_path.exists())
        self.assertTrue((self.config / 'environment.d/nekomote.conf').exists())
        self.assertFalse(self.root.exists())

    def test_failed_reinstall_restores_existing_store_package(self):
        INSTALLER.operate('install', self.prefix, self.package)
        original = {str(p.relative_to(self.root)): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        self.fail = True
        with self.assertRaises(ValueError):
            INSTALLER.operate('reinstall', self.prefix, self.package)
        self.assertEqual(original, {str(p.relative_to(self.root)): p.read_bytes()
                                   for p in self.root.rglob('*') if p.is_file()})

    def test_success_migrates_legacy_only_after_checks(self):
        record_path, record = self.legacy()
        INSTALLER.operate('install', self.prefix, self.package)
        self.assertFalse(record_path.exists())
        self.assertFalse((self.config / 'environment.d/nekomote.conf').exists())
        for relative in record['files']:
            self.assertFalse((self.prefix / relative).exists())
        self.assertTrue(self.root.exists())

    def test_modified_legacy_file_prevents_all_uninstall_mutation(self):
        path, record = self.legacy({'nekomote/LICENSE': b'license',
                                    'nekomote/lib/libnekomote_plasma_qml.so': b'old'})
        (self.prefix / 'nekomote/lib/libnekomote_plasma_qml.so').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'modified'):
            INSTALLER.operate('uninstall', self.prefix)
        self.assertTrue(path.exists())
        self.assertTrue((self.prefix / 'nekomote/LICENSE').exists())
        self.assertEqual(self.calls, [])

    def test_record_traversal_and_external_environment_path_rejected(self):
        path, record = self.legacy()
        for mutation in ('traversal', 'environment'):
            with self.subTest(mutation=mutation):
                invalid = json.loads(json.dumps(record))
                if mutation == 'traversal':
                    invalid['files']['../outside'] = {'sha256': '0' * 64}
                else:
                    invalid['session_environment']['path'] = str(self.base / 'unrelated')
                path.write_text(json.dumps(invalid))
                with self.assertRaises(ValueError):
                    INSTALLER.operate('uninstall', self.prefix)
                self.assertTrue((self.prefix / 'nekomote/lib/libnekomote_plasma_qml.so').exists())
        self.assertEqual(self.calls, [])

    def test_symlink_parent_rejected(self):
        outside = self.base / 'outside'
        outside.mkdir()
        (self.prefix / 'nekomote').symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            INSTALLER.operate('uninstall', self.prefix)
        self.assertTrue(outside.exists())

    def test_checksum_traversal_and_native_hash_fail_before_migration(self):
        path, _ = self.legacy()
        for failure in ('checksum', 'traversal', 'binary', 'architecture', 'qt'):
            with self.subTest(failure=failure):
                self.compatibility['architecture'] = 'aarch64' if failure == 'architecture' else 'x86_64'
                self.compatibility['qt_build_version'] = '6.9.0' if failure == 'qt' else '6.4.2'
                self.write_package({'../outside': b'bad'} if failure == 'traversal' else None)
                if failure == 'checksum':
                    self.package.write_bytes(b'corrupt')
                if failure == 'binary':
                    key = next(iter(self.compatibility['binaries']))
                    original = self.compatibility['binaries'][key]['sha256']
                    self.compatibility['binaries'][key]['sha256'] = '0' * 64
                    self.write_package()
                    self.compatibility['binaries'][key]['sha256'] = original
                with self.assertRaises(ValueError):
                    INSTALLER.operate('install', self.prefix, self.package)
                self.assertTrue(path.exists())
                self.assertFalse(self.root.exists())
        self.assertFalse(any('kpackagetool' in call[0] for call in self.calls))

    def test_unrecorded_legacy_identity_is_required(self):
        legacy = self.prefix / 'plasma/wallpapers' / INSTALLER.LEGACY_ID
        legacy.mkdir(parents=True)
        metadata = legacy / 'metadata.json'
        metadata.write_text(json.dumps({'KPlugin': {'Id': 'another.wallpaper'}}))
        with self.assertRaisesRegex(ValueError, 'identity'):
            INSTALLER.operate('uninstall', self.prefix)
        metadata.write_text(json.dumps({'KPackageStructure': 'Plasma/Wallpaper',
                                        'KPlugin': {'Id': INSTALLER.LEGACY_ID}}))
        INSTALLER.operate('uninstall', self.prefix)
        self.assertFalse(legacy.exists())

    def test_qt5_uses_matching_system_tool(self):
        self.compatibility['plasma_major'] = 5
        self.compatibility['qt_build_version'] = '5.12.8'
        self.payload['metadata.desktop'] = (
            '[Desktop Entry]\nX-KDE-PluginInfo-Name=' + INSTALLER.PACKAGE_ID +
            '\nX-KDE-ServiceTypes=Plasma/Wallpaper;\n').encode()
        self.write_package()
        INSTALLER.operate('install', self.prefix, self.package)
        INSTALLER.operate('uninstall', self.prefix)
        self.assertTrue(any(call[0] == '/bin/kpackagetool5' for call in self.calls))
        self.assertFalse(any(call[0] == '/bin/kpackagetool6' for call in self.calls))

    def test_incoming_json_desktop_identity_cannot_disagree(self):
        original = self.payload['metadata.json']
        self.payload['metadata.desktop'] = (
            '[Desktop Entry]\nX-KDE-PluginInfo-Name=' + INSTALLER.PACKAGE_ID +
            '\nX-KDE-ServiceTypes=Plasma/Wallpaper;\n').encode()
        self.payload['metadata.json'] = json.dumps({'KPackageStructure': 'Plasma/Wallpaper',
                                                  'KPlugin': {'Id': 'other.plugin'}}).encode()
        self.write_package()
        with self.assertRaisesRegex(ValueError, 'archive wallpaper identity'):
            INSTALLER.operate('install', self.prefix, self.package)
        self.payload['metadata.json'] = original
        self.payload['metadata.desktop'] = b'X-KDE-PluginInfo-Name=other.plugin\n'
        self.write_package()
        with self.assertRaisesRegex(ValueError, 'identity mismatch'):
            INSTALLER.operate('install', self.prefix, self.package)
        self.assertEqual(self.calls, [])
        self.assertFalse(self.root.exists())

    def test_kpackage_success_without_installation_preserves_legacy(self):
        record_path, _ = self.legacy()
        original = self.host_command
        def no_install(*args):
            if str(args[0]).endswith('kpackagetool6'):
                return 'ignored installation'
            return original(*args)
        with patch.object(INSTALLER, 'command', side_effect=no_install):
            with self.assertRaises(ValueError):
                INSTALLER.operate('install', self.prefix, self.package)
        self.assertTrue(record_path.exists())
        self.assertTrue((self.prefix / 'nekomote/lib/libnekomote_plasma_qml.so').exists())
        self.assertFalse(self.root.exists())

    def test_modified_environment_prevents_batch_uninstall(self):
        record_path, _ = self.legacy()
        env = self.config / 'environment.d/nekomote.conf'
        env.write_text('user edited environment')
        with self.assertRaisesRegex(ValueError, 'modified legacy environment'):
            INSTALLER.operate('uninstall', self.prefix)
        self.assertTrue(record_path.exists())
        self.assertTrue((self.prefix / 'nekomote/lib/libnekomote_plasma_qml.so').exists())
        self.assertEqual(env.read_text(), 'user edited environment')

    def test_host_dependency_failure_does_not_remove_legacy(self):
        record_path, _ = self.legacy()
        original = self.host_command
        def missing_dependency(*args):
            if args[:2] == ('ldd', '-r'):
                return 'libQt6Quick.so.6 => not found'
            return original(*args)
        with patch.object(INSTALLER, 'command', side_effect=missing_dependency):
            with self.assertRaisesRegex(ValueError, 'dependency failure'):
                INSTALLER.operate('install', self.prefix, self.package)
        self.assertTrue(record_path.exists())
        self.assertFalse(self.root.exists())

    def test_adjacent_archive_discovery_and_menu(self):
        with patch.object(INSTALLER, '__file__', str(self.base / 'installer.py')):
            with patch.object(INSTALLER.sys, 'argv', ['installer.py', '--prefix', str(self.prefix)]):
                with patch('builtins.input', return_value='1'):
                    self.assertEqual(INSTALLER.main(), 0)
        self.assertTrue(self.root.is_dir())

    def test_zip_symlink_entry_rejected_before_install(self):
        with zipfile.ZipFile(self.package, 'a') as archive:
            entry = zipfile.ZipInfo('contents/unsafe')
            entry.external_attr = 0o120777 << 16
            archive.writestr(entry, '/outside')
        Path(str(self.package) + '.sha256').write_text(
            INSTALLER.digest(self.package) + '  ' + self.package.name + '\n')
        with self.assertRaisesRegex(ValueError, 'unsafe ZIP'):
            INSTALLER.operate('install', self.prefix, self.package)
        self.assertEqual(self.calls, [])

    def test_recordless_exact_old_environment_removed_but_user_file_retained(self):
        env = self.config / 'environment.d/nekomote.conf'
        env.parent.mkdir()
        env.write_text('QML_IMPORT_PATH="' + str(self.prefix / 'nekomote/qml') + '"\n')
        INSTALLER.operate('uninstall', self.prefix)
        self.assertFalse(env.exists())
        env.write_text('user-authored environment')
        INSTALLER.operate('uninstall', self.prefix)
        self.assertEqual(env.read_text(), 'user-authored environment')


if __name__ == '__main__':
    unittest.main()
