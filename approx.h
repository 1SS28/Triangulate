#pragma once

#include "kernel.h"
#include "pool.h"

struct Run {
    double e0, e;       // error after mean fill, and after the last triangle
    vector<float> canvas;
    vector<Tri> tris;
};

// Greedy search: each round keeps the best of `tries` random candidates and
// commits it. Seeded, so two runs of the same arguments are identical.
//
// Candidates are generated up front on one thread, so the rand() stream -- and
// therefore the result -- does not depend on the thread count. Only the scoring
// is parallel, which is 99% of the work anyway.
inline Run approximate(const vector<float> &target, int W, int H, int N, int tries, unsigned seed, int threads, bool verbose) {
    Run r;
    r.canvas = meanCanvas(target, W, H);
    r.e0 = loss(r.canvas, target);
    r.e = r.e0;
    srand(seed);

    vector<Tri> cand(tries);
    vector<double> score(tries);
    Pool *pool = threads > 1 ? new Pool(threads) : 0;

    for (int n = 0; n < N; n++) {
        for (int k = 0; k < tries; k++) cand[k] = randomTri(W, H);

        function<void(int, int)> job = [&](int lo, int hi) {
            for (int k = lo; k < hi; k++) score[k] = fit(cand[k], r.canvas, target, W, H);
        };
        if (pool) pool->run(tries, job);
        else job(0, tries);

        // ascending scan with a strict compare, so ties go to the lowest index
        // and the winner matches the single-threaded run exactly
        int best = -1;
        double bestScore = 0;  // 0 = no better than nothing
        for (int k = 0; k < tries; k++) {
            if (score[k] < bestScore) { bestScore = score[k]; best = k; }
        }
        if (best < 0) {
            if (verbose) printf("no improving triangle found at %d, stopping\n", n);
            break;
        }

        draw(cand[best], r.canvas, W, H);
        r.e += bestScore;
        r.tris.push_back(cand[best]);
        if (verbose && n % 20 == 19) printf("%4d triangles  error %.1f  (%.1f%% of start)\n", n + 1, r.e, 100 * r.e / r.e0);
    }

    delete pool;
    return r;
}
