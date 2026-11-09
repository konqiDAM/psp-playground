"""Dependency-free PNG decoder used by the Racing 3D road probe.

Only 8-bit RGB/RGBA non-interlaced PNGs are needed (what PPSSPPHeadless
writes). Returns (width, height, rows) with rows[y][x] == (r, g, b, a).
"""

import struct
import zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"not a PNG: {path}")

    pos = 8
    idat = b""
    width = height = 0
    color_type = 0
    bit_depth = 0

    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", body[:10])
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break

    if bit_depth != 8 or color_type not in (2, 6):
        raise ValueError(f"unsupported PNG (depth {bit_depth}, type {color_type})")
    if width <= 0 or height <= 0:
        raise ValueError(f"bad PNG dimensions: {path}")

    raw = zlib.decompress(idat)
    bpp = 4 if color_type == 6 else 3
    stride = width * bpp
    rows = []
    prev = bytearray(stride)

    for y in range(height):
        base = y * (stride + 1)
        filt = raw[base]
        line = bytearray(raw[base + 1:base + 1 + stride])
        if filt == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif filt == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filt == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filt == 4:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                up = prev[i]
                up_left = prev[i - bpp] if i >= bpp else 0
                p = left + up - up_left
                pa = abs(p - left)
                pb = abs(p - up)
                pc = abs(p - up_left)
                pred = left if pa <= pb and pa <= pc else (up if pb <= pc else up_left)
                line[i] = (line[i] + pred) & 0xFF
        elif filt != 0:
            raise ValueError(f"bad PNG filter {filt} on row {y}")

        row = []
        for x in range(width):
            off = x * bpp
            if bpp == 4:
                row.append((line[off], line[off + 1], line[off + 2], line[off + 3]))
            else:
                row.append((line[off], line[off + 1], line[off + 2], 255))
        rows.append(row)
        prev = line

    return width, height, rows
