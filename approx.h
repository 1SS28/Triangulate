#pragma once

#include "kernel.h"
#include "pool.h"
#include "residual.h"

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
//
// guided samples candidate centers from the residual map; otherwise they are
// uniform over the canvas. Kept switchable so the two are comparable.
inline Run approximate(const vector<float> &target, int W, int H, int N, int tries, unsigned seed, int threads, bool guided, bool verbose) {
    Run r;
    Ctx *ctx = ctxCreate(target, W, H);
    r.e0 = loss(ctxCanvas(ctx), target);
    r.e = r.e0;
    srand(seed);

    vector<Tri> cand(tries);
    vector<double> score(tries);
    Pool *pool = threads > 1 ? new Pool(threads) : 0;
    Residual res;
    if (guided) resInit(res, ctxCanvas(ctx), target, W, H);

    for (int n = 0; n < N; n++) {
        for (int k = 0; k < tries; k++) {
            if (!guided) { cand[k] = randomTri(W, H); continue; }
            float cx, cy;
            resSample(res, cx, cy);
            cand[k] = triAt(cx, cy);
        }

        function<void(int, int)> job = [&](int lo, int hi) {
            for (int k = lo; k < hi; k++) score[k] = fit(cand[k], ctx);
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

        draw(cand[best], ctx, pool);
        if (guided) resUpdate(res, cand[best], ctxCanvas(ctx), target);
        r.e += bestScore;
        r.tris.push_back(cand[best]);
        if (verbose && n % 20 == 19) printf("%4d triangles  error %.1f  (%.1f%% of start)\n", n + 1, r.e, 100 * r.e / r.e0);
    }

    r.canvas = ctxCanvas(ctx);
    ctxFree(ctx);
    delete pool;
    return r;
}
