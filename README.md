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

    ./run.sh cat.png [triangles] [candidates] [size] [threads]

Defaults are 200 triangles, 2000 candidates per triangle, a 256 pixel longest
side, and a single thread. Thread count does not change the output. The script builds, converts, runs, and opens the result.
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
    ./geo out/cat-target.ppm 200 2000 out/cat 15

`tools/toppm.py` converts any image macOS can read into a binary PPM, using
sips plus a small PNG decoder, so there are no third-party dependencies.

## Benchmarking

`make bench` builds a runner that times the approximation loop alone -- no file
IO, no process startup -- and reports the median of several trials plus the
final error, so a faster kernel that quietly changed the answer is obvious.

    ./bench out/sample-target.ppm 200 2000 5 15

The rasterizer lives behind `kernel.h` and is swapped at link time, so backends
can be compared on identical work. `make compare` runs the two CPU kernels head
to head:

    make bench KERNEL=kernel_direct.cpp
    make bench KERNEL=kernel_avx2.cpp CXXFLAGS="-O2 -Wall -pthread -mavx2"
    make bench KERNEL=kernel_hip.cpp CXX=hipcc

### Reconstruction quality

Measured on kodim23 from the Kodak set, the standard benchmark photo for image
reconstruction work, so the numbers are reproducible:

    curl -o images/kodim23.png https://r0k.us/graphics/kodak/kodak/kodim23.png
    ./run.sh kodim23.png 350 2000 512

| resolution | flat-colour baseline | 350 triangles | % of baseline |
|------------|----------------------|---------------|---------------|
| 256 x 170  |               5650.0 |         315.3 |          5.6% |
| 512 x 341  |              23147.3 |        1785.3 |          7.7% |

The percentage is resolution-dependent -- the same triangle budget covers
proportionally less detail as the image grows -- so quote it with the size.

### CPU optimization so far

Apple M5 Pro, 256x256, 200 triangles, 2000 candidates each, whole-process wall
time. Every step below leaves the output bit-identical -- same 200 triangles,
same parameters -- except the fused pass, which lowers error by solving colour
exactly instead of taking the mean.

| version                            | time   | vs prev | error |
|------------------------------------|--------|---------|-------|
| V1 random search                   | 2.29 s |    --   | 203.6 |
| fused color fit + scoring          | 1.01 s |  2.27x  | 145.9 |
| incremental edge functions         | 0.80 s |  1.26x  | 145.9 |
| scanline spans                     | 0.40 s |  2.00x  | 145.9 |
| 15 threads                         | 0.07 s |  5.71x  | 145.9 |
| prefix-sum kernel, 8 threads       | 0.06 s |  1.17x  | 145.9 |

38x end to end.

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

Both kernels are kept, because they behave differently under threading:

| threads | direct   | prefix   | prefix is |
|---------|----------|----------|-----------|
|       1 | 474.4 ms | 161.8 ms |     2.93x |
|       2 | 249.1 ms |  94.8 ms |     2.63x |
|       4 | 141.2 ms |  65.4 ms |     2.16x |
|       8 |  88.7 ms |  53.4 ms |     1.66x |
|      15 |  66.8 ms |  58.5 ms |     1.14x |

The advantage narrows as threads are added. The prefix kernel trades arithmetic
for table lookups, and the tables are far larger than cache -- 21 MB at
512x341 -- so it goes latency-bound and stops scaling past about 8 threads,
while the direct kernel reads contiguously and keeps scaling. Peak throughput
ends up close; the prefix kernel simply reaches it on half the cores.

Two findings on the way there, both from measuring rather than guessing. The
five sums were first stored as five separate arrays, which was only 1.41x --
each row cost five cache lines instead of one. Interleaving them at each x took
it to 2.83x. And the prefix rebuild after each draw was serial, which capped
threaded performance until it was parallelized over rows.

### Thread scaling

Scoring is parallel; candidates are still generated on one thread so the rand()
stream does not depend on the thread count, and the winner is chosen by an
ascending scan with a strict compare. Results are therefore bit-identical at
every thread count -- worth checking, since a benchmark that quietly changes the
work it measures is worthless.

The M5 Pro has 5 performance and 10 efficiency cores, and the curve bends where
the P cores run out.

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
Residual-based triangle placement.

### V3
Optimize triangle geometry numerically.

### V4
Compare optimization methods.

### V5
Add complexity regularization / multiple primitive types.
