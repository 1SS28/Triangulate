#!/usr/bin/env python3
"""PSNR between two binary PPMs.

    python3 tools/psnr.py a.ppm b.ppm

Both must be the same size; a mismatch means the two pipelines did not fit the
same target and the comparison would be meaningless.
"""

import math
import sys


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"P6":
        raise ValueError("%s is not a P6 PPM" % path)
    vals, pos = [], 2
    while len(vals) < 3:
        while pos < len(data) and data[pos : pos + 1].isspace():
            pos += 1
        if data[pos : pos + 1] == b"#":
            while data[pos : pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while not data[pos : pos + 1].isspace():
            pos += 1
        vals.append(int(data[start:pos]))
    w, h, _ = vals
    return w, h, data[pos + 1 : pos + 1 + w * h * 3]


def main():
    wa, ha, a = read_ppm(sys.argv[1])
    wb, hb, b = read_ppm(sys.argv[2])
    if (wa, ha) != (wb, hb):
        raise SystemExit("size mismatch: %dx%d vs %dx%d" % (wa, ha, wb, hb))
    sse = 0
    for x, y in zip(a, b):
        d = x - y
        sse += d * d
    mse = sse / (255.0 * 255.0 * len(a))  # normalize to [0,1] like the C++ loss
    print("%.4f" % (99.0 if mse <= 0 else 10 * math.log10(1.0 / mse)))


main()
