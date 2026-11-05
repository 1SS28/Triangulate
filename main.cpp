#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace std;

struct Tri { float x1, y1, x2, y2, x3, y3, r, g, b, a; };

// signed area x2; sign says which side of a->b p is on
float edge(float ax, float ay, float bx, float by, float px, float py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }

// next int, skipping whitespace and # comments
int readInt(FILE *f) {
    int c = fgetc(f);
    while (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '#') {
        if (c == '#') while (c != '\n' && c != EOF) c = fgetc(f);
        c = fgetc(f);
    }
    int v = 0;
    while (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); c = fgetc(f); }
    return v;
}

vector<float> readPPM(const char *path, int &W, int &H) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); exit(1); }
    if (fgetc(f) != 'P' || fgetc(f) != '6') { printf("%s is not a binary PPM (P6)\n", path); exit(1); }
    W = readInt(f);
    H = readInt(f);
    readInt(f);  // maxval, assume 255
    vector<float> img(W * H * 3);
    for (int i = 0; i < W * H * 3; i++) img[i] = fgetc(f) / 255.0f;
    fclose(f);
    return img;
}

void writePPM(const char *path, const vector<float> &img, int W, int H) {
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6 %d %d 255\n", W, H);
    for (float v : img) {
        int c = (int)(v * 255 + 0.5f);  // blending can drift outside [0,1]
        if (c < 0) c = 0;
        if (c > 255) c = 255;
        fputc(c, f);
    }
    fclose(f);
}

// squared error over every channel
double loss(const vector<float> &a, const vector<float> &b) {
    double s = 0;
    for (size_t i = 0; i < a.size(); i++) { double d = a[i] - b[i]; s += d * d; }
    return s;
}

float frand() { return rand() / (float)RAND_MAX; }

int lo3(float a, float b, float c) { float m = a; if (b < m) m = b; if (c < m) m = c; return (int)m; }
int hi3(float a, float b, float c) { float m = a; if (b > m) m = b; if (c > m) m = c; return (int)m + 1; }

// random point, vertices scattered up to s away
Tri randomTri(int W, int H) {
    float cx = frand() * W, cy = frand() * H;
    float s = 8 + frand() * 60;
    Tri t;
    t.x1 = cx + (frand() * 2 - 1) * s; t.y1 = cy + (frand() * 2 - 1) * s;
    t.x2 = cx + (frand() * 2 - 1) * s; t.y2 = cy + (frand() * 2 - 1) * s;
    t.x3 = cx + (frand() * 2 - 1) * s; t.y3 = cy + (frand() * 2 - 1) * s;
    t.r = t.g = t.b = 0;  // fit() sets these
    t.a = 0.5f;
    return t;
}

// bounding box, clipped to canvas
void box(const Tri &t, int W, int H, int &bx0, int &by0, int &bx1, int &by1) {
    bx0 = lo3(t.x1, t.x2, t.x3); bx1 = hi3(t.x1, t.x2, t.x3);
    by0 = lo3(t.y1, t.y2, t.y3); by1 = hi3(t.y1, t.y2, t.y3);
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 > W) bx1 = W;
    if (by1 > H) by1 = H;
}

// inv = 1/area, so this multiplies instead of dividing; either winding works
bool inside(const Tri &t, float inv, float px, float py) {
    float w0 = edge(t.x2, t.y2, t.x3, t.y3, px, py) * inv;
    float w1 = edge(t.x3, t.y3, t.x1, t.y1, px, py) * inv;
    return w0 >= 0 && w1 >= 0 && 1 - w0 - w1 >= 0;
}

// One rasterization gives both the best color and the error it would cause.
// Write u = (1-a)v - t for a covered pixel, e = v - t. Drawing color c makes the
// error sum n(ac)^2 + 2ac*Su + Su2, so the delta is quadratic in c -- minimized at
// c = -Su/(na), and both fall out of the same three running sums.
double fit(Tri &t, const vector<float> &canvas, const vector<float> &target, int W, int H) {
    float area = edge(t.x1, t.y1, t.x2, t.y2, t.x3, t.y3);
    if (area < 1e-6f && area > -1e-6f) return 0;  // degenerate
    float inv = 1.0f / area;
    int bx0, by0, bx1, by1;
    box(t, W, H, bx0, by0, bx1, by1);

    double sU[3] = {0, 0, 0}, sU2[3] = {0, 0, 0}, sE2[3] = {0, 0, 0};
    int n = 0;
    float a = t.a;
    for (int y = by0; y < by1; y++)
        for (int x = bx0; x < bx1; x++) {
            if (!inside(t, inv, x + 0.5f, y + 0.5f)) continue;
            int i = (y * W + x) * 3;
            for (int k = 0; k < 3; k++) {
                double v = canvas[i + k], tt = target[i + k];
                double u = (1 - a) * v - tt, e = v - tt;
                sU[k] += u; sU2[k] += u * u; sE2[k] += e * e;
            }
            n++;
        }
    if (n == 0) return 0;

    double d = 0;
    float *c[3] = {&t.r, &t.g, &t.b};
    for (int k = 0; k < 3; k++) {
        double ck = -sU[k] / (n * a);  // optimal color for this channel
        if (ck < 0) ck = 0;
        if (ck > 1) ck = 1;
        *c[k] = ck;
        d += n * a * a * ck * ck + 2 * a * ck * sU[k] + sU2[k] - sE2[k];
    }
    return d;
}

void draw(const Tri &t, vector<float> &canvas, int W, int H) {
    float area = edge(t.x1, t.y1, t.x2, t.y2, t.x3, t.y3);
    if (area < 1e-6f && area > -1e-6f) return;
    float inv = 1.0f / area;
    int bx0, by0, bx1, by1;
    box(t, W, H, bx0, by0, bx1, by1);
    float c[3] = {t.r, t.g, t.b};
    for (int y = by0; y < by1; y++)
        for (int x = bx0; x < bx1; x++) {
            if (!inside(t, inv, x + 0.5f, y + 0.5f)) continue;
            float *p = &canvas[(y * W + x) * 3];
            for (int k = 0; k < 3; k++) p[k] = t.a * c[k] + (1 - t.a) * p[k];  // alpha over
        }
}

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
    printf("target %s  %dx%d\n", path, W, H);

    // start at the target's mean color
    double mean[3] = {0, 0, 0};
    for (int i = 0; i < W * H; i++)
        for (int k = 0; k < 3; k++) mean[k] += target[i * 3 + k];
    for (int k = 0; k < 3; k++) mean[k] /= W * H;
    vector<float> canvas(W * H * 3);
    for (int i = 0; i < W * H; i++)
        for (int k = 0; k < 3; k++) canvas[i * 3 + k] = mean[k];

    double e0 = loss(canvas, target), e = e0;
    printf("error after mean fill: %.1f\n", e0);

    FILE *log = fopen(triPath, "w");
    srand(12345);  // fixed seed so runs compare

    for (int n = 0; n < N; n++) {
        // keep the best of `tries` random candidates
        Tri best;
        double bestScore = 0;  // 0 = no better than nothing
        for (int k = 0; k < tries; k++) {
            Tri t = randomTri(W, H);
            double s = fit(t, canvas, target, W, H);
            if (s < bestScore) { bestScore = s; best = t; }
        }
        if (bestScore == 0) { printf("no improving triangle found at %d, stopping\n", n); break; }

        draw(best, canvas, W, H);
        e += bestScore;
        fprintf(log, "%.2f %.2f %.2f %.2f %.2f %.2f %.4f %.4f %.4f %.3f\n",
                best.x1, best.y1, best.x2, best.y2, best.x3, best.y3, best.r, best.g, best.b, best.a);
        if (n % 20 == 19) printf("%4d triangles  error %.1f  (%.1f%% of start)\n", n + 1, e, 100 * e / e0);
    }
    fclose(log);

    writePPM(imgPath, canvas, W, H);
    // recompute should match; catches drift in fit()
    printf("final error %.1f (recomputed %.1f)\n", e, loss(canvas, target));
    printf("wrote %s and %s\n", imgPath, triPath);
}
