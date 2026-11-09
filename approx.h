#pragma once

#include "kernel.h"
#include "pool.h"
#include "residual.h"

struct Run {
    double e0, e;       // error after mean fill, and after the last triangle
    vector<float> canvas;
    vector<Tri> tris;
};

// Hill-climb one vertex coordinate at a time, shrinking the step whenever a move
// fails. The best of a few thousand random draws is nowhere near a local optimum,
// and fit() is cheap, so a few dozen more evaluations per round buy a lot.
inline double refine(Tri &t, const Ctx *g, double score, int steps) {
    float step = 16;
    for (int i = 0; i < steps; i++) {
        Tri c = t;
        float *v[6] = {&c.x1, &c.y1, &c.x2, &c.y2, &c.x3, &c.y3};
        *v[rand() % 6] += (frand() * 2 - 1) * step;
        double s = fit(c, g);
        if (s < score) { score = s; t = c; }
        else step *= 0.93f;
    }
    return score;
}

// Greedy search: each round keeps the best of `tries` random candidates and
// commits it. Seeded, so two runs of the same arguments are identical.
//
// Candidates are generated up front on one thread, so the rand() stream -- and
// therefore the result -- does not depend on the thread count. Only the scoring
// is parallel, which is 99% of the work anyway.
//
// guided samples candidate centers from the residual map; otherwise they are
// uniform over the canvas. steps is the refinement budget for the winner. Both
// are switchable so the plain version stays the baseline they are measured against.
inline Run approximate(const vector<float> &target, int W, int H, int N, int tries, unsigned seed, int threads, bool guided, int steps, bool verbose) {
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

        if (steps > 0) bestScore = refine(cand[best], ctx, bestScore, steps);
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
