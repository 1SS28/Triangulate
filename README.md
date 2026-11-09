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
Neither the thread count nor the kernel changes the output. The script builds, converts, runs, and opens the result.
Run it with no arguments to list the images available.

Everything generated goes to `out/`:

- `<name>-target.ppm` — the converted target the program actually fits
- `<name>.ppm` — the reconstruction
- `<name>_tris.txt` — one triangle per line, 10 parameters each

Runtime scales with pixels x triangles x candidates, so raise `size` only once
you know how long a run takes.

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
can be compared on the same image and triangle budget. `make compare` runs the two CPU kernels head
to head:

    make bench KERNEL=kernel_direct.cpp
    make bench KERNEL=kernel_avx2.cpp CXXFLAGS="-O2 -Wall -pthread -mavx2"
    make bench KERNEL=kernel_hip.cpp CXX=hipcc

### Against fogleman/primitive

[Primitive](https://github.com/fogleman/primitive) is the reference
implementation of this idea: same greedy loop, same mean-colour start, same
hill-climbing refinement. `tools/kodak_bench.py` hands both fitters the identical
downscaled target and scores both outputs with `tools/psnr.py`, so the PSNRs are
comparable.

All 24 Kodak images, 350 triangles at 512 px longest side, one thread:

|                    | mean PSNR | mean time |
|--------------------|-----------|-----------|
| this project       | 25.02 dB  |    0.38 s |
| fogleman/primitive | 24.97 dB  |   21.25 s |

A tie on quality -- +0.04 dB, and primitive is ahead on 11 of the 24 -- in
1/56th the time. Where the speed comes from, holding the algorithm or the
language fixed:

| version                        | mean time | vs next |
|--------------------------------|-----------|---------|
| primitive, Go, O(area)         |   21.25 s |         |
| direct kernel, C++, O(area)    |    0.94 s |  22.7x  |
| prefix kernel, C++, O(height)  |    0.36 s |   2.6x  |

So most of the 56x is implementation rather than the complexity change: the
prefix reformulation is worth 2.6x, and the rest is the gap between this code and
primitive's. Worth stating plainly, since 56x invites the wrong conclusion.

Two caveats. The search budgets are not matched, only the output budget is --
2000 random draws plus 64 refinement steps per triangle here against primitive's
own schedule -- so this is quality per triangle, with time as a separate axis.
And primitive's defaults sit very close but not exactly on ours: alpha 128 is
0.502 against 0.5, and it reports RMSE where this reports SSE.

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
|        125 |  1514.7 |   1440.1 |      4.9% |  52 ms |
|        500 |  1350.1 |   1297.9 |      3.9% | 118 ms |
|       2000 |  1211.5 |   1174.1 |      3.1% | 391 ms |

With both on, 125 candidates reaches 1440 in 52 ms, against 1803 in 381 ms for
plain greedy: 20% lower error in a seventh of the time. Refinement also tightens
the spread between seeds from about +-2% to +-0.8%, because the answer depends
less on the luck of the draw.

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

sample, 256x256, 200 triangles x 2000 candidates:

| threads | direct   | prefix   | prefix is |
|---------|----------|----------|-----------|
|       1 | 474.4 ms | 161.8 ms |     2.93x |
|       2 | 249.1 ms |  94.8 ms |     2.63x |
|       4 | 141.2 ms |  65.4 ms |     2.16x |
|       8 |  88.7 ms |  53.4 ms |     1.66x |
|      15 |  66.8 ms |  58.5 ms |     1.14x |

kodim23, 512x341, 350 triangles x 2000 candidates:

| threads | direct   | prefix   | prefix is |
|---------|----------|----------|-----------|
|       1 | 904.4 ms | 491.5 ms |     1.84x |
|       2 | 476.1 ms | 220.0 ms |     2.16x |
|       4 | 256.9 ms | 135.8 ms |     1.89x |
|       8 | 167.9 ms | 118.2 ms |     1.42x |
|      15 | 130.5 ms | 138.7 ms |     0.94x |

The M5 Pro has 5 performance and 10 efficiency cores, and both curves bend where
the P cores run out. The prefix kernel's advantage narrows as threads are added,
and on the larger image the direct kernel overtakes at 15. The prefix kernel
trades arithmetic for table lookups, and the tables are far larger than cache --
21 MB at 512x341 -- so it goes latency-bound and stops scaling past about 8
threads, while the direct kernel reads contiguously and keeps scaling.

The fastest run of either kernel on kodim23 is the prefix kernel on 8 threads,
118 ms, which beats the direct kernel on all 15. Which kernel wins depends on
how many cores you are willing to spend.

The two kernels do not agree bit for bit once refinement is on, and that is
inherent rather than a bug. Refinement scores a triangle against a slightly
perturbed copy of itself, so the two scores are genuinely close, and the kernels
sum in different orders -- prefix sums of doubles against per-pixel accumulation.
Close enough scores let one kernel accept a move the other rejects. On kodim02 a
single flip at triangle 27 leaves 321 of the 350 triangles different, for a PSNR
difference of 0.08 dB (29.46 against 29.54). Quality is unaffected; exact
reproducibility across kernels is not available, so compare them on aggregate
quality and time, not on a diff. Thread count is a separate matter -- within one
kernel the output is bit-identical at any thread count, checked on all 24 Kodak
images at 1, 2, 8 and 15 threads.

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
across kernels it does not, for the reason given above.

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
comparison is honest. The direct kernel is the better GPU candidate -- it reads
contiguously and is bandwidth-bound, where the prefix kernel is latency-bound on
scattered lookups.

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
