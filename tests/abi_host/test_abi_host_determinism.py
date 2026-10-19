#!/usr/bin/env python3
"""G8 确定性 C ABI 诊断工具测试。

验证 abi_host 的：
- 数值解析负例（尾随字符、负数、溢出、超界、空字符串）退出码 2
- 固定步模式 PNG 截图可解码
- 两次同输入像素完全相同（确定性）
- 缺纹理的有效合成资产退出非零
- 交互模式与固定步模式分开

需要 GPU 和显示服务器；headless 环境跳过。
"""
import hashlib
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
HOST = pathlib.Path(os.environ.get('NEKOMOTE_ABI_HOST', REPO / 'build/release/bin/nekomote_abi_host'))
MODEL = pathlib.Path(os.environ.get('NEKOMOTE_MODEL', REPO / 'assets/characters/chocola/date-a/model.nkm'))
SESSION = pathlib.Path(os.environ.get('NEKOMOTE_SESSION', REPO / 'assets/characters/chocola/date-a/session.nks.json'))


def read_png_pixels(path):
    """最小 PNG 解码器，返回 (width, height, pixel_bytes)。"""
    data = path.read_bytes()
    assert data[:8] == bytes([137, 80, 78, 71, 13, 10, 26, 10]), 'bad PNG signature'
    pos = 8
    width = height = color_type = 0
    idat = b''
    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos + 4])[0]
        ctype = data[pos + 4:pos + 8]
        cdata = data[pos + 8:pos + 8 + length]
        if ctype == b'IHDR':
            width, height, _, color_type = struct.unpack('>IIBB', cdata[:10])
        elif ctype == b'IDAT':
            idat += cdata
        elif ctype == b'IEND':
            break
        pos += 12 + length
    import zlib
    raw = zlib.decompress(idat)
    channels = 4 if color_type == 6 else 3
    row_bytes = width * channels
    pixels = bytearray()
    for y in range(height):
        offset = y * (row_bytes + 1)
        assert raw[offset] == 0, f'unexpected filter {raw[offset]}'
        pixels += raw[offset + 1:offset + 1 + row_bytes]
    return width, height, bytes(pixels)


@unittest.skipUnless(HOST.is_file() and MODEL.is_file(), 'abi_host or model not built')
class AbiHostDeterminismTest(unittest.TestCase):
    def _run(self, *args, timeout=30):
        return subprocess.run([str(HOST)] + list(args),
                              capture_output=True, text=True, timeout=timeout)

    def test_trailing_chars_rejected(self):
        r = self._run('--model', 'x', '--session', 'y', '--width', '800abc')
        self.assertEqual(r.returncode, 2)
        self.assertIn('invalid integer', r.stderr)

    def test_negative_width_rejected(self):
        r = self._run('--model', 'x', '--session', 'y', '--width', '-5')
        self.assertEqual(r.returncode, 2)

    def test_negative_seed_rejected(self):
        r = self._run('--model', 'x', '--session', 'y', '--seed', '-1')
        self.assertEqual(r.returncode, 2)
        self.assertIn('negative', r.stderr)

    def test_overflow_seed_rejected(self):
        r = self._run('--model', 'x', '--session', 'y', '--seed', '99999999999')
        self.assertEqual(r.returncode, 2)

    def test_width_out_of_range_rejected(self):
        r = self._run('--model', 'x', '--session', 'y', '--width', '70000')
        self.assertEqual(r.returncode, 2)

    def test_empty_steps_rejected(self):
        r = self._run('--model', 'x', '--session', 'y', '--steps', '')
        self.assertEqual(r.returncode, 2)

    def test_capture_requires_bounded_fixed_steps(self):
        r = self._run('--model', 'x', '--session', 'y', '--capture', 'unused.png')
        self.assertEqual(r.returncode, 2)
        self.assertIn('--steps', r.stderr)

    def test_excessive_steps_rejected(self):
        r = self._run('--model', 'x', '--session', 'y', '--steps', '100001')
        self.assertEqual(r.returncode, 2)

    def test_excessive_capture_dimensions_rejected(self):
        r = self._run('--model', 'x', '--session', 'y', '--width', '8193')
        self.assertEqual(r.returncode, 2)

    def test_fixed_step_png_is_decodable(self):
        with tempfile.TemporaryDirectory() as tmp:
            cap = pathlib.Path(tmp) / 'cap.png'
            r = self._run('--model', str(MODEL), '--session', str(SESSION),
                          '--steps', '10', '--seed', '42',
                          '--capture', str(cap),
                          '--width', '160', '--height', '120')
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertTrue(cap.is_file())
            w, h, pixels = read_png_pixels(cap)
            self.assertEqual(w, 160)
            self.assertEqual(h, 120)
            self.assertEqual(len(pixels), 160 * 120 * 4)

    def test_same_input_produces_identical_pixels(self):
        with tempfile.TemporaryDirectory() as tmp:
            cap1 = pathlib.Path(tmp) / 'cap1.png'
            cap2 = pathlib.Path(tmp) / 'cap2.png'
            for cap in (cap1, cap2):
                r = self._run('--model', str(MODEL), '--session', str(SESSION),
                              '--steps', '20', '--seed', '42',
                              '--capture', str(cap),
                              '--width', '160', '--height', '120')
                self.assertEqual(r.returncode, 0, r.stderr)
            _, _, p1 = read_png_pixels(cap1)
            _, _, p2 = read_png_pixels(cap2)
            self.assertEqual(hashlib.sha256(p1).hexdigest(),
                             hashlib.sha256(p2).hexdigest())

    def test_missing_texture_exits_nonzero(self):
        with tempfile.TemporaryDirectory() as tmp:
            dst = pathlib.Path(tmp) / 'model'
            shutil.copytree(MODEL.parent, dst)
            shutil.rmtree(dst / 'textures')
            r = self._run('--model', str(dst / 'model.nkm'),
                          '--session', str(dst / 'session.nks.json'),
                          '--steps', '1', '--seed', '42', timeout=15)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn('texture', r.stderr.lower())

    def test_different_seed_may_differ(self):
        """不同 seed 不要求一定不同，但同 seed 一定相同。此测试只验证不崩溃。"""
        with tempfile.TemporaryDirectory() as tmp:
            cap = pathlib.Path(tmp) / 'cap.png'
            r = self._run('--model', str(MODEL), '--session', str(SESSION),
                          '--steps', '5', '--seed', '999',
                          '--capture', str(cap),
                          '--width', '160', '--height', '120')
            self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == '__main__':
    unittest.main()
