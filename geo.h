#pragma once

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace std;

struct Tri { float x1, y1, x2, y2, x3, y3, r, g, b, a; };

// signed area x2; sign says which side of a->b p is on
inline float edge(float ax, float ay, float bx, float by, float px, float py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }

inline int lo3(float a, float b, float c) { float m = a; if (b < m) m = b; if (c < m) m = c; return (int)m; }
inline int hi3(float a, float b, float c) { float m = a; if (b > m) m = b; if (c > m) m = c; return (int)m + 1; }

// bounding box, clipped to canvas
inline void box(const Tri &t, int W, int H, int &bx0, int &by0, int &bx1, int &by1) {
    bx0 = lo3(t.x1, t.x2, t.x3); bx1 = hi3(t.x1, t.x2, t.x3);
    by0 = lo3(t.y1, t.y2, t.y3); by1 = hi3(t.y1, t.y2, t.y3);
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 > W) bx1 = W;
    if (by1 > H) by1 = H;
}

inline float frand() { return rand() / (float)RAND_MAX; }

// random point, vertices scattered up to s away
inline Tri randomTri(int W, int H) {
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

// next int, skipping whitespace and # comments
inline int readInt(FILE *f) {
    int c = fgetc(f);
    while (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '#') {
        if (c == '#') while (c != '\n' && c != EOF) c = fgetc(f);
        c = fgetc(f);
    }
    int v = 0;
    while (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); c = fgetc(f); }
    return v;
}

inline vector<float> readPPM(const char *path, int &W, int &H) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); exit(1); }
    if (fgetc(f) != 'P' || fgetc(f) != '6') { printf("%s is not a binary PPM (P6)\n", path); exit(1); }
    W = readInt(f);
    H = readInt(f);
    readInt(f);  // maxval, assume 255
    vector<float> img(W * H * 3);
    int WH = W * H;
    for (int i = 0; i < WH; i++)
        for (int c = 0; c < 3; c++) img[c * WH + i] = fgetc(f) / 255.0f;  // planes, not interleaved
    fclose(f);
    return img;
}

inline void writePPM(const char *path, const vector<float> &img, int W, int H) {
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6 %d %d 255\n", W, H);
    int WH = W * H;
    for (int i = 0; i < WH; i++)
        for (int k = 0; k < 3; k++) {
            int c = (int)(img[k * WH + i] * 255 + 0.5f);  // blending can drift outside [0,1]
            if (c < 0) c = 0;
            if (c > 255) c = 255;
            fputc(c, f);
        }
    fclose(f);
}

// squared error over every channel
inline double loss(const vector<float> &a, const vector<float> &b) {
    double s = 0;
    for (size_t i = 0; i < a.size(); i++) { double d = a[i] - b[i]; s += d * d; }
    return s;
}

// canvas filled with the target's mean color
inline vector<float> meanCanvas(const vector<float> &target, int W, int H) {
    int WH = W * H;
    double mean[3] = {0, 0, 0};
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < WH; i++) mean[k] += target[k * WH + i];
        mean[k] /= WH;
    }
    vector<float> canvas(WH * 3);
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < WH; i++) canvas[k * WH + i] = mean[k];
    return canvas;
}
