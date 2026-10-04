"""Synthetic PSB v4 regressions; no original model, browser or third-party module."""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / 'tools/reference_capture'))
from psb_fixture import make_fixture


def table(values):
    count_width = max(1, (len(values).bit_length()+7)//8)
    width = max(1, (max(values, default=0).bit_length()+7)//8)
    return (bytes([12+count_width])+len(values).to_bytes(count_width, 'little')+
            bytes([12+width])+b''.join(v.to_bytes(width, 'little') for v in values))


def fixture():
    names = ['metadata', 'hairControl', 'friction_x', 'friction_y', 'enabled',
             'eyeControl', 'blinkEnabled', 'beginFrame']
    trie = {}
    for name in names:
        node = trie
        for char in name.encode()+b'\0':
            node = node.setdefault(char, {})
    bases, parents, occupied = {0: 0}, {0: 0}, {0}
    terminals = {}

    def encode_trie(node, index, prefix):
        base = 1
        while any(base+char in occupied for char in node):
            base += 1
        bases[index] = base
        for char in node:
            child = base+char
            parents[child] = index
            occupied.add(child)
        for char, children in node.items():
            child = base+char
            if char == 0:
                terminals[prefix.decode()] = child
            else:
                encode_trie(children, child, prefix+bytes([char]))
    encode_trie(trie, 0, b'')
    size = max(occupied)+1
    name_data = (table([bases.get(i, 0) for i in range(size)])+
                 table([parents.get(i, 0) for i in range(size)])+
                 table([terminals[name] for name in names]))

    def dictionary(fields):
        pool, offsets, positions = b'', [], {}
        for _, payload in fields:
            if payload not in positions:
                positions[payload] = len(pool)
                pool += payload
            offsets.append(positions[payload])
        return b'\x21'+table([names.index(name) for name, _ in fields])+table(offsets)+pool
    number = b'\x1e'+struct.pack('<f', 0.03125)
    control = dictionary([('friction_x', number), ('friction_y', number), ('enabled', b'\x05\x01')])
    collection = b'\x20'+table([0])+control
    eye = dictionary([('enabled', b'\x05\x01'), ('blinkEnabled', b'\x05\x01'),
                      ('beginFrame', b'\x04')])
    eyes = b'\x20'+table([0])+eye
    root = dictionary([('metadata', dictionary([('hairControl', collection), ('eyeControl', eyes)]))])
    header = bytearray(44)
    header[:8] = b'PSB\0'+struct.pack('<HH', 4, 0)
    strings = table([])
    struct.pack_into('<I', header, 12, len(header))
    struct.pack_into('<I', header, 16, len(header)+len(name_data))
    struct.pack_into('<I', header, 20, len(header)+len(name_data)+len(strings))
    struct.pack_into('<I', header, 36, len(header)+len(name_data)+len(strings))
    return bytes(header)+name_data+strings+root


class SharedScalarTests(unittest.TestCase):
    def test_aliases_and_actual_float_are_reported(self):
        original = fixture()
        result, edits = make_fixture(original, numbers={'/metadata/hairControl/0/friction_x': 0.1})
        self.assertEqual(len(result), len(original))
        self.assertEqual(edits[0]['affected_paths'], ['/metadata/hairControl/0/friction_x',
                                                     '/metadata/hairControl/0/friction_y'])
        self.assertEqual(edits[0]['stored_after'], struct.unpack('<f', struct.pack('<f', 0.1))[0])
        self.assertEqual(original, fixture())

    def test_conflicting_shared_writes_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'conflicting aliased fixture writes'):
            make_fixture(fixture(), numbers={'/metadata/hairControl/0/friction_x': 0.5,
                                            '/metadata/hairControl/0/friction_y': 0.125})

    def test_equal_requests_can_share_storage(self):
        original = fixture()
        one, _ = make_fixture(original, numbers={'/metadata/hairControl/0/friction_x': 0.5})
        both, edits = make_fixture(original, numbers={'/metadata/hairControl/0/friction_x': 0.5,
                                                     '/metadata/hairControl/0/friction_y': 0.5})
        self.assertEqual(one, both)
        self.assertEqual(edits[0]['offset'], edits[1]['offset'])

    def test_unknown_and_out_of_range_paths_are_rejected(self):
        for path in ['/metadata/hairControl/1/friction_x', '/metadata/hairControl/0/unknown']:
            with self.subTest(path=path), self.assertRaises((ValueError, KeyError)):
                make_fixture(fixture(), numbers={path: 0.5})

    def test_retargeting_separates_blink_from_shared_enable(self):
        original = fixture()
        result, edits = make_fixture(original, references={
            '/metadata/eyeControl/0/blinkEnabled': '/metadata/eyeControl/0/beginFrame'})
        self.assertEqual(len(original), len(result))
        self.assertEqual(edits[0]['kind'], 'field_reference')
        self.assertEqual(edits[0]['affected_paths'], ['/metadata/eyeControl/0/blinkEnabled'])
        # A separate enabled write now affects that field alone.
        _, next_edits = make_fixture(result, numbers={'/metadata/eyeControl/0/enabled': 0})
        self.assertEqual(next_edits[0]['affected_paths'], ['/metadata/eyeControl/0/enabled'])

    def test_retargeting_rejects_other_parents_and_containers(self):
        with self.assertRaisesRegex(ValueError, 'same parent'):
            make_fixture(fixture(), references={'/metadata/eyeControl/0/blinkEnabled':
                                                '/metadata/hairControl/0/enabled'})
        with self.assertRaises(ValueError):
            make_fixture(fixture(), references={'/metadata/hairControl': '/metadata/eyeControl'})


if __name__ == '__main__':
    unittest.main()
