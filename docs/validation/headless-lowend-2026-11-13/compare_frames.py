"""Compare x86 little-endian snapshots produced by runtime_probe.cpp.

Reports differences without imposing a new acceptance tolerance or changing goldens.
Usage: python3 compare_frames.py FIRST_DIRECTORY SECOND_DIRECTORY
"""

import json
import math
from pathlib import Path
import struct
import sys


def frames(path):
    data = path.read_bytes()
    offset = 0

    def words(count):
        nonlocal offset
        result = struct.unpack_from(f'<{count}I', data, offset)
        offset += count * 4
        return result

    while offset < len(data):
        vertices, indices = words(2)
        points = struct.unpack_from(f'<{vertices * 8}f', data, offset)
        offset += vertices * 8 * 4
        integers = [vertices, indices, *words(indices)]
        for _ in range(2):
            count, = words(1)
            integers.extend((count, *words(count * 6)))
        composition, count = words(2)
        integers.extend((composition, count, *words(count * 2)))
        yield points, integers
    assert offset == len(data)


def main():
    first, second = map(Path, sys.argv[1:])
    names = sorted(p.name for p in first.glob('*.bin'))
    if not names or set(names) != {p.name for p in second.glob('*.bin')}:
        raise ValueError('missing or unexpected snapshot files')
    report = []
    for name in names:
        a, b = list(frames(first / name)), list(frames(second / name))
        if len(a) != len(b):
            raise ValueError('frame count differs: ' + name)
        maxima = [0.0] * 8
        changed = 0
        for (left, li), (right, ri) in zip(a, b):
            if li != ri or len(left) != len(right):
                raise ValueError('integer frame protocol differs: ' + name)
            for i, (x, y) in enumerate(zip(left, right)):
                if not math.isfinite(x) or not math.isfinite(y):
                    raise ValueError('nonfinite vertex field: ' + name)
                difference = abs(x - y)
                maxima[i % 8] = max(maxima[i % 8], difference)
                changed += x != y
        report.append(dict(case=name, sampled_frames=len(a), integer_fields_equal=True,
                           unequal_float_values=changed,
                           max_absolute_difference=dict(zip(['x', 'y', 'u', 'v', 'r', 'g', 'b', 'a'], maxima))))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
