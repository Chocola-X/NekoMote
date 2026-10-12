"""Generate tiny synthetic PNG format fixtures; uses only the Python standard library."""
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).parent

def chunk(kind, payload):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))

def png(name, width, height, depth, color, raw, extra=(), interlace=0):
    data = b'\x89PNG\r\n\x1a\n'
    data += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, depth, color, 0, 0, interlace))
    for kind, payload in extra:
        data += chunk(kind, payload)
    data += chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    (ROOT / name).write_bytes(data)

png('gray1.png', 4, 1, 1, 0, b'\0\x60')
png('gray16.png', 3, 1, 16, 0, b'\0' + struct.pack('>HHH', 0x1234, 0x8000, 0xFFFF), [(b'tRNS', struct.pack('>H', 0x8000))])
png('palette2.png', 4, 1, 2, 3, b'\0\x1b', [(b'PLTE', bytes([10,20,30,40,50,60,70,80,90,100,110,120])), (b'tRNS', bytes([0,64,128,255]))])
png('rgb_trns.png', 2, 1, 8, 2, bytes([0,17,18,19,100,101,102]), [(b'tRNS', struct.pack('>HHH',17,18,19))])
png('rgba16.png', 2, 1, 16, 6, b'\0' + struct.pack('>HHHHHHHH', 0x1234,0x5678,0x9ABC,0x8000,0xABCD,0x00FF,0xFFFF,0))
png('gamma.png', 2, 1, 8, 6, bytes([0,50,100,150,128,200,150,100,0]), [(b'gAMA', struct.pack('>I',100000))])
raw = bytearray()
for x0,y0,dx,dy in [(0,0,8,8),(4,0,8,8),(0,4,4,8),(2,0,4,4),(0,2,2,4),(1,0,2,2),(0,1,1,2)]:
    for y in range(y0,3,dy):
        if x0 >= 4:
            continue
        raw.append(0)
        for x in range(x0,4,dx):
            raw.extend([x*30,y*50,x+y,128 if (x+y)%2 else 255])
png('adam7.png',4,3,8,6,raw,interlace=1)
(ROOT / 'truncated.png').write_bytes((ROOT / 'adam7.png').read_bytes()[:-5])
(ROOT / 'empty.bin').write_bytes(b'')
png('oversized.png',32769,1,8,0,b'\0\0')
