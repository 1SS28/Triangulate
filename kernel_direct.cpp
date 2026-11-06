#include <cmath>

#include "kernel.h"

const char *KERNEL_NAME = "direct";

// Straight per-pixel accumulation over each covered span. Kept alongside the
// prefix-sum kernel because the two behave very differently under threading:
// this one does more arithmetic but reads contiguously, so it keeps scaling
// where the table lookups go latency-bound.
struct Ctx {
    int W, H;
    vector<float> canvas;
    vector<float> target;
};

Ctx *ctxCreate(const vector<float> &target, int W, int H) {
    Ctx *g = new Ctx;
    g->W = W; g->H = H;
    g->target = target;
    g->canvas = meanCanvas(target, W, H);
    return g;
}

void ctxFree(Ctx *g) { delete g; }

const vector<float> &ctxCanvas(const Ctx *g) { return g->canvas; }

// A convex triangle covers one contiguous span per scanline. Each edge gives a
// half-line constraint w + g*(x-bx0) >= 0, so intersecting the three gives the
// span directly, with no per-pixel test.
static void span(float w0, float w1, float w2, float g0, float g1, float g2,
                 int bx0, int bx1, int &xs, int &xe) {
    xs = bx0; xe = bx1;
    float w[3] = {w0, w1, w2}, g[3] = {g0, g1, g2};
    for (int i = 0; i < 3; i++) {
        if (g[i] > 0) {
            int m = bx0 + (int)ceil(-w[i] / g[i]);
            if (m > xs) xs = m;
        } else if (g[i] < 0) {
            int m = bx0 + (int)floor(-w[i] / g[i]) + 1;
            if (m < xe) xe = m;
        } else if (w[i] < 0) {
            xe = xs;  // this edge excludes the whole row
            return;
        }
    }
    if (xe < xs) xe = xs;  // constraints can cross: empty row, not a negative count
    // the divisions round, so settle the two ends against the exact test
    while (xs < xe && (w0 + g0 * (xs - bx0) < 0 || w1 + g1 * (xs - bx0) < 0 || w2 + g2 * (xs - bx0) < 0)) xs++;
    while (xe > xs && (w0 + g0 * (xe - 1 - bx0) < 0 || w1 + g1 * (xe - 1 - bx0) < 0 || w2 + g2 * (xe - 1 - bx0) < 0)) xe--;
}

// Edge setup shared by fit and draw: row-start weights and their per-pixel and
// per-row increments, normalized by 1/area so they are barycentric weights.
struct Walk { float r0, r1, r2, gx0, gx1, gx2, gy0, gy1, gy2; int bx0, by0, bx1, by1; bool ok; };

static Walk setup(const Tri &t, int W, int H) {
    Walk k;
    k.ok = false;
    float area = edge(t.x1, t.y1, t.x2, t.y2, t.x3, t.y3);
    if (area < 1e-6f && area > -1e-6f) return k;  // degenerate
    float inv = 1.0f / area;
    box(t, W, H, k.bx0, k.by0, k.bx1, k.by1);
    float px = k.bx0 + 0.5f, py = k.by0 + 0.5f;
    k.r0 = edge(t.x2, t.y2, t.x3, t.y3, px, py) * inv;
    k.r1 = edge(t.x3, t.y3, t.x1, t.y1, px, py) * inv;
    k.r2 = edge(t.x1, t.y1, t.x2, t.y2, px, py) * inv;
    k.gx0 = -(t.y3 - t.y2) * inv; k.gy0 = (t.x3 - t.x2) * inv;
    k.gx1 = -(t.y1 - t.y3) * inv; k.gy1 = (t.x1 - t.x3) * inv;
    k.gx2 = -(t.y2 - t.y1) * inv; k.gy2 = (t.x2 - t.x1) * inv;
    k.ok = true;
    return k;
}

// Drawing colour c over the covered pixels makes the error sum
// n(ac)^2 + 2ac*Su + Su2, which is quadratic in c and minimized at c = -Su/(na).
// Both the colour and the resulting delta come out of the same five sums.
double fit(Tri &t, const Ctx *g) {
    Walk k = setup(t, g->W, g->H);
    if (!k.ok) return 0;

    int W = g->W, WH = W * g->H;
    double a = t.a, ia = 1 - a;
    double sU[3] = {0, 0, 0}, sU2[3] = {0, 0, 0}, sE2[3] = {0, 0, 0};
    int n = 0;
    for (int y = k.by0; y < k.by1; y++) {
        int xs, xe;
        span(k.r0, k.r1, k.r2, k.gx0, k.gx1, k.gx2, k.bx0, k.bx1, xs, xe);
        for (int c = 0; c < 3; c++) {
            const float *cv = &g->canvas[c * WH + y * W], *tg = &g->target[c * WH + y * W];
            for (int x = xs; x < xe; x++) {
                double v = cv[x], tt = tg[x];
                double u = ia * v - tt, e = v - tt;
                sU[c] += u; sU2[c] += u * u; sE2[c] += e * e;
            }
        }
        n += xe - xs;
        k.r0 += k.gy0; k.r1 += k.gy1; k.r2 += k.gy2;
    }
    if (n == 0) return 0;

    double d = 0;
    float *col[3] = {&t.r, &t.g, &t.b};
    for (int c = 0; c < 3; c++) {
        double ck = -sU[c] / (n * a);  // optimal colour for this channel
        if (ck < 0) ck = 0;
        if (ck > 1) ck = 1;
        *col[c] = ck;
        d += n * a * a * ck * ck + 2 * a * ck * sU[c] + sU2[c] - sE2[c];
    }
    return d;
}

void draw(const Tri &t, Ctx *g, Pool *pool) {
    Walk k = setup(t, g->W, g->H);
    if (!k.ok) return;
    int W = g->W, WH = W * g->H;
    float col[3] = {t.r, t.g, t.b};
    int first = W;
    for (int y = k.by0; y < k.by1; y++) {
        int xs, xe;
        span(k.r0, k.r1, k.r2, k.gx0, k.gx1, k.gx2, k.bx0, k.bx1, xs, xe);
        if (xe > xs && xs < first) first = xs;
        for (int c = 0; c < 3; c++) {
            float *p = &g->canvas[c * WH + y * W];
            for (int x = xs; x < xe; x++) p[x] = t.a * col[c] + (1 - t.a) * p[x];  // alpha over
        }
        k.r0 += k.gy0; k.r1 += k.gy1; k.r2 += k.gy2;
    }
    (void)first; (void)pool;  // nothing to rebuild
}
