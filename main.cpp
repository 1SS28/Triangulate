#include "approx.h"

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "target.ppm";
    int N = argc > 2 ? atoi(argv[2]) : 200;       // triangles
    int tries = argc > 3 ? atoi(argv[3]) : 2000;  // candidates per triangle
    const char *prefix = argc > 4 ? argv[4] : "out";

    char imgPath[512], triPath[512];
    snprintf(imgPath, sizeof imgPath, "%s.ppm", prefix);
    snprintf(triPath, sizeof triPath, "%s_tris.txt", prefix);

    int W, H;
    vector<float> target = readPPM(path, W, H);
    printf("target %s  %dx%d  kernel %s\n", path, W, H, KERNEL_NAME);

    Run r = approximate(target, W, H, N, tries, 12345, true);
    printf("error after mean fill: %.1f\n", r.e0);

    FILE *log = fopen(triPath, "w");
    for (size_t i = 0; i < r.tris.size(); i++) {
        Tri &t = r.tris[i];
        fprintf(log, "%.2f %.2f %.2f %.2f %.2f %.2f %.4f %.4f %.4f %.3f\n",
                t.x1, t.y1, t.x2, t.y2, t.x3, t.y3, t.r, t.g, t.b, t.a);
    }
    fclose(log);

    writePPM(imgPath, r.canvas, W, H);
    // recompute should match; catches drift in fit()
    printf("final error %.1f (recomputed %.1f)\n", r.e, loss(r.canvas, target));
    printf("wrote %s and %s\n", imgPath, triPath);
}
