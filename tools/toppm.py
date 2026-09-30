#!/usr/bin/env python3
"""Convert any image macOS can read into a binary PPM for main.cpp.

    python3 tools/toppm.py photo.jpg target.ppm [max_size]

sips decodes to PNG, this decodes the PNG. No third-party libraries.
"""

import os
import struct
import subprocess
import sys
import tempfile
import zlib


def png_to_rgb(data):
    """Non-interlaced 8-bit PNG -> (width, height, rgb_bytes)."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")

    width = height = color_type = None
    idat = b""
    pos = 8
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + length]
        pos += 12 + length  # length + type + body + crc

        if kind == b"IHDR":
            width, height, depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8:
                raise ValueError("only 8-bit PNGs supported, got %d" % depth)
            if interlace:
                raise ValueError("interlaced PNGs not supported")
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break

    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color_type)
    if channels is None:
        raise ValueError("unsupported PNG color type %s" % color_type)

    raw = zlib.decompress(idat)
    stride = width * channels
    rows = []
    prev = bytearray(stride)
    pos = 0
    for _ in range(height):
        filter_type = raw[pos]
        pos += 1
        line = bytearray(raw[pos : pos + stride])
        pos += stride

        # undo the row filter; a = left, b = above, c = above-left
        if filter_type == 1:  # Sub
            for x in range(channels, stride):
                line[x] = (line[x] + line[x - channels]) & 255
        elif filter_type == 2:  # Up
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif filter_type == 3:  # Average
            for x in range(stride):
                a = line[x - channels] if x >= channels else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif filter_type == 4:  # Paeth
            for x in range(stride):
                a = line[x - channels] if x >= channels else 0
                c = prev[x - channels] if x >= channels else 0
                b = prev[x]
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        elif filter_type != 0:
            raise ValueError("bad filter type %d" % filter_type)

        prev = line
        rows.append(line)

    # drop alpha, expand gray
    out = bytearray()
    for line in rows:
        for x in range(width):
            px = line[x * channels : x * channels + channels]
            if channels == 1:
                out += bytes([px[0], px[0], px[0]])
            elif channels == 2:
                out += bytes([px[0], px[0], px[0]])
            else:
                out += px[:3]
    return width, height, bytes(out)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    src, dst = sys.argv[1], sys.argv[2]
    max_size = int(sys.argv[3]) if len(sys.argv) > 3 else 256

    tmp = tempfile.mktemp(suffix=".png")
    try:
        subprocess.run(
            ["sips", "-s", "format", "png", "-Z", str(max_size), src, "-o", tmp],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.STDOUT,
        )
        with open(tmp, "rb") as f:
            width, height, rgb = png_to_rgb(f.read())
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)

    with open(dst, "wb") as f:
        f.write(b"P6 %d %d 255\n" % (width, height))
        f.write(rgb)
    print("wrote %s  %dx%d" % (dst, width, height))


if __name__ == "__main__":
    main()
