"""Offline inspection/fixtures for NekoMote's current binary model container.

The player uses the native, bounded, record-streaming decoder. This small tool
codec intentionally builds a document for offline comparisons only. No Python
MessagePack dependency is needed; binary/ext values are outside the model schema.
"""
import pathlib
import struct
import zlib

MAGIC = b'NKMMPK01'
DECODED_LIMIT = 512 * 1024 * 1024


def pack(value):
    if value is None:
        return b'\xc0'
    if isinstance(value, bool):
        return b'\xc3' if value else b'\xc2'
    if isinstance(value, int):
        if 0 <= value < 128:
            return bytes([value])
        if -32 <= value < 0:
            return bytes([value & 255])
        if value >= 0:
            for code, form, limit in ((0xcc, '>B', 1 << 8), (0xcd, '>H', 1 << 16),
                                      (0xce, '>I', 1 << 32), (0xcf, '>Q', 1 << 64)):
                if value < limit:
                    return bytes([code]) + struct.pack(form, value)
        else:
            for code, form, limit in ((0xd0, '>b', -(1 << 7)), (0xd1, '>h', -(1 << 15)),
                                      (0xd2, '>i', -(1 << 31)), (0xd3, '>q', -(1 << 63))):
                if value >= limit:
                    return bytes([code]) + struct.pack(form, value)
        raise ValueError('model integer exceeds 64 bits')
    if isinstance(value, float):
        return b'\xcb' + struct.pack('>d', value)
    if isinstance(value, str):
        body = value.encode('utf-8')
        if len(body) < 32:
            return bytes([0xa0 + len(body)]) + body
        for code, form, limit in ((0xd9, '>B', 1 << 8), (0xda, '>H', 1 << 16),
                                  (0xdb, '>I', 1 << 32)):
            if len(body) < limit:
                return bytes([code]) + struct.pack(form, len(body)) + body
    if isinstance(value, (list, dict)):
        size = len(value)
        array = isinstance(value, list)
        if size < 16:
            header = bytes([(0x90 if array else 0x80) + size])
        elif size < 65536:
            header = bytes([0xdc if array else 0xde]) + struct.pack('>H', size)
        else:
            header = bytes([0xdd if array else 0xdf]) + struct.pack('>I', size)
        values = value if array else [item for pair in value.items() for item in pair]
        return header + b''.join(pack(item) for item in values)
    raise ValueError('unsupported model value')


def unpack(payload):
    offset = 0

    def take(size):
        nonlocal offset
        if size < 0 or offset + size > len(payload):
            raise ValueError('truncated model record')
        result = payload[offset:offset + size]
        offset += size
        return result

    def number(form):
        return struct.unpack(form, take(struct.calcsize(form)))[0]

    def value(depth=0):
        if depth >= 256:
            raise ValueError('model nesting exceeds limit')
        code = take(1)[0]
        if code <= 0x7f:
            return code
        if code >= 0xe0:
            return code - 256
        if code in (0xc0, 0xc2, 0xc3):
            return {0xc0: None, 0xc2: False, 0xc3: True}[code]
        formats = {0xca: '>f', 0xcb: '>d', 0xcc: '>B', 0xcd: '>H', 0xce: '>I', 0xcf: '>Q',
                   0xd0: '>b', 0xd1: '>h', 0xd2: '>i', 0xd3: '>q'}
        if code in formats:
            return number(formats[code])
        if 0xa0 <= code <= 0xbf or code in (0xd9, 0xda, 0xdb):
            size = code - 0xa0 if code <= 0xbf else number({0xd9: '>B', 0xda: '>H', 0xdb: '>I'}[code])
            return take(size).decode('utf-8')
        if 0x90 <= code <= 0x9f or code in (0xdc, 0xdd):
            size = code - 0x90 if code <= 0x9f else number('>H' if code == 0xdc else '>I')
            if size > len(payload) - offset:
                raise ValueError('truncated model array')
            return [value(depth + 1) for _ in range(size)]
        if 0x80 <= code <= 0x8f or code in (0xde, 0xdf):
            size = code - 0x80 if code <= 0x8f else number('>H' if code == 0xde else '>I')
            if size > (len(payload) - offset) // 2:
                raise ValueError('truncated model map')
            result = {}
            for _ in range(size):
                key = value(depth + 1)
                if not isinstance(key, str) or key in result:
                    raise ValueError('invalid or duplicate model key')
                result[key] = value(depth + 1)
            return result
        raise ValueError('unsupported model record type')

    result = value()
    if offset != len(payload):
        raise ValueError('trailing model records')
    return result


def read_model(path):
    path = pathlib.Path(path)
    if path.stat().st_size > DECODED_LIMIT + 1024 * 1024:
        raise ValueError('encoded model size exceeds limit')
    with path.open('rb') as stream:
        header = stream.read(16)
        if len(header) != 16 or header[:8] != MAGIC:
            raise ValueError('unsupported model container; rebuild or migrate assets')
        expected, = struct.unpack('<Q', header[8:])
        if expected == 0 or expected > DECODED_LIMIT:
            raise ValueError('decoded model size exceeds limit')
        decoder = zlib.decompressobj()
        chunks, produced = [], 0
        while data := stream.read(32768):
            try:
                chunk = decoder.decompress(data, expected - produced + 1)
            except zlib.error as error:
                raise ValueError('invalid model compressed stream or checksum') from error
            produced += len(chunk)
            if produced > expected or decoder.unconsumed_tail:
                raise ValueError('model decoded length mismatch')
            chunks.append(chunk)
            if decoder.unused_data or (decoder.eof and stream.read(1)):
                raise ValueError('trailing model compressed data')
        if not decoder.eof or produced != expected:
            raise ValueError('truncated model or decoded length mismatch')
    return unpack(b''.join(chunks))


def write_model(path, document):
    payload = pack(document)
    if not payload or len(payload) > DECODED_LIMIT:
        raise ValueError('encoded model size exceeds limit')
    pathlib.Path(path).write_bytes(MAGIC + struct.pack('<Q', len(payload)) + zlib.compress(payload, 9))
