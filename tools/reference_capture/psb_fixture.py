"""Diagnostic PSB fixture mutation, never a runtime importer.

Only walks the PSB compact-array/dictionary address tables to change explicitly
selected flags, dimensions, numbers or existing string references. Layout facts were checked against psbfile (MIT,
number201724) and FreeMote's public PSB definitions; this implementation is
independent. Unselected payloads and all address tables remain unchanged. Capture metadata records every changed byte and both hashes.
"""
import struct


def make_fixture(data, *, disable_clamp=False, disable_physics=False,
                 disable_auto_blink=False, icon_sizes=None, numbers=None, strings=None):
    if data[:4] != b'PSB\0' or struct.unpack_from('<HH', data, 4) != (4, 0):
        raise ValueError('diagnostic fixture requires an unencrypted PSB v4')
    result = bytearray(data)

    def integer(pos, width):
        if not 1 <= width <= 4 or pos < 0 or pos+width > len(data):
            raise ValueError('invalid PSB address width/bounds')
        return int.from_bytes(data[pos:pos+width], 'little')

    def table(pos):
        count_width = data[pos]-12
        count = integer(pos+1, count_width)
        width = data[pos+1+count_width]-12
        begin = pos+2+count_width
        if count > len(data)//max(1, width):
            raise ValueError('invalid PSB table size')
        return [integer(begin+i*width, width) for i in range(count)], begin+count*width

    names_offset = struct.unpack_from('<I', data, 12)[0]
    bases, end = table(names_offset)
    parents, end = table(end)
    terminals, end = table(end)
    names = []
    for terminal in terminals:
        cursor = parents[terminal]
        chars = []
        visited = set()
        while cursor:
            if cursor in visited:
                raise ValueError('cyclic PSB name trie')
            visited.add(cursor)
            parent = parents[cursor]
            chars.append(cursor-bases[parent])
            cursor = parent
        names.append(bytes(reversed(chars)).decode('utf-8'))

    def dictionary(pos):
        if data[pos] != 0x21:
            raise ValueError('expected a PSB dictionary')
        keys, end = table(pos+1)
        offsets, begin = table(end)
        if len(keys) != len(offsets):
            raise ValueError('invalid PSB dictionary counts')
        return {names[key]: begin+offset for key, offset in zip(keys, offsets)}

    root = dictionary(struct.unpack_from('<I', data, 36)[0])
    edits = []
    metadata = dictionary(root['metadata'])
    for collection, field in ([('bustControl', 'enabled'), ('hairControl', 'enabled'),
                                ('partsControl', 'enabled')] if disable_physics else []) + (
                                [('eyeControl', 'blinkEnabled')] if disable_auto_blink else []):
        controls = metadata[collection]
        if data[controls] != 0x20:
            raise ValueError('expected PSB controller collection: '+collection)
        offsets, begin = table(controls+1)
        for index, offset in enumerate(offsets):
            pos = dictionary(begin+offset)[field]
            if data[pos] == 0x04 or data[pos:pos+2] == b'\x05\x00':
                continue
            if data[pos:pos+2] != b'\x05\x01':
                raise ValueError('fixture expects integer controller flag encoded in one byte')
            result[pos+1] = 0
            edits.append(dict(path=f'/metadata/{collection}/{index}/{field}',
                              offset=pos+1, before=1, after=0))
    if disable_clamp:
        controls = dictionary(root['metadata'])['clampControl']
        if data[controls] != 0x20:
            raise ValueError('expected PSB clamp collection')
        offsets, begin = table(controls+1)
        for index, offset in enumerate(offsets):
            pos = dictionary(begin+offset)['enabled']
            if data[pos:pos+2] != b'\x05\x01':
                raise ValueError('fixture expects enabled=1 encoded in one byte')
            result[pos+1] = 0
            edits.append(dict(path=f'/metadata/clampControl/{index}/enabled',
                              offset=pos+1, before=1, after=0))
    for qualified, size in (icon_sizes or {}).items():
        texture, icon = qualified.split('/')
        icons = dictionary(dictionary(dictionary(root['source'])[texture])['icon'])
        fields = dictionary(icons[icon])
        for name, value in zip(['width', 'height'], size):
            if not value > 0:
                raise ValueError('fixture icon size must be positive')
            pos = fields[name]
            if data[pos] != 0x1e:
                raise ValueError('fixture icon size must be float32')
            before = struct.unpack_from('<f', data, pos+1)[0]
            struct.pack_into('<f', result, pos+1, value)
            edits.append(dict(path=f'/source/{texture}/icon/{icon}/{name}',
                              offset=pos+1, before=before, after=value, bytes=4))

    def resolve(path):
        # JSON Pointer paths identify source fields; never insert a value or
        # change encoded widths, collections, address tables or resources.
        if not path.startswith(('/metadata/', '/object/')):
            raise ValueError('fixtures require an explicit metadata/object path')
        pos = struct.unpack_from('<I', data, 36)[0]
        for token in path.split('/')[1:]:
            token = token.replace('~1', '/').replace('~0', '~')
            if data[pos] == 0x21:
                pos = dictionary(pos)[token]
            elif data[pos] == 0x20:
                offsets, begin = table(pos+1)
                if not token.isdecimal() or not 0 <= int(token) < len(offsets):
                    raise ValueError('fixture array index is out of range: '+path)
                pos = begin+offsets[int(token)]
            else:
                raise ValueError('fixture path traverses a scalar: '+path)
        return pos

    for path, value in (numbers or {}).items():
        pos = resolve(path)
        tag = data[pos]
        if tag in (0x1e, 0x1f):
            fmt = '<f' if tag == 0x1e else '<d'
            before = struct.unpack_from(fmt, data, pos+1)[0]
            payload = struct.pack(fmt, value)
        elif 0x05 <= tag <= 0x0c and isinstance(value, int):
            width = tag-4
            before = int.from_bytes(data[pos+1:pos+1+width], 'little', signed=True)
            payload = value.to_bytes(width, 'little', signed=True)
        elif tag in (0x04, 0x1d) and value == 0:
            continue
        else:
            raise ValueError('numeric fixture requires a representable same-width number: '+path)
        result[pos+1:pos+1+len(payload)] = payload
        edits.append(dict(path=path, offset=pos+1, before=before, after=value, bytes=len(payload)))
    offsets, _ = table(struct.unpack_from('<I', data, 16)[0])
    strings_begin = struct.unpack_from('<I', data, 20)[0]
    labels = [data[strings_begin+offset:data.index(0, strings_begin+offset)].decode('utf-8')
              for offset in offsets]
    for path, value in (strings or {}).items():
        pos = resolve(path)
        width = data[pos]-0x14
        if not 1 <= width <= 4:
            raise ValueError('string fixture requires an existing string token: '+path)
        index = labels.index(value)
        payload = index.to_bytes(width, 'little')
        before = labels[integer(pos+1, width)]
        result[pos+1:pos+1+width] = payload
        edits.append(dict(path=path, offset=pos+1, before=before, after=value, bytes=width))
    allowed = {edit['offset']+i for edit in edits for i in range(edit.get('bytes', 1))}
    if any(a != b and pos not in allowed for pos, (a, b) in enumerate(zip(result, data))):
        raise ValueError('unexpected diagnostic fixture difference')
    return bytes(result), edits
