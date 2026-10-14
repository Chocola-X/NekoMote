"""Generated resources and malformed atlas inputs, without browser or real assets."""
import json
import pathlib
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / 'tools/reference_capture'))
from synthetic_model import encode_psb, model, sprite, write_model


class SyntheticModelTests(unittest.TestCase):
    def test_png_and_psb_contain_the_same_offset_rgba_tile(self):
        document = model([sprite()])
        atlas = document['source']['tex']
        atlas['texture'].update(width=128, height=64, truncated_width=128, truncated_height=64)
        atlas['icon']['tile'].update(left=40.0, top=16.0)
        with tempfile.TemporaryDirectory() as root:
            directory = pathlib.Path(root) / 'asset'
            write_model(directory, document)
            png = (directory / 'texture.png').read_bytes()
            compressed = bytearray()
            offset = 8
            while offset < len(png):
                length = struct.unpack_from('>I', png, offset)[0]
                if png[offset + 4:offset + 8] == b'IDAT':
                    compressed.extend(png[offset + 8:offset + 8 + length])
                offset += length + 12
            rows = zlib.decompress(compressed)
            self.assertEqual(len(rows), 64 * (128 * 4 + 1))
            rgba = b''.join(rows[y * 513 + 1:(y + 1) * 513] for y in range(64))
            psb = (directory / 'model.psb').read_bytes()
            resource_begin = struct.unpack_from('<I', psb, 32)[0]
            self.assertEqual(psb[resource_begin:], rgba)
            self.assertEqual(rgba[:(16 * 128 + 40) * 4], bytes((16 * 128 + 40) * 4))
            self.assertEqual(rgba[(16 * 128 + 40) * 4:(16 * 128 + 40) * 4 + 4],
                             bytes([240, 64, 32, 255]))
            self.assertEqual(json.loads((directory / 'model.json').read_text()), document)

    def test_invalid_rectangles_fail_before_writing_any_asset(self):
        for field, value in [('left', -1), ('top', 0.5), ('width', 0),
                             ('height', float('nan')), ('left', 1), ('width', 33)]:
            with self.subTest(field=field, value=value), tempfile.TemporaryDirectory() as root:
                document = model([sprite()])
                document['source']['tex']['icon']['tile'][field] = value
                directory = pathlib.Path(root) / 'invalid'
                with self.assertRaises(ValueError):
                    write_model(directory, document)
                self.assertFalse(directory.exists())

    def test_missing_resource_and_nonfinite_parameters_are_rejected(self):
        for document in [dict(pixel='#resource#1'), dict(parameter=float('inf'))]:
            with self.subTest(document=document), self.assertRaises(ValueError):
                encode_psb(document, [bytes(4)])


if __name__ == '__main__':
    unittest.main()
