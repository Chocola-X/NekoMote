#!/usr/bin/env python3
"""Install an official NekoMote Plasma bundle using the host's KPackage tool.

Python 3.8+, standard library only. No sudo, network, desktop restart, asset or
panel-configuration changes. Checksums detect damage, not publisher authenticity:
only use bundles obtained from the trusted project release channel.
"""
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

PACKAGE_ID = 'uk.nekopara.nekomote'
LEGACY_ID = 'org.nekomote.wallpaper'
RECORD = 'nekomote/.install-record.json'
BINARIES = {
    'lib/libnekomote_plasma_qml.so': 'contents/lib/libnekomote_plasma_qml.so',
    'qml/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so':
        'contents/ui/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so',
}


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(65536), b''):
            h.update(chunk)
    return h.hexdigest()


def read_object(path):
    value = json.loads(path.read_text())
    if not isinstance(value, dict):
        raise ValueError('expected JSON object: ' + str(path))
    return value


def safe_path(path):
    """Never traverse a symlink, including an existing ancestor of the prefix."""
    path = Path(path)
    if not path.is_absolute() or '..' in path.parts:
        raise ValueError('unsafe absolute path: ' + str(path))
    for parent in (path,) + tuple(path.parents):
        if parent.is_symlink():
            raise ValueError('symlink rejected: ' + str(parent))
    return path


def data_root(value=None):
    path = safe_path(Path(value or os.environ.get('XDG_DATA_HOME', str(Path.home() / '.local/share'))))
    if str(path) in ('/', str(Path.home())) or path.parts[1:2] in [
            (name,) for name in ('usr', 'etc', 'bin', 'sbin', 'lib', 'lib64', 'proc', 'sys', 'dev', 'boot')]:
        raise ValueError('unsafe data prefix: ' + str(path))
    return path


def environment_file():
    return safe_path(Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config'))) /
                     'environment.d/nekomote.conf')


def command(*args):
    env = dict(os.environ)
    env.pop('LD_LIBRARY_PATH', None)
    env.pop('LD_PRELOAD', None)
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True,
                            timeout=60, env=env)
    if result.returncode:
        raise ValueError('command failed: ' + ' '.join(str(a) for a in args) + '\n' +
                         result.stdout + result.stderr)
    return result.stdout + result.stderr


def desktop_has_identity(path, expected):
    safe_path(path)
    fields = dict(re.findall(r'^([^#=\n]+)=(.*)$', path.read_text(), re.M))
    return fields.get('X-KDE-PluginInfo-Name') == expected and \
        'Plasma/Wallpaper' in fields.get('X-KDE-ServiceTypes', '').split(';')


def package_identity(root, expected):
    safe_path(root)
    metadata = root / 'metadata.json'
    desktop = root / 'metadata.desktop'
    if metadata.is_file():
        safe_path(metadata)
        data = read_object(metadata)
        if data.get('KPackageStructure') == 'Plasma/Wallpaper' and \
                isinstance(data.get('KPlugin'), dict) and data['KPlugin'].get('Id') == expected:
            return data
    if desktop.is_file() and desktop_has_identity(desktop, expected):
        return {'KPlugin': {'Id': expected}}
    raise ValueError('unrecognized wallpaper identity: ' + str(root))


def tree_files(root):
    safe_path(root)
    files = []
    for path in sorted(root.rglob('*')):
        safe_path(path)
        if path.is_file():
            files.append(path)
        elif not path.is_dir():
            raise ValueError('non-regular package entry: ' + str(path))
    return files


def legacy_allowed(name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or '\\' in name or str(path) != name:
        return False
    exact = {'nekomote/lib/libnekomote_plasma_qml.so',
             'nekomote/qml/NekoMote/Wallpaper/qmldir',
             'nekomote/qml/NekoMote/Wallpaper/libnekomote_plasma_qmlplugin.so',
             'nekomote/qml/NekoMote/Wallpaper/nekomote_plasma_qml.qmltypes',
             'nekomote/LICENSE', 'nekomote/THIRD_PARTY_NOTICES'}
    return name in exact or any(name.startswith(base + '/') for base in (
        'plasma/wallpapers/' + PACKAGE_ID, 'plasma/plasmoids/org.nekomote.validation'))


def legacy_plan(prefix):
    """Validate the entire old install before any KPackage call or deletion."""
    remove = []
    record_path = safe_path(prefix / RECORD)
    if record_path.exists():
        record = read_object(record_path)
        if record.get('prefix') != str(prefix) or not isinstance(record.get('files'), dict):
            raise ValueError('invalid legacy installation record')
        for name, info in record['files'].items():
            if not legacy_allowed(name) or not isinstance(info, dict):
                raise ValueError('legacy record path rejected: ' + name)
            path = safe_path(prefix / name)
            if not path.is_file() or digest(path) != info.get('sha256'):
                raise ValueError('modified or missing legacy file: ' + str(path))
            remove.append(path)
        for base in ('plasma/wallpapers/' + PACKAGE_ID,
                     'plasma/plasmoids/org.nekomote.validation'):
            for path in tree_files(prefix / base):
                if path not in remove:
                    raise ValueError('unrecorded legacy package file: ' + str(path))
        env = environment_file()
        info = record.get('session_environment')
        if info is not None:
            if not isinstance(info, dict) or info.get('path') != str(env):
                raise ValueError('legacy environment path rejected')
            if env.exists():
                if not env.is_file() or digest(env) != info.get('sha256'):
                    raise ValueError('modified legacy environment file: ' + str(env))
                remove.append(env)
        elif env.exists():
            escaped = str(prefix / 'nekomote/qml').replace('\\', '\\\\').replace('"', '\\"').replace('$', '$$')
            allowed = [f'{var}="{escaped}"\n' for var in ('QML_IMPORT_PATH', 'QML2_IMPORT_PATH')]
            allowed.append('QML_IMPORT_PATH="' + str(prefix) + '/nekomote/qml:${QML_IMPORT_PATH:-}"\n')
            if env.read_text() not in allowed:
                raise ValueError('unrecognized legacy environment content')
            remove.append(env)
        remove.append(record_path)
    if not record_path.exists():
        env = environment_file()
        if env.is_file():
            escaped = str(prefix / 'nekomote/qml').replace('\\', '\\\\').replace('"', '\\"').replace('$', '$$')
            allowed = [f'{var}="{escaped}"\n' for var in ('QML_IMPORT_PATH', 'QML2_IMPORT_PATH')]
            allowed.append('QML_IMPORT_PATH="' + str(prefix) + '/nekomote/qml:${QML_IMPORT_PATH:-}"\n')
            if env.read_text() in allowed:
                remove.append(env)
    legacy = safe_path(prefix / 'plasma/wallpapers' / LEGACY_ID)
    if legacy.exists():
        package_identity(legacy, LEGACY_ID)
        remove.extend(tree_files(legacy))
    return remove


def validate_archive(package, destination):
    safe_path(package)
    checksum = safe_path(Path(str(package) + '.sha256'))
    fields = checksum.read_text().split()
    if len(fields) != 2 or fields[1] != package.name or \
            not re.fullmatch('[0-9a-f]{64}', fields[0]) or fields[0] != digest(package):
        raise ValueError('invalid package checksum')
    with zipfile.ZipFile(package) as archive:
        seen = set()
        total = 0
        for entry in archive.infolist():
            name = entry.filename
            path = PurePosixPath(name)
            mode = entry.external_attr >> 16
            total += entry.file_size
            if name in seen or not name or path.is_absolute() or '..' in path.parts or \
                    '\\' in name or ':' in name or str(path) != name.rstrip('/') or \
                    stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR) or total > 1024**3:
                raise ValueError('unsafe ZIP entry: ' + name)
            seen.add(name)
        archive.extractall(destination)
    # New payloads require the canonical JSON identity. The desktop fallback
    # is only for recognizing old installations, never for incoming archives.
    plugin = read_object(destination / 'metadata.json')
    if plugin.get('KPackageStructure') != 'Plasma/Wallpaper' or \
            not isinstance(plugin.get('KPlugin'), dict) or plugin['KPlugin'].get('Id') != PACKAGE_ID:
        raise ValueError('invalid archive wallpaper identity')
    desktop = destination / 'metadata.desktop'
    if desktop.exists() and (not desktop.is_file() or not desktop_has_identity(desktop, PACKAGE_ID)):
        raise ValueError('archive JSON/desktop identity mismatch')
    info = read_object(destination / 'contents/compatibility.json')
    if info.get('schema') != 1 or info.get('plasma_major') not in (5, 6) or \
            info.get('architecture') not in ('x86_64', 'aarch64'):
        raise ValueError('invalid compatibility metadata')
    if info['plasma_major'] == 5 and not desktop.is_file():
        raise ValueError('missing Plasma5 desktop metadata')
    binaries = info.get('binaries')
    if not isinstance(binaries, dict):
        raise ValueError('invalid native binary metadata')
    for old, new in BINARIES.items():
        path = destination / new
        binary = binaries.get(old)
        if not isinstance(binary, dict) or not path.is_file() or digest(path) != binary.get('sha256'):
            raise ValueError('native payload hash mismatch: ' + new)
    for relative in ('contents/ui/main.qml', 'contents/ui/config.qml',
                     'contents/ui/NekoMote/Wallpaper/qmldir', 'contents/config/main.xml'):
        if not (destination / relative).is_file():
            raise ValueError('missing package file: ' + relative)
    return info


def host_check(info, extracted):
    major = info['plasma_major']
    if platform.machine() != info['architecture']:
        raise ValueError('package architecture does not match host')
    tool = shutil.which('kpackagetool' + str(major))
    shell = shutil.which('plasmashell')
    if not tool or not shell:
        raise ValueError('required kpackagetool/plasmashell not found')
    if not re.search(r'plasmashell\s+' + str(major) + r'\.', command(shell, '--version')):
        raise ValueError('Plasma major does not match package')
    libraries = command('ldd', shell)
    match = re.search(r'libQt' + str(major) + r'Core\.so\.\d+\s+=>\s+(\S+)', libraries)
    if not match:
        raise ValueError('cannot identify plasmashell Qt library')
    qt = re.search(r'\.so\.(' + str(major) + r'\.\d+\.\d+)$', str(Path(match[1]).resolve()))
    required = info.get('qt_build_version', '')
    if not qt or not re.fullmatch(r'[56]\.\d+\.\d+', required) or int(required[0]) != major:
        raise ValueError('cannot verify host/build Qt version')
    if tuple(map(int, qt[1].split('.')[:2])) < tuple(map(int, required.split('.')[:2])):
        raise ValueError('host Qt is older than package Qt')
    for relative in BINARIES.values():
        result = command('ldd', '-r', extracted / relative)
        if re.search(r'not found|undefined symbol|not a dynamic executable', result):
            raise ValueError('host dependency failure: ' + result)
    return tool


def current_package(prefix):
    root = safe_path(prefix / 'plasma/wallpapers' / PACKAGE_ID)
    if not root.exists():
        return root, None
    package_identity(root, PACKAGE_ID)
    metadata = root / 'contents/compatibility.json'
    if metadata.exists():
        safe_path(metadata)
        major = read_object(metadata).get('plasma_major')
        if major not in (5, 6):
            raise ValueError('invalid installed package major')
    else:
        major = 5 if (root / 'metadata.desktop').exists() else 6
    tree_files(root)
    return root, major


def operate(action, prefix, package=None):
    remove = legacy_plan(prefix)
    root, installed_major = current_package(prefix)
    if action == 'status':
        print(json.dumps({'installed': installed_major is not None, 'plasma_major': installed_major,
                          'legacy_files': len(remove), 'package_root': str(root)}))
        return
    with tempfile.TemporaryDirectory(prefix='nekomote-installer-') as temporary:
        work = Path(temporary)
        if action != 'uninstall':
            if package is None:
                candidates = list(Path(__file__).resolve().parent.glob('*.wallpaper.zip'))
                if len(candidates) != 1:
                    raise ValueError('expected one adjacent *.wallpaper.zip; use --package')
                package = candidates[0]
            package = Path(package).absolute()
            info = validate_archive(package, work / 'extracted')
            tool = host_check(info, work / 'extracted')
            if action == 'install' and installed_major is not None and not (prefix / RECORD).exists():
                raise ValueError('already installed; use reinstall')
            operation = '--install'
            argument = package
        else:
            if installed_major is None and not remove:
                print('NekoMote is not installed')
                return
            tool = shutil.which('kpackagetool' + str(installed_major)) if installed_major else None
            if installed_major and not tool:
                raise ValueError('installed package requires kpackagetool' + str(installed_major))
            operation, argument = '--remove', PACKAGE_ID
        # Snapshot before KPackage can replace its directory. Old files remain in
        # place until the operation succeeds; failures restore all original bytes.
        originals = set(remove + (tree_files(root) if root.exists() else []))
        for index, path in enumerate(sorted(originals)):
            target = work / 'backup' / str(index)
            target.parent.mkdir(exist_ok=True)
            shutil.copy2(path, target)
        backups = dict(zip(sorted(originals), sorted((work / 'backup').glob('*'), key=lambda p: int(p.name))))
        try:
            if installed_major is not None or action != 'uninstall':
                prefix.mkdir(parents=True, exist_ok=True)
                if installed_major is not None and action != 'uninstall':
                    # Explicit reinstallation supports identical versions and
                    # downgrades too; KPackage upgrade version checks vary.
                    command(tool, '--type', 'Plasma/Wallpaper', '--packageroot', root.parent,
                            '--remove', PACKAGE_ID)
                command(tool, '--type', 'Plasma/Wallpaper', '--packageroot', root.parent,
                        operation, argument)
                if action == 'uninstall':
                    if root.exists():
                        raise ValueError('KPackage did not remove the wallpaper directory')
                else:
                    package_identity(root, PACKAGE_ID)
                    for source, relative in BINARIES.items():
                        path = root / relative
                        safe_path(path)
                        if not path.is_file() or digest(path) != info['binaries'][source]['sha256']:
                            raise ValueError('KPackage did not install the expected native payload')
            for path in remove:
                # The successful installation owns the current wallpaper directory.
                if action != 'uninstall' and root in path.parents:
                    continue
                if path.exists():
                    path.unlink()
            for legacy in (prefix / 'plasma/wallpapers' / LEGACY_ID,):
                if legacy.exists():
                    shutil.rmtree(legacy)
        except Exception as failure:
            try:
                if root.exists():
                    shutil.rmtree(root)
                for path, source in backups.items():
                    path.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, path)
            except OSError as rollback_error:
                # Preserve a recovery copy if permissions/device failure prevent
                # rollback. Do not silently discard the only remaining old bytes.
                recovery = Path(tempfile.mkdtemp(prefix='nekomote-recovery-'))
                for path, source in backups.items():
                    shutil.copy2(source, recovery / source.name)
                (recovery / 'paths.json').write_text(json.dumps(
                    {source.name: str(path) for path, source in backups.items()}, indent=2))
                raise ValueError(str(failure) + '\nRollback failed: ' + str(rollback_error) +
                                 '\nRecovery files retained at: ' + str(recovery)) from failure
            raise
    print(action + ' complete; assets and panel configuration retained. Restart Plasma manually when ready.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', nargs='?', choices=('install', 'reinstall', 'uninstall', 'status'))
    parser.add_argument('--package', type=Path)
    parser.add_argument('--prefix', type=Path, help='user data root (XDG_DATA_HOME by default)')
    args = parser.parse_args()
    if args.action is None:
        print('1 Install\n2 Reinstall\n3 Uninstall\n4 Status\n0 Exit')
        choice = input('Select: ').strip()
        if choice == '0':
            return 0
        actions = {'1': 'install', '2': 'reinstall', '3': 'uninstall', '4': 'status'}
        if choice not in actions:
            raise ValueError('invalid menu selection')
        args.action = actions[choice]
    operate(args.action, data_root(args.prefix), args.package)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError, zipfile.BadZipFile, subprocess.SubprocessError, EOFError) as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        sys.exit(1)
