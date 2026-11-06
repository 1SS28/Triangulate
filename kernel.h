#pragma once

#include "geo.h"
#include "pool.h"

// Which backend this binary was linked against. Swap implementations at link
// time (kernel_scalar.cpp, later kernel_avx2.cpp, kernel_hip.cpp) so bench can
// compare them on identical work.
extern const char *KERNEL_NAME;

// Opaque per-backend state: the canvas plus whatever that backend needs to score
// a candidate quickly. The scalar kernel keeps prefix sums here; a GPU kernel
// would keep device buffers. Nothing outside the kernel looks inside.
struct Ctx;

Ctx *ctxCreate(const vector<float> &target, int W, int H);
void ctxFree(Ctx *g);
const vector<float> &ctxCanvas(const Ctx *g);

// Sets t's color to the optimum and returns the change in total squared error.
// Negative means the reconstruction improves. Read-only on the context, so it is
// safe to call from many threads at once.
double fit(Tri &t, const Ctx *g);

// Commits a triangle. Mutates the context, so it is called from one thread --
// but the prefix rebuild it triggers is row-independent, and at high thread
// counts that rebuild is the serial bottleneck, so it takes the pool.
void draw(const Tri &t, Ctx *g, Pool *pool);
