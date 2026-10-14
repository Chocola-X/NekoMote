#!/usr/bin/env python3
"""Generate independently authored geometric E-mote diagnostic assets.

The PSB writer extends this project's synthetic v4 fixture encoding, not an
E-mote evaluator. No original model, engine source or conversion executable is
used. JSON/PNG/resx and the reference PSB describe the same generated input.
"""
import argparse
import copy
import hashlib
import json
import math
import pathlib
import struct
import zlib


def table(values):
    count_width = max(1, (len(values).bit_length() + 7) // 8)
    width = max(1, (max(values, default=0).bit_length() + 7) // 8)
    return (bytes([12 + count_width]) + len(values).to_bytes(count_width, 'little') +
            bytes([12 + width]) + b''.join(v.to_bytes(width, 'little') for v in values))


def encode_psb(document, resources):
    """Encode the small unencrypted PSB v4 subset needed by these fixtures."""
    names, strings = set(), set()

    def collect(value):
        if isinstance(value, dict):
            names.update(value)
            for child in value.values():
                collect(child)
        elif isinstance(value, list):
            for child in value:
                collect(child)
        elif isinstance(value, str) and not value.startswith('#resource#'):
            strings.add(value)
    collect(document)
    names, strings = sorted(names), sorted(strings)
    name_ids = {name: i for i, name in enumerate(names)}
    string_ids = {value: i for i, value in enumerate(strings)}
    trie = {}
    for name in names:
        node = trie
        for char in name.encode('utf-8') + b'\0':
            node = node.setdefault(char, {})
    bases, parents, occupied, terminals = {0: 0}, {0: 0}, {0}, {}

    def place(node, index, prefix):
        base = 1
        while any(base + char in occupied for char in node):
            base += 1
        bases[index] = base
        for char in node:
            child = base + char
            parents[child] = index
            occupied.add(child)
        for char, children in node.items():
            child = base + char
            if char == 0:
                terminals[prefix.decode('utf-8')] = child
                bases[child] = name_ids[prefix.decode('utf-8')]
            else:
                place(children, child, prefix + bytes([char]))
    place(trie, 0, b'')
    size = max(occupied) + 1
    name_data = (table([bases.get(i, 0) for i in range(size)]) +
                 table([parents.get(i, 0) for i in range(size)]) +
                 table([terminals[name] for name in names]))

    def index(tag, value):
        width = max(1, (value.bit_length() + 7) // 8)
        if width > 4:
            raise ValueError('PSB index exceeds UInt32')
        return bytes([tag + width - 1]) + value.to_bytes(width, 'little')

    def encode(value):
        if value is None:
            return b'\x01'
        if isinstance(value, bool):
            return b'\x03' if value else b'\x02'
        if isinstance(value, int):
            if value == 0:
                return b'\x04'
            width = next((n for n in range(1, 9)
                          if -(1 << (8*n-1)) <= value < (1 << (8*n-1))), None)
            if width is None:
                raise ValueError('PSB integer exceeds Int64')
            return bytes([4 + width]) + value.to_bytes(width, 'little', signed=True)
        if isinstance(value, float):
            if not math.isfinite(value):
                raise ValueError('nonfinite PSB input')
            packed = struct.pack('<f', value)
            return (b'\x1e' + packed if struct.unpack('<f', packed)[0] == value
                    else b'\x1f' + struct.pack('<d', value))
        if isinstance(value, str):
            if value.startswith('#resource#'):
                resource = int(value[len('#resource#'):])
                if not 0 <= resource < len(resources):
                    raise ValueError('missing PSB resource')
                return index(25, resource)
            return index(21, string_ids[value])
        if isinstance(value, (dict, list)):
            children = [value[key] for key in sorted(value)] if isinstance(value, dict) else value
            payloads = [encode(child) for child in children]
            offsets, offset = [], 0
            for payload in payloads:
                offsets.append(offset)
                offset += len(payload)
            keys = table([name_ids[key] for key in sorted(value)]) if isinstance(value, dict) else b''
            return bytes([33 if isinstance(value, dict) else 32]) + keys + table(offsets) + b''.join(payloads)
        raise ValueError('unsupported PSB value type')

    root = encode(document)
    string_offsets, string_data = [], b''
    for value in strings:
        string_offsets.append(len(string_data))
        string_data += value.encode('utf-8') + b'\0'
    resource_offsets, resource_data = [], b''
    for resource in resources:
        resource_offsets.append(len(resource_data))
        resource_data += resource
    header = bytearray(56)
    header[:8] = b'PSB\0' + struct.pack('<HH', 4, 0)
    blocks = [(12, name_data), (36, root), (16, table(string_offsets)),
              (20, string_data), (44, table([])), (48, table([])), (52, b''),
              (24, table(resource_offsets)), (28, table([len(r) for r in resources])),
              (32, resource_data)]
    body = b''
    struct.pack_into('<I', header, 8, len(header))
    for field, block in blocks:
        struct.pack_into('<I', header, field, len(header) + len(body))
        body += block
    struct.pack_into('<I', header, 40, zlib.adler32(header[8:40] + header[44:56]))
    return bytes(header) + body


def png_rgba(width, height, rgba):
    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data +
                struct.pack('>I', zlib.crc32(kind + data)))
    scanlines = b''.join(b'\0' + rgba[y*width*4:(y+1)*width*4] for y in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(scanlines)) + chunk(b'IEND', b''))


def layer(label, content, children=None, **options):
    result = dict(label=label, type=0, coordinate=0, exportSelf=1,
                  frameList=[dict(content=content, time=0, type=2), dict(time=61, type=0)],
                  children=children or [], groundCorrection=0, inheritMask=0x20007fc,
                  joinTarget=1, meshCombine=0, meshDivision=16, meshSyncChildMask=8,
                  meshTransform=1, metadata=None, motionIndependentLayerInherit=0,
                  objTriPriority=2, originalIndex=0, parameterize=None, stencilType=0,
                  transformOrder=[0, 3, 2, 1])
    result.update(options)
    return result


def content(x=0, y=0, bp=None):
    return dict(src='blank', icon='64:64:0:0', mask=0x2000002,
                coord=[float(x), float(y), 0.0], mesh=dict(bp=bp, cc=None))


def patch(axis, amount):
    # Analytic polynomial warp: x += amount * v^2, or y += amount * u^2.
    squared = [0.0, 0.0, 1.0/3.0, 1.0]
    return [component for row in range(4) for col in range(4)
            for component in (col/3.0 + (amount*squared[row] if axis == 'x' else 0),
                              row/3.0 + (amount*squared[col] if axis == 'y' else 0))]


def model(layers, variables=None):
    variables = variables or []
    flattened = []
    def visit(children):
        for index, child in enumerate(children):
            child['originalIndex'] = index
            flattened.append(child)
            visit(child['children'])
    visit(layers)
    motion = dict(bounds=dict(left=0, top=0, right=96, bottom=96), lastTime=61,
                  loopTime=-1, layer=layers, layerIndexMap={l['label']: i for i, l in enumerate(flattened)},
                  metadata=None, parameter=[], parameterize=None,
                  priority=[dict(content=list(reversed(range(len(flattened)))), time=0, type=1),
                            dict(content=None, time=61, type=1)],
                  referenceModelFileList=[], referenceProjectFileList=[], tag=[], type=0, variable=[])
    metadata = dict(base=dict(chara='test', motion='root'), format='emote', scale=1,
                    mirror=0, version=1.0, logo=0, suitableClearColor=dict(r=0, g=0, b=0, a=0),
                    variableList=[dict(label=v, frameList=[]) for v in variables],
                    instantVariableList=[])
    for name in ['bustControl', 'clampControl', 'customPartsOrder', 'eyeControl',
                 'eyebrowControl', 'hairControl', 'immediateControl', 'loopControl',
                 'mouthControl', 'orbitControl', 'partsControl', 'selectorControl',
                 'timelineControl', 'transitionControl']:
        metadata[name] = []
    return dict(id='motion', label='synthetic', version=3.03, spec='ems', easing=[],
                screenSize=dict(width=256, height=256, originX=0, originY=0), metadata=metadata,
                object={'test': dict(metadata=None, motion={'root': motion})},
                source={'tex': dict(icon={'tile': dict(attr=0, width=32, height=32, left=0.0,
                                                     top=0.0, originX=0, originY=0)},
                                    texture=dict(width=32, height=32, truncated_width=32,
                                                 truncated_height=32, type='RGBA8', pixel='#resource#0'))})


def sprite(x=0, y=0):
    return layer('tile', dict(src='tex', icon='tile', mask=0x2000002,
                             coord=[float(x), float(y), 0.0], mesh=dict(bp=None, cc=None)))


def parameter_model(mode=1, begin=-2.0, end=3.0, frame_type=3):
    node = sprite()
    node['parameterize'] = 0
    first = node['frameList'][0]['content']
    node['frameList'] = [dict(time=0, type=frame_type, content=first),
                         dict(time=60, type=2, content={**first, 'coord': [60.0, 0.0, 0.0]}),
                         dict(time=61, type=0)]
    document = model([node], ['p'])
    document['object']['test']['motion']['root']['parameter'] = [
        dict(id='p', rangeBegin=begin, rangeEnd=end, division=60,
             discretization=mode, enabled=1)]
    return document


def inheritance_model(outer_mask=0x7fc, inner_mask=0x7fc, root_sync=8, middle_sync=8,
                      root_angle=0.0, middle_angle=0.0, own_curve=False,
                      third_boundary=False, animated=False, combined=False):
    tile = sprite(7, 11)
    if third_boundary:
        last_curve = layer('last curve', content(1, 2, patch('x', -0.2)), [tile])
        tile = layer('third boundary', content(3, 1), [last_curve], inheritMask=0x7fc)
    inner = layer('inner boundary', content(5, 6, patch('x', 0.15) if own_curve else None),
                  [tile], inheritMask=inner_mask)
    if combined:
        inner['children'] = [layer('combined helper', content(0, 0, patch('y', -0.1)),
                                   [tile], meshCombine=1)]
    mid_content = content(3, 4, patch('y', 0.3))
    mid_content.update(angle=middle_angle, mask=mid_content['mask'] | 0x10)
    middle = layer('middle curve', mid_content, [inner], meshSyncChildMask=middle_sync)
    outer = layer('outer boundary', content(2, 3), [middle], inheritMask=outer_mask)
    root_content = content(0, 0, patch('x', 0.4))
    root_content.update(angle=root_angle, mask=root_content['mask'] | 0x10)
    root = layer('outer curve', root_content, [outer], meshSyncChildMask=root_sync)
    document = model([root], ['p'] if animated else [])
    if animated:
        root['parameterize'] = 0
        last = copy.deepcopy(root_content)
        last['mesh']['bp'] = patch('x', -0.25)
        last['coord'] = [-3.0, 5.0, 0.0]
        root['frameList'] = [dict(time=0, type=3, content=root_content),
                             dict(time=60, type=2, content=last), dict(time=61, type=0)]
        document['object']['test']['motion']['root']['parameter'] = [
            dict(id='p', rangeBegin=-1.0, rangeEnd=1.0, division=60,
                 discretization=0, enabled=1)]
    return document


def rigid_grid_model():
    """Large rotated tile under a propagated identity surface, authored here."""
    tile = sprite(-0.20084495842456818, 174.00558471679688)
    tile['frameList'][0]['content'].update(angle=0.8, mask=0x2000012)
    tile['meshDivision'] = 16
    parent_content = content(0, 0, patch('x', 0))
    parent_content['icon'] = '100:100:0:0'
    document = model([layer('identity surface', parent_content, [tile])])
    document['source']['tex']['texture'].update(
        width=4096, height=2048, truncated_width=4096, truncated_height=2048)
    document['source']['tex']['icon']['tile'].update(
        left=800.0, top=1.0, width=595, height=921, originX=289, originY=460)
    return document


def cases():
    """Discriminating inputs plus held-out rotations, placement and combinations."""
    result = [dict(name='baseline', document=model([sprite()]), variables={})]
    for begin, end, label in [(-2.0, 3.0, 'integer-range'), (-2.5, 3.5, 'fractional-range')]:
        for value in [-3.0, -2.49, -1.9, -1.01, -0.4, 0.49, 0.99, 1.01, 3.49, 4.0]:
            result.append(dict(name=f'discrete-{label}-{value}',
                               document=parameter_model(1, begin, end), variables={'p': value}))
    for outer, inner, name in [(0x20007fc, 0x20007fc, 'full'),
                                (0x7fc, 0x20007fc, 'outer'),
                                (0x20007fc, 0x7fc, 'inner'), (0x7fc, 0x7fc, 'nested')]:
        result.append(dict(name='inherit-'+name,
                           document=inheritance_model(outer, inner), variables={}))
    for name, options in [
        ('rotate-root', dict(root_angle=17.5)),
        ('rotate-both', dict(root_angle=17.5, middle_angle=-23.0)),
        ('sync-root', dict(root_sync=1)),
        ('sync-middle', dict(middle_sync=1)),
        ('sync-both', dict(root_sync=1, middle_sync=1)),
        ('sync-none', dict(root_sync=0)),
        ('sync-none-both', dict(root_sync=0, middle_sync=0)),
        ('own-curve', dict(own_curve=True)),
        ('combined', dict(combined=True)),
        ('combined-rotated', dict(combined=True, own_curve=True, root_angle=17.5)),
        ('combined-full', dict(combined=True, own_curve=True,
                               outer_mask=0x20007fc, inner_mask=0x20007fc)),
        ('combined-inner', dict(combined=True, own_curve=True, outer_mask=0x20007fc)),
        ('combined-outer', dict(combined=True, own_curve=True, inner_mask=0x20007fc)),
        ('combined-position', dict(combined=True, own_curve=True, middle_sync=1)),
        ('triple', dict(third_boundary=True)),
        ('triple-rotated', dict(third_boundary=True, root_angle=-11.25,
                                middle_angle=29.0, own_curve=True)),
    ]:
        result.append(dict(name='inherit-'+name, document=inheritance_model(**options), variables={}))
    for value in [-0.75, 0.0, 0.6]:
        result.append(dict(name=f'inherit-parameter-{value}',
                           document=inheritance_model(animated=True, own_curve=True,
                                                      third_boundary=True), variables={'p': value}))
    result.append(dict(name='inherit-placement',
                       document=inheritance_model(root_angle=17.5, own_curve=True),
                       variables={}, x=-9.3, y=-21.5, scale=1.75))
    large = inheritance_model(root_angle=17.5, own_curve=True)
    large['source']['tex']['texture'].update(width=2048, height=2048,
                                           truncated_width=2048, truncated_height=2048)
    large['source']['tex']['icon']['tile'].update(left=1000.0, top=700.0)
    result.append(dict(name='inherit-large-atlas', document=large, variables={}))
    result.append(dict(name='inherit-full-sync-none', document=inheritance_model(
        0x20007fc, 0x20007fc, root_sync=0, middle_sync=0), variables={}))
    result.append(dict(name='rigid-grid', document=rigid_grid_model(), variables={}))
    for sync in [0, 1, 8]:
        inner = layer('inner boundary', content(5, 6), [sprite(7, 11)], inheritMask=0x7fc)
        outer = layer('outer boundary', content(2, 3), [inner], inheritMask=0x7fc,
                      meshSyncChildMask=sync)
        root = layer('outer curve', content(0, 0, patch('x', 0.4)), [outer])
        result.append(dict(name=f'inherit-adjacent-{sync}', document=model([root]), variables={}))
    return result


def write_model(directory, document):
    colors = [(240, 64, 32, 255), (32, 200, 80, 255), (40, 80, 240, 255), (240, 200, 32, 255)]
    atlas = document['source']['tex']
    width, height = atlas['texture']['width'], atlas['texture']['height']
    rect = atlas['icon']['tile']
    for value in [width, height, rect['width'], rect['height']]:
        if not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0 or int(value) != value:
            raise ValueError('atlas and tile dimensions must be positive integers')
    for value in [rect['left'], rect['top']]:
        if not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0 or int(value) != value:
            raise ValueError('tile offsets must be nonnegative integers')
    width, height = int(width), int(height)
    tile_width, tile_height = int(rect['width']), int(rect['height'])
    left, top = int(rect['left']), int(rect['top'])
    if left + tile_width > width or top + tile_height > height:
        raise ValueError('tile rectangle exceeds atlas bounds')
    directory.mkdir(parents=True, exist_ok=False)
    tile = bytes(c for y in range(tile_height) for x in range(tile_width)
                 for c in colors[(x//8 + 2*(y//8)) % 4])
    rgba = bytearray(width * height * 4)
    for y in range(tile_height):
        start = ((top+y)*width + left)*4
        rgba[start:start+tile_width*4] = tile[y*tile_width*4:(y+1)*tile_width*4]
    rgba = bytes(rgba)
    texture = png_rgba(width, height, rgba)
    (directory/'texture.png').write_bytes(texture)
    (directory/'model.json').write_text(json.dumps(document, indent=2) + '\n')
    resx = dict(PsbVersion=4, PsbType='Motion', Platform='ems', CryptKey=None,
                ExternalTextures=False, Context={}, Resources={'0': 'texture.png'})
    (directory/'model.resx.json').write_text(json.dumps(resx, indent=2) + '\n')
    # RGBA8 PSB resources are raw atlas bytes; the PNG is for the native importer.
    (directory/'model.psb').write_bytes(encode_psb(document, [rgba]))
    (directory/'manifest.json').write_text(json.dumps(dict(
        provenance='independently authored procedural fixture; no source model',
        files={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
               for p in sorted(directory.iterdir())}), indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    write_model(args.output, model([sprite()]))


if __name__ == '__main__':
    main()
