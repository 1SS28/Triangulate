# Triangulate

## Goal

Reconstruct a raster image using only geometric primitives and mathematical
optimization, without generative AI or learned models.

The objective is to create a beautiful algorithm while preserving as much of math's beauty as possible.

I find geometry particularly interesting so this project will also serve as a good learning opportunity.

Given a target image I, find a set of geometric primitives theta such that:

    minimize L(I, R(theta))

where R(theta) is the image produced by rendering the primitives.

## Running

Drop any image into `images/`, then:

    ./run.sh cat.png [triangles] [candidates] [size] [threads] [guided] [refine]

Defaults are 200 triangles, 2000 candidates per triangle, a 256 pixel longest
side, a single thread, residual-guided placement, and 64 refinement steps.
The thread count never changes the output, and the kernel rarely does (see Two
kernels). The script builds, converts, runs, and opens the result.
Run it with no arguments to list the images available.

Everything generated goes to `out/`:

- `<name>-target.ppm` — the converted target the program actually fits
- `<name>.ppm` — the reconstruction
- `<name>_tris.txt` — one triangle per line, 10 parameters each

Runtime scales with triangles x candidates and only weakly with `size`, because
candidates are drawn at a fixed size in pixels: on kodim23 a 768 pixel run takes
about twice as long as a 256 pixel one. A larger image does need more triangles
to look as good.

To drive the pieces directly instead:

    make
    python3 tools/toppm.py images/cat.png out/cat-target.ppm 256
    ./geo out/cat-target.ppm 200 2000 out/cat 15 1 64

`tools/toppm.py` converts any image macOS can read into a binary PPM, using
sips plus a small PNG decoder, so there are no third-party dependencies.

## Benchmarking

`make bench` builds a runner that times the approximation loop alone -- no file
IO, no process startup -- and reports the median of several trials plus the
final error, so a faster kernel that quietly changed the answer is obvious.

    ./bench out/sample-target.ppm 200 2000 5 15

The rasterizer lives behind `kernel.h` and is swapped at link time, so backends
can be compared on the same image and triangle budget. `make compare` runs the
two CPU kernels head to head; to build one by hand:

    make bench KERNEL=kernel_direct.cpp

    # planned, not written yet
    make bench KERNEL=kernel_avx2.cpp CXXFLAGS="-O2 -Wall -pthread -mavx2"
    make bench KERNEL=kernel_hip.cpp CXX=hipcc

### Against fogleman/primitive

[Primitive](https://github.com/fogleman/primitive) is the reference
implementation of this idea: same greedy loop, same mean-colour start, same
hill-climbing refinement. `tools/kodak_bench.py` hands both fitters the identical
downscaled target and scores both outputs with `tools/psnr.py`.

That compares what the two programs save, which for primitive is not what its
search optimized. Primitive scores candidates on an aliased canvas, but the PNG
it saves comes from a separate anti-aliased renderer, and colours fitted to one
coverage fit the other less well: the saved PNG is 0.89 dB worse than the canvas,
0.38 to 2.09 dB per image. So the fair comparison is each search on its own
canvas. Primitive logs its canvas error with `-v`, as RMSE over RGBA; alpha never
differs, so the RGB MSE is that squared times 4/3.

All 24 Kodak images, 350 triangles at 512 px longest side, one thread, timed on
an idle machine:

|                                          | mean PSNR | time per image |
|------------------------------------------|-----------|----------------|
| primitive, its canvas                    |  25.87 dB |         21.3 s |
| primitive, saved PNG                     |  24.98 dB |                |
| this project, 2,000 candidates (default) |  25.02 dB |         0.35 s |
| this project, 20,143 candidates          |  25.53 dB |         3.29 s |
| this project, 130,619 candidates         |  25.84 dB |         22.6 s |

Time is the fitting loop alone, without startup or file IO. At its defaults this
project is 60x faster and 0.85 dB behind, on every one of the 24 images. With
20,143 candidates, plus the 64 refinement steps, it makes as many evaluations per
triangle as primitive, about 20,200, and is still 0.34 dB behind in 6.5x less
time: primitive's search gets more out of each evaluation. With 130,619 it takes
about primitive's time, and the two are level: 0.02 dB apart, 95% interval -0.13
to +0.08 dB. Against primitive's saved PNG the defaults are level too, +0.04 dB.

Where the 60x comes from:

| version                       | evals/triangle | per eval | mean time |
|-------------------------------|----------------|----------|-----------|
| primitive, Go, O(area)        |         20,203 |  3.01 us |    21.3 s |
| direct kernel, C++, O(area)   |          2,064 |  1.29 us |    0.93 s |
| prefix kernel, C++, O(height) |          2,064 |  0.49 us |    0.35 s |

Three factors multiply to it: primitive makes 9.8x as many evaluations per
triangle, each costs it 2.3x what the direct kernel pays, and prefix sums cut the
cost of an evaluation another 2.6x. The smaller search is the biggest factor, and
most of the quality gap too; the prefix reformulation is the smallest. Worth
stating plainly, since 60x invites the wrong conclusion. The 2.3x also
understates the implementation gap: this project's candidates cover 1.8x more
pixels, so per pixel primitive is about 4x slower.

Primitive's alpha 128 is 0.502 against 0.5 here. It seeds itself from the clock,
so its PSNRs are means of three runs; this project's are means of five seeds,
three for the two larger budgets. Both have a run-to-run standard deviation of
about 0.04 dB.

### Reconstruction quality

Measured on kodim23 from the Kodak set, the standard benchmark photo for image
reconstruction work, so the numbers are reproducible:

    curl -o images/kodim23.png https://r0k.us/graphics/kodak/kodak/kodim23.png
    ./run.sh kodim23.png 350 2000 512

| resolution | flat-colour baseline | plain greedy | current default | % of baseline |
|------------|----------------------|--------------|-----------------|---------------|
| 256 x 170  |               5650.0 |        309.8 |           197.9 |          3.5% |
| 512 x 341  |              23147.3 |       1802.6 |          1174.1 |          5.1% |

350 triangles, 2000 candidates each, mean over five seeds. "Plain greedy" is
uniform placement with no refinement; the default adds both, below. The
percentage is resolution-dependent -- the same triangle budget covers
proportionally less detail as the image grows -- so quote it with the size.

### Candidate placement and refinement

Two cheap additions to the greedy loop, both switchable so the plain version
stays the baseline they are measured against.

**Residual-guided placement.** Candidate centres are drawn from the error that is
left rather than uniformly over the canvas, so triangles get proposed where the
reconstruction is still wrong. The residual is held as a sum per 8x8 tile; after
each committed triangle only the tiles it overlapped are refreshed, so the map
costs one pass at startup and O(bbox) per round. Sampling single pixels instead
of tiles was worse -- it piles every candidate onto the one worst pixel. 8x8 beat
4, 16 and 32.

**Vertex refinement.** The winner is then hill-climbed: move one of its six
coordinates at random, keep the move if the error drops, shrink the step when it
does not. 64 of those is 3% more `fit()` calls than the 2000 random draws already
cost, and buys far more than 3% -- the best of a few thousand random triangles is
nowhere near a local optimum.

kodim23 at 512x341, 350 triangles x 2000 candidates, mean over five seeds:

| placement | refinement | error  | vs plain |
|-----------|------------|--------|----------|
| uniform   | off        | 1802.6 |    --    |
| residual  | off        | 1709.7 |    5.2%  |
| uniform   | 64 steps   | 1211.5 |   32.8%  |
| residual  | 64 steps   | 1174.1 |   34.9%  |

Refinement does most of that and partly subsumes the placement, since a
hill-climb can walk a badly placed triangle somewhere better. Placement keeps
3-5% on top, and more of it when candidates are scarce:

| candidates | uniform | residual | better by |   time |
|------------|---------|----------|-----------|--------|
|        125 |  1514.7 |   1440.1 |      4.9% |  55 ms |
|        500 |  1350.1 |   1297.9 |      3.9% | 114 ms |
|       2000 |  1211.5 |   1174.1 |      3.1% | 365 ms |

With both on, 125 candidates reaches 1440 in 55 ms, against 1803 in 366 ms for
plain greedy: 20% lower error in 15% of the time. On this image the two also
narrow the spread between seeds, from a standard deviation of 1.8% of the error
to 0.7%, but that does not generalize: over all 24 Kodak images it is about 1%
with or without them.

Switch either off to compare -- `./run.sh kodim23.png 350 2000 512 1 0 0` is
plain greedy.

### CPU optimization so far

Apple M5 Pro, 256x256, 200 triangles, 2000 candidates each, whole-process wall
time. Every step below leaves the output bit-identical -- same 200 triangles,
same parameters -- except the fused pass, which lowers error by solving colour
exactly instead of taking the mean.

Every row is `/usr/bin/time -p` on the same command, as plain greedy -- uniform
placement, no refinement -- so they stay comparable with the V1 baseline.

| version                            | threads | time   | vs prev | error |
|------------------------------------|---------|--------|---------|-------|
| V1 random search                   |       1 | 2.29 s |    --   | 203.6 |
| fused color fit + scoring          |       1 | 1.01 s |  2.27x  | 145.9 |
| incremental edge functions         |       1 | 0.80 s |  1.26x  | 145.9 |
| scanline spans                     |       1 | 0.40 s |  2.00x  | 145.9 |
| prefix-sum kernel                  |       1 | 0.15 s |  2.67x  | 145.9 |
| prefix-sum kernel                  |       8 | 0.06 s |  2.50x  | 145.9 |

**15x single-threaded, 38x end to end.** The single-thread column is the honest
measure of the algorithmic work; the last row is what adding cores buys on top.

### Two kernels

`fit()` needs three sums over the covered pixels, and all three are polynomials
in five quantities that do not depend on the candidate's colour or alpha:

    Su  = (1-a)Sv - St
    Su2 = (1-a)^2 Sv2 - 2(1-a) Svt + St2
    Se2 = Sv2 - 2 Svt + St2

Coverage is one contiguous span per scanline, so row-wise prefix sums of
v, t, v^2, t^2 and vt turn each row into two lookups per quantity. `fit()` then
costs O(height) instead of O(area). St and St2 never change; the other three are
rebuilt only for the rows a committed triangle touched.

Both kernels are kept, because they behave differently under threading.

kodim23 at 256x170, 350 triangles x 2000 candidates, default settings, median of
three runs on an idle machine:

| threads | direct   | prefix   | prefix is |
|---------|----------|----------|-----------|
|       1 | 830.5 ms | 272.4 ms |     3.05x |
|       2 | 455.8 ms | 169.4 ms |     2.69x |
|       4 | 263.8 ms | 113.1 ms |     2.33x |
|       5 | 228.9 ms | 104.8 ms |     2.18x |
|       8 | 189.8 ms | 104.2 ms |     1.82x |
|      15 | 153.7 ms | 120.4 ms |     1.28x |

The same at 512x341:

| threads | direct   | prefix   | prefix is |
|---------|----------|----------|-----------|
|       1 | 953.7 ms | 356.6 ms |     2.67x |
|       2 | 545.7 ms | 216.0 ms |     2.53x |
|       4 | 331.5 ms | 142.3 ms |     2.33x |
|       5 | 287.9 ms | 127.6 ms |     2.26x |
|       8 | 242.0 ms | 137.8 ms |     1.76x |
|      15 | 209.3 ms | 159.0 ms |     1.32x |

The M5 Pro has 5 super cores sharing a 16 MB L2, and 10 performance cores in two
clusters of five with 8 MB each. The prefix kernel is faster at every size and
thread count. At these two sizes it stops gaining at 5 or 6 threads and then
slows down, while the direct kernel keeps gaining to 15 without catching up: the
fastest run at 512x341 is the prefix kernel on 5 threads, 128 ms, against 209 ms
for the direct kernel on all 15. At 768x512 the prefix kernel's lead is 1.81x on
one thread and 1.37x on 15.

Memory is not the main reason the lead shrinks with threads: it shrinks fastest
at 256x170, where the prefix kernel's 5 MB of tables fit in either L2. Timing
each phase of the same configuration, at 512x341 unless noted, shows three costs
that come first:

- Candidates are generated on one thread, so that the result does not depend on
  the thread count. That takes 41-45 ms per image at any thread count: 12% of
  the prefix kernel's one-thread time, but only 5% of the direct kernel's.
- The prefix kernel's rounds are short. Scoring 2,000 candidates on 5 threads
  takes it about 195 us, against 528 us for the direct kernel, so the fixed cost
  of each round (see Thread scaling below) weighs more, and its scoring stops
  speeding up at 5 threads at either size. With rounds ten times longer it speeds
  up again from 5 to 15 threads, 2.1x at 256x170 and 1.5x at 512x341.
- The table rebuild after each committed triangle gets slower past 5 threads,
  from 7 ms per image on 5 to 29 ms on 15: a few dozen rows are too little work
  to split fifteen ways.

Memory does show up once those are out of the way. With the long rounds the
prefix kernel's 2.1x at 256x170 is close to the direct kernel's 2.4x, but at
512x341, where its 20 MB of tables fit in neither L2, it gets 1.5x against 2.4x.
Size costs it on one thread too: candidates are the same size in pixels at every
resolution, so from 512x341 to 768x512 the direct kernel's scoring time barely
moves, 828 to 888 ms, while the prefix kernel's goes from 273 to 460 ms as its
tables grow to 45 MB, consistent with cache misses on scattered lookups.

Refinement, which also runs on one thread, favours the prefix kernel too: it
costs the direct kernel 52 ms per image and the prefix kernel 9. Without
placement or refinement, in plain greedy, the direct kernel draws level by 15
threads: 131 ms against 129.

The two kernels agree bit for bit on 22 of the 24 Kodak images, and that they
differ on the other two is inherent rather than a bug. Refinement scores a
triangle against a slightly perturbed copy of itself, so the two scores are
genuinely close, and the kernels sum in different orders -- prefix sums of
doubles against per-pixel accumulation. Close enough scores let one kernel accept
a move the other rejects. On kodim02 a single flip at triangle 27 leaves 321 of
the 350 triangles different, for a PSNR difference of 0.08 dB (29.46 against
29.54); on kodim24 the first triangle already differs, and the two end 0.04 dB
apart. Exact reproducibility across kernels is not guaranteed, so compare them on
aggregate quality and time, not on a diff. Thread count is a separate matter --
within one kernel the output is bit-identical at any thread count, checked for the
prefix kernel on all 24 Kodak images at 1, 2, 8 and 15 threads.

Two findings on the way there, both from measuring rather than guessing. The
five sums were first stored as five separate arrays, which was only 1.41x --
each row cost five cache lines instead of one. Interleaving them at each x took
it to 2.83x. And the prefix rebuild after each draw was serial, which capped
threaded performance until it was parallelized over rows.

### Thread scaling

Scoring is parallel; candidates are still generated on one thread so the rand()
stream does not depend on the thread count, the winner is chosen by an ascending
scan with a strict compare, and refinement runs on the committing thread.
Results are therefore bit-identical at every thread count, checked on all 24
Kodak images at 1, 2, 8 and 15 threads -- worth checking, since a benchmark that
quietly changes the work it measures is worthless. This holds within a kernel;
across kernels it fails on 2 of the 24 images, for the reason given above.

Efficiency also depends on round length, because every round ends in a barrier.
Holding total work fixed at 400,000 candidates on the direct kernel:

| rounds x candidates | 1 thread | 15 threads | speedup |
|---------------------|----------|------------|---------|
| 200 x 2,000         |   419 ms |      68 ms |   6.1x  |
| 50 x 8,000          |   418 ms |      55 ms |   7.6x  |
| 20 x 20,000         |   417 ms |      51 ms |   8.3x  |

That is roughly 89 us of synchronization per round. Static chunking was worse
again: candidate triangles vary ~70x in area, so threads sat idle waiting on
whoever drew the big ones. Pulling small batches off an atomic cursor recovered
most of the loss.

Still to do: AVX2 and a HIP port, benchmarked on one machine so the GPU
comparison is honest. The direct kernel may be the better GPU candidate -- it
reads contiguously, where the prefix kernel's lookups are scattered.

## Initial Constraints

- Start with triangles only
- No AI / ML
- Minimal reliance on computer vision libraries
- Implement optimization and geometric operations manually
- Input: raster image
- Output:
  - reconstructed image
  - list of geometric primitives
  - reconstruction error

## Triangle Representation

Each triangle:

T = (x1, y1, x2, y2, x3, y3, r, g, b, alpha)

Parameters:
- 6 geometric parameters
- 3 color parameters
- 1 opacity parameter

## Initial Algorithm

1. Load target image
2. Initialize canvas to target's mean color
3. Calculate residual error

       E(x,y) = I(x,y) - R(x,y)

4. Select a high-error region
5. Initialize a triangle in that region
6. Optimize:
   - vertices
   - color
   - opacity
7. Add triangle to reconstruction
8. Repeat until N triangles or convergence


## Optimization

Start:
- coordinate descent
- finite-difference gradients

Later investigate:
- simulated annealing
- alternative initialization strategies
- analytical solutions for color / opacity
- global re-optimization

## Questions to Investigate

- How should the next primitive be initialized?
- Can optimal color be solved analytically?
- How should triangle size depend on local error?
- When should optimization stop?
- Is it better to add new triangles or re-optimize old ones?
- How does reconstruction error scale with primitive count?
- Are mixed primitives more efficient than triangles alone?

## Milestones

### V0
Render a triangle mathematically.

### V1
Approximate a simple image using triangles.

### V2
Residual-based triangle placement. Done -- 5% lower error at equal cost.

### V3
Optimize triangle geometry numerically. Done -- vertex hill-climbing, 33% lower
error for 3% more candidate evaluations.

### V4
Compare optimization methods.

### V5
Add complexity regularization / multiple primitive types.
