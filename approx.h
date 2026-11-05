#pragma once

#include "kernel.h"

struct Run {
    double e0, e;       // error after mean fill, and after the last triangle
    vector<float> canvas;
    vector<Tri> tris;
};

// Greedy search: each round keeps the best of `tries` random candidates and
// commits it. Seeded, so two runs of the same arguments are identical.
inline Run approximate(const vector<float> &target, int W, int H, int N, int tries, unsigned seed, bool verbose) {
    Run r;
    r.canvas = meanCanvas(target, W, H);
    r.e0 = loss(r.canvas, target);
    r.e = r.e0;
    srand(seed);

    for (int n = 0; n < N; n++) {
        Tri best;
        double bestScore = 0;  // 0 = no better than nothing
        for (int k = 0; k < tries; k++) {
            Tri t = randomTri(W, H);
            double s = fit(t, r.canvas, target, W, H);
            if (s < bestScore) { bestScore = s; best = t; }
        }
        if (bestScore == 0) {
            if (verbose) printf("no improving triangle found at %d, stopping\n", n);
            break;
        }
        draw(best, r.canvas, W, H);
        r.e += bestScore;
        r.tris.push_back(best);
        if (verbose && n % 20 == 19) printf("%4d triangles  error %.1f  (%.1f%% of start)\n", n + 1, r.e, 100 * r.e / r.e0);
    }
    return r;
}
