#pragma once

#include "geo.h"

// Where the error still is, summed over coarse tiles. Candidate centers come
// from this instead of a uniform draw, so triangles land on the parts of the
// image that are still wrong.
//
// Tiles rather than pixels for two reasons: a per-pixel distribution piles every
// candidate onto the single worst pixel, and tile sums refresh in O(bbox) after a
// draw instead of O(WH).
static const int TILE = 8;

struct Residual {
    int W, H, tw, th;
    vector<double> tile;  // squared error per tile
    vector<double> cdf;   // running total, for sampling
};

inline double tileErr(const Residual &R, int ti, int tj, const vector<float> &canvas, const vector<float> &target) {
    int x0 = ti * TILE, x1 = x0 + TILE, y0 = tj * TILE, y1 = y0 + TILE;
    if (x1 > R.W) x1 = R.W;
    if (y1 > R.H) y1 = R.H;
    int WH = R.W * R.H;
    double s = 0;
    for (int c = 0; c < 3; c++)
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                size_t i = (size_t)c * WH + (size_t)y * R.W + x;
                double d = canvas[i] - target[i];
                s += d * d;
            }
    return s;
}

inline void resCdf(Residual &R) {
    double run = 0;
    for (size_t i = 0; i < R.tile.size(); i++) { run += R.tile[i]; R.cdf[i] = run; }
}

inline void resInit(Residual &R, const vector<float> &canvas, const vector<float> &target, int W, int H) {
    R.W = W; R.H = H;
    R.tw = (W + TILE - 1) / TILE;
    R.th = (H + TILE - 1) / TILE;
    R.tile.assign((size_t)R.tw * R.th, 0);
    R.cdf.resize(R.tile.size());
    for (int tj = 0; tj < R.th; tj++)
        for (int ti = 0; ti < R.tw; ti++) R.tile[(size_t)tj * R.tw + ti] = tileErr(R, ti, tj, canvas, target);
    resCdf(R);
}

// Only the tiles the committed triangle overlapped can have changed.
inline void resUpdate(Residual &R, const Tri &t, const vector<float> &canvas, const vector<float> &target) {
    int bx0, by0, bx1, by1;
    box(t, R.W, R.H, bx0, by0, bx1, by1);
    if (bx1 <= bx0 || by1 <= by0) return;
    int ti0 = bx0 / TILE, ti1 = (bx1 - 1) / TILE;
    int tj0 = by0 / TILE, tj1 = (by1 - 1) / TILE;
    for (int tj = tj0; tj <= tj1; tj++)
        for (int ti = ti0; ti <= ti1; ti++) R.tile[(size_t)tj * R.tw + ti] = tileErr(R, ti, tj, canvas, target);
    resCdf(R);
}

// Pick a tile with probability proportional to its error, then a point inside it.
inline void resSample(const Residual &R, float &cx, float &cy) {
    double total = R.cdf.back();
    if (total <= 0) { cx = frand() * R.W; cy = frand() * R.H; return; }  // nothing left to fix
    double u = frand() * total;
    int lo = 0, hi = (int)R.cdf.size() - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (R.cdf[mid] < u) lo = mid + 1;
        else hi = mid;
    }
    cx = (lo % R.tw + frand()) * TILE;
    cy = (lo / R.tw + frand()) * TILE;
}
