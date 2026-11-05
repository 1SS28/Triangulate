#include <cmath>

#include "kernel.h"

const char *KERNEL_NAME = "scalar";

// A convex triangle covers one contiguous span per scanline. Each edge gives a
// half-line constraint w + g*(x-bx0) >= 0, so intersecting the three gives the span
// directly -- no per-pixel test, and the run is contiguous for SIMD and for the GPU.
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
// per-row increments, all normalized by 1/area so they are barycentric weights.
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

// Write u = (1-a)v - t for a covered pixel, e = v - t. Drawing color c makes the
// error sum n(ac)^2 + 2ac*Su + Su2, so the delta is quadratic in c -- minimized at
// c = -Su/(na), and both fall out of the same three running sums. One pass.
double fit(Tri &t, const vector<float> &canvas, const vector<float> &target, int W, int H) {
    Walk k = setup(t, W, H);
    if (!k.ok) return 0;

    double sU[3] = {0, 0, 0}, sU2[3] = {0, 0, 0}, sE2[3] = {0, 0, 0};
    int n = 0;
    float a = t.a;
    for (int y = k.by0; y < k.by1; y++) {
        int xs, xe;
        span(k.r0, k.r1, k.r2, k.gx0, k.gx1, k.gx2, k.bx0, k.bx1, xs, xe);
        for (int x = xs; x < xe; x++) {
            int i = (y * W + x) * 3;
            for (int c = 0; c < 3; c++) {
                double v = canvas[i + c], tt = target[i + c];
                double u = (1 - a) * v - tt, e = v - tt;
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
        double ck = -sU[c] / (n * a);  // optimal color for this channel
        if (ck < 0) ck = 0;
        if (ck > 1) ck = 1;
        *col[c] = ck;
        d += n * a * a * ck * ck + 2 * a * ck * sU[c] + sU2[c] - sE2[c];
    }
    return d;
}

void draw(const Tri &t, vector<float> &canvas, int W, int H) {
    Walk k = setup(t, W, H);
    if (!k.ok) return;
    float col[3] = {t.r, t.g, t.b};
    for (int y = k.by0; y < k.by1; y++) {
        int xs, xe;
        span(k.r0, k.r1, k.r2, k.gx0, k.gx1, k.gx2, k.bx0, k.bx1, xs, xe);
        for (int x = xs; x < xe; x++) {
            float *p = &canvas[(y * W + x) * 3];
            for (int c = 0; c < 3; c++) p[c] = t.a * col[c] + (1 - t.a) * p[c];  // alpha over
        }
        k.r0 += k.gy0; k.r1 += k.gy1; k.r2 += k.gy2;
    }
}
