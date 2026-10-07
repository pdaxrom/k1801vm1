"""Reconstruct native Terak pixels from a simulator RAM/CSR snapshot (stdlib)."""
import struct
import zlib


def render(memory, gar, vir, vcr, output):
    ram, address = bytearray(73728), 0
    for line in memory.read_text().splitlines():
        for token in line.split('//', 1)[0].split():
            if token.startswith('@'):
                address = int(token[1:], 16)
            else:
                ram[address] = int(token, 16)
                address += 1
    pixels = bytearray()
    for y in range(240):
        pixels.append(0)  # PNG filter: none.
        zone = 2 if y < 80 else 1 if y < 160 else 0
        shifted = y+(vir & 15)
        row, scan = (shifted//10+(vir >> 8 & 31)) % 25, shifted % 10
        for x in range(640):
            value = 0
            a = (gar & ~1)+y*40+x//16
            if vcr & (1 << (zone+3)) and a < 57344 and ram[a] & (1 << (x//2 & 7)):
                value = 124
            code = ram[65536+row*128+x//8]
            if vcr & (1 << zone) and (32 <= code < 128 or code >= 160) and \
                    ram[69632+code*16+scan] & (1 << (x & 7)):
                value = 255
            pixels.append(value)

    def chunk(kind, data):
        return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data))

    output.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', 640, 240, 8, 0, 0, 0, 0))+
                       chunk(b'IDAT', zlib.compress(pixels))+chunk(b'IEND', b''))
