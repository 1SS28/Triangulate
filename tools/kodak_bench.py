#!/usr/bin/env python3
"""Compare against fogleman/primitive across the Kodak set.

    python3 tools/kodak_bench.py [triangles] [size] [threads]

Both fitters get the identical downscaled target, so the PSNRs are comparable.
Primitive's defaults already match ours: triangles (-m 1) and alpha 128 (~0.5).
Writes out/kodak/results.csv.
"""

import csv
import glob
import os
import subprocess
import sys
import time

N = int(sys.argv[1]) if len(sys.argv) > 1 else 350
SIZE = int(sys.argv[2]) if len(sys.argv) > 2 else 512
THREADS = int(sys.argv[3]) if len(sys.argv) > 3 else 1
TRIES = 2000

PRIM = os.path.expanduser("~/go/bin/primitive")
OUT = "out/kodak"


def run(cmd):
    t = time.perf_counter()
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("failed: %s\n%s" % (" ".join(cmd), r.stderr[-500:]))
    return time.perf_counter() - t, r.stdout


def psnr(a, b):
    return float(subprocess.run(["python3", "tools/psnr.py", a, b],
                                capture_output=True, text=True, check=True).stdout)


def main():
    os.makedirs(OUT, exist_ok=True)
    rows = []
    for src in sorted(glob.glob("images/kodak/*.png")):
        name = os.path.splitext(os.path.basename(src))[0]
        tgt_png, tgt_ppm = "%s/%s-in.png" % (OUT, name), "%s/%s.ppm" % (OUT, name)

        # one shared target: sips downscales once, both fitters see the same pixels
        subprocess.run(["sips", "-Z", str(SIZE), src, "--out", tgt_png],
                       check=True, capture_output=True)
        subprocess.run(["python3", "tools/toppm.py", tgt_png, tgt_ppm, str(SIZE)],
                       check=True, capture_output=True)

        mine_t, _ = run(["./geo", tgt_ppm, str(N), str(TRIES),
                         "%s/%s-mine" % (OUT, name), str(THREADS), "1", "64"])
        mine_p = psnr(tgt_ppm, "%s/%s-mine.ppm" % (OUT, name))

        prim_png = "%s/%s-prim.png" % (OUT, name)
        prim_t, _ = run([PRIM, "-i", tgt_png, "-o", prim_png, "-n", str(N),
                         "-m", "1", "-r", str(SIZE), "-s", str(SIZE),
                         "-j", str(THREADS)])
        subprocess.run(["python3", "tools/toppm.py", prim_png,
                        "%s/%s-prim.ppm" % (OUT, name), str(SIZE)],
                       check=True, capture_output=True)
        prim_p = psnr(tgt_ppm, "%s/%s-prim.ppm" % (OUT, name))

        rows.append((name, mine_p, prim_p, mine_p - prim_p, mine_t, prim_t, prim_t / mine_t))
        print("%-9s  mine %6.2f dB %7.2fs   primitive %6.2f dB %7.2fs   %+.2f dB  %5.1fx"
              % (name, mine_p, mine_t, prim_p, prim_t, mine_p - prim_p, prim_t / mine_t),
              flush=True)

    with open("%s/results.csv" % OUT, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["image", "psnr_mine", "psnr_primitive", "psnr_delta",
                    "sec_mine", "sec_primitive", "speedup"])
        w.writerows(rows)

    n = len(rows)
    mp = sum(r[1] for r in rows) / n
    pp = sum(r[2] for r in rows) / n
    print("\n%d images, %d triangles at %dpx, %d thread(s)" % (n, N, SIZE, THREADS))
    print("mean PSNR      mine %.2f dB   primitive %.2f dB   delta %+.2f dB" % (mp, pp, mp - pp))
    print("mean time      mine %.2f s    primitive %.2f s    %.1fx"
          % (sum(r[4] for r in rows) / n, sum(r[5] for r in rows) / n,
             sum(r[5] for r in rows) / sum(r[4] for r in rows)))
    print("mine wins on quality: %d/%d" % (sum(1 for r in rows if r[3] > 0), n))


main()
