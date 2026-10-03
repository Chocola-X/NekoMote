"""Diagnostic PSB fixture mutation, never a runtime importer.

Only walks the PSB compact-array/dictionary address tables to change explicitly
selected control flags. Layout facts were checked against psbfile (MIT,
number201724) and FreeMote's public PSB definitions; this implementation is
independent. The complete source model and all geometry/resource bytes remain
unchanged. Capture metadata records every changed byte and both hashes.
"""
import struct


def disable_clamp(data):
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
    controls = dictionary(root['metadata'])['clampControl']
    if data[controls] != 0x20:
        raise ValueError('expected PSB clamp collection')
    offsets, begin = table(controls+1)
    edits = []
    for index, offset in enumerate(offsets):
        pos = dictionary(begin+offset)['enabled']
        if data[pos:pos+2] != b'\x05\x01':
            raise ValueError('fixture expects enabled=1 encoded in one byte')
        result[pos+1] = 0
        edits.append(dict(path=f'/metadata/clampControl/{index}/enabled',
                          offset=pos+1, before=1, after=0))
    if sum(a != b for a, b in zip(result, data)) != len(edits):
        raise ValueError('unexpected diagnostic fixture difference')
    return bytes(result), edits
