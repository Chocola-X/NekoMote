"""Offline lossless audio conversion and character voice sharing helpers."""
import ctypes
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import tempfile


def sha256(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def publish_directory(staging, target):
    """Linux atomic rename without replacement, including dangling destination links."""
    rename = ctypes.CDLL(None, use_errno=True).renameat2
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p,
                       ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(-100, os.fsencode(staging), -100, os.fsencode(target), 1):
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), str(target))


def convert_audio(converter, source, destination):
    destination = pathlib.Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if os.path.lexists(destination):
        raise ValueError(f'conversion destination exists: {destination}')
    completed = False
    try:
        result = subprocess.run([str(converter), str(source), str(destination)],
                                capture_output=True, text=True, check=True)
        completed = True
        metadata = json.loads(result.stdout)
        if not isinstance(metadata, dict) or metadata.get('ok') is not True:
            raise ValueError('converter did not verify lossless audio')
        if not destination.is_file() or destination.is_symlink():
            raise ValueError(f'converter produced no regular file: {destination}')
        return metadata
    except (OSError, ValueError, TypeError, subprocess.SubprocessError):
        # The native tool publishes atomically and never leaves partial output.
        # A failed process may have refused a destination created by another
        # writer; do not remove that writer's file.
        if completed and (destination.is_file() or destination.is_symlink()):
            destination.unlink()
        raise


def safe_relative(value):
    path = pathlib.PurePosixPath(value)
    if not value or path.is_absolute() or '..' in path.parts or '\\' in value or '\x00' in value:
        raise ValueError(f'unsafe asset path: {value!r}')
    return path


def validate_links(root, allowed_root=None):
    root = pathlib.Path(root).resolve(strict=True)
    allowed_root = pathlib.Path(allowed_root or root).resolve(strict=True)
    visited = set()

    def visit(directory):
        resolved_directory = directory.resolve(strict=True)
        if resolved_directory in visited:
            return
        visited.add(resolved_directory)
        for path in directory.iterdir():
            if path.is_symlink():
                target = os.readlink(path)
                if os.path.isabs(target):
                    raise ValueError(f'absolute asset link: {path}')
                try:
                    resolved = path.resolve(strict=True)
                except (OSError, RuntimeError) as error:
                    raise ValueError(f'dangling or cyclic asset link: {path}') from error
                if not resolved.is_relative_to(allowed_root):
                    raise ValueError(f'asset link escapes root: {path}')
            if path.is_dir():
                visit(path)
    visit(root)


def tree_ledger(root, allowed_root=None):
    """Include directory link text and every target file, without following cycles."""
    root = pathlib.Path(root)
    validate_links(root, allowed_root)
    entries = {}
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            resolved = path.resolve(strict=True)
            targets = ({p.relative_to(resolved).as_posix(): sha256(p)
                        for p in sorted(resolved.rglob('*')) if p.is_file()}
                       if resolved.is_dir() else {'': sha256(resolved)})
            entries[relative] = {'link': os.readlink(path), 'targets': targets}
        elif path.is_file():
            entries[relative] = sha256(path)
    return entries


def audio_group_name(character, voices):
    """Use source voice namespaces, never costume names (teenage also uses ko voices)."""
    groups = {character: 'main', 'ko' + character: 'little'}
    voices = set(voices)
    if len(voices) != 1 or not voices.issubset(groups):
        raise ValueError(f'{character}: expected one normal or kitten voice namespace, got {sorted(voices)}')
    return groups[next(iter(voices))]


def group_audio(bundle, role_root, final_bundle=None, *, name=None):
    """Publish main/little filename→content mappings; share bytes within this role.

    A group owns each first occurrence of a FLAC stream. Subsequent filenames and
    groups use relative file links to that stream. Source voice namespaces select
    the group; conflicting content under the same group is rejected. Caller stages
    the model itself.
    """
    audio = bundle / 'audio'
    if not audio.is_dir() or audio.is_symlink():
        return None
    files = sorted(p for p in audio.rglob('*') if p.is_file())
    if not files:
        audio.rmdir()
        return None
    if any(p.suffix != '.flac' or p.is_symlink() for p in files):
        raise ValueError('new model audio must contain only regular FLAC files')
    if name is None:
        session = json.loads((bundle / 'session.nks.json').read_text())['session']
        name = audio_group_name(role_root.name, (
            reaction['label'].split('/')[0] for reaction in session['reactions']
            if reaction.get('audio_path')))
    if name not in ('main', 'little'):
        raise ValueError(f'unsupported audio group: {name}')
    mapping = {p.relative_to(audio).as_posix(): sha256(p) for p in files}
    pool = role_root / 'audio'
    if pool.is_symlink():
        raise ValueError(f'audio pool must be a regular directory: {pool}')
    pool.mkdir(parents=True, exist_ok=True)
    validate_links(pool, role_root)
    group = pool / name
    if os.path.lexists(group) and (group.is_symlink() or not group.is_dir()):
        raise ValueError(f'audio group must be a regular directory: {group}')
    if group.exists():
        actual = {p.relative_to(group).as_posix(): sha256(p)
                  for p in group.rglob('*') if p.is_file()}
        if mapping != actual:
            raise ValueError(f'audio group content mismatch: {group}')
    else:
        known = {}
        for path in sorted(pool.rglob('*.flac')):
            if path.is_file():
                resolved = path.resolve(strict=True)
                if not resolved.is_relative_to(role_root.resolve()):
                    raise ValueError('audio pool link escapes role')
                known.setdefault(sha256(path), path)
        with tempfile.TemporaryDirectory(prefix='.group-', dir=pool) as temporary:
            stage = pathlib.Path(temporary) / 'group'
            stage.mkdir()
            for source in files:
                relative = source.relative_to(audio)
                destination = stage / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                digest = mapping[relative.as_posix()]
                final_destination = group / relative
                if digest in known:
                    destination.symlink_to(os.path.relpath(known[digest], final_destination.parent))
                else:
                    shutil.copyfile(source, destination)
                    known[digest] = final_destination
            publish_directory(stage, group)
    shutil.rmtree(audio)
    target_bundle = final_bundle or bundle
    audio.symlink_to(os.path.relpath(group, target_bundle), target_is_directory=True)
    return name
