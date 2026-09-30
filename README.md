# Geometric Image Approximation

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

    ./run.sh cat.png [triangles] [candidates] [size]

Defaults are 200 triangles, 2000 candidates per triangle, and a 256 pixel
longest side. The script builds, converts, runs, and opens the result.
Run it with no arguments to list the images available.

Everything generated goes to `out/`:

- `<name>-target.ppm` — the converted target the program actually fits
- `<name>.ppm` — the reconstruction
- `<name>_tris.txt` — one triangle per line, 10 parameters each

Runtime scales with pixels x triangles x candidates, so raise `size` only once
you know how long a run takes.

To drive the pieces directly instead:

    c++ -O2 -o geo main.cpp
    python3 tools/toppm.py images/cat.png out/cat-target.ppm 256
    ./geo out/cat-target.ppm 200 2000 out/cat

`tools/toppm.py` converts any image macOS can read into a binary PPM, using
sips plus a small PNG decoder, so there are no third-party dependencies.

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