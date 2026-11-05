#pragma once

#include "geo.h"

// Which backend this binary was linked against. Swap implementations at link
// time (kernel_scalar.cpp, later kernel_avx2.cpp, kernel_hip.cpp) so bench can
// compare them on identical work.
extern const char *KERNEL_NAME;

// One rasterization: sets t's color to the optimum and returns the change in
// total squared error. Negative means the reconstruction improves.
double fit(Tri &t, const vector<float> &canvas, const vector<float> &target, int W, int H);

void draw(const Tri &t, vector<float> &canvas, int W, int H);
