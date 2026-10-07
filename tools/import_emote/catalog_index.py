"""共享的 catalog 索引与候选选择逻辑。

`import_all_assets.py`、`import_all_test.py` 和 `audit_catalog.py` 共用本模块，
保证 195 条记录的 canonical 选择一致：优先父目录名等于 key 的候选，
否则保留全部候选让调用方按歧义处理。索引每批只建立一次。
"""
import collections
import hashlib
import json
import pathlib


def validate_model_name(name):
    """Catalog IDs form one directory component, never an output path."""
    if (not isinstance(name, str) or not name or name in ('.', '..') or
            '/' in name or '\\' in name or '\0' in name or
            name.removesuffix('.pure') in ('', '.', '..')):
        raise ValueError(f'invalid model ID: {name!r}')


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def load_catalog(reference_root):
    """返回 (catalog dict, catalog_sha256)。catalog 不存在或非 JSON 时抛异常。"""
    catalog_path = reference_root / 'config/model-catalog.json'
    if not catalog_path.is_file():
        raise FileNotFoundError(f'catalog not found: {catalog_path}')
    data = json.loads(catalog_path.read_text())
    if not isinstance(data, dict):
        raise ValueError('catalog must be an object')
    for name, record in data.items():
        validate_model_name(name)
        if not isinstance(record, dict):
            raise ValueError(f'catalog record must be an object: {name!r}')
    digest = hashlib.sha256(catalog_path.read_bytes()).hexdigest()
    return data, digest


def build_index(reference_root):
    """索引所有解包模型 JSON，按 stem 去除 .pure 后缀分组。

    跳过 `.resx.json` 资源映射文件。返回 defaultdict[list]，调用方不应再 mutate。
    """
    index = collections.defaultdict(list)
    conversion = reference_root / 'model_conversion'
    if not conversion.is_dir():
        return index
    for p in sorted(conversion.rglob('*.json')):
        if not p.name.endswith('.resx.json'):
            index[p.stem.removesuffix('.pure')].append(p)
    return index


def resolve_candidates(index, name):
    """canonical 选择：优先父目录名等于 key 的候选。

    若恰好一个候选的父目录名等于 key，则只返回它；否则返回全部候选，
    让调用方按歧义（多个）或缺失（零个）处理。不在此处抛异常，
    以便账本保留 AMBIGUOUS/MISSING 状态。
    """
    key = name.removesuffix('.pure')
    candidates = index.get(key, [])
    canonical = [p for p in candidates if p.parent.name == key]
    if len(canonical) == 1:
        return canonical
    return candidates


def find_external_textures(source, src_dir):
    """读取模型 JSON，找出 pixel==null 的纹理，精确匹配目录下的 PNG。

    返回 dict[texture_name, str|None]：None 表示歧义或缺失，调用方据此报错。
    """
    document = json.loads(source.read_text())
    external = {}
    for texture, value in document.get('source', {}).items():
        if not isinstance(value, dict):
            continue
        tex = value.get('texture', {})
        if not isinstance(tex, dict) or tex.get('pixel') is not None:
            continue
        suffix = 'tex' + (texture.split('#')[-1] if '#' in texture else '000') + '.png'
        matches = sorted(src_dir.glob('*' + suffix))
        if len(matches) == 1:
            external[texture] = str(matches[0].relative_to(src_dir))
        else:
            external[texture] = None
    return external


def relative_to(path, root):
    """安全计算相对路径，跨设备或绝对外路径时返回 str(path)。"""
    try:
        return str(path.relative_to(root))
    except ValueError:
        return str(path)
