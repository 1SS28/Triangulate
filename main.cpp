#include <cstdio>
#include <vector>

using namespace std;

struct Tri { float x1, y1, x2, y2, x3, y3, r, g, b, a; };

// Twice the signed area of (a, b, p); sign tells which side of edge a->b p lies on
float edge(float ax, float ay, float bx, float by, float px, float py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }

int main() {
    const int W = 256, H = 256;
    vector<float> img(W * H * 3, 1.0f);
    Tri t{40, 30, 220, 90, 100, 230, 0.9f, 0.2f, 0.3f, 0.8f};

    float area = edge(t.x1, t.y1, t.x2, t.y2, t.x3, t.y3);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float px = x + 0.5f, py = y + 0.5f;  // sample pixel center
            float w0 = edge(t.x2, t.y2, t.x3, t.y3, px, py) / area;
            float w1 = edge(t.x3, t.y3, t.x1, t.y1, px, py) / area;
            if (w0 < 0 || w1 < 0 || 1 - w0 - w1 < 0) continue;  // outside: some barycentric weight negative
            float *p = &img[(y * W + x) * 3], c[3] = {t.r, t.g, t.b};
            for (int k = 0; k < 3; k++) p[k] = t.a * c[k] + (1 - t.a) * p[k];  // alpha "over" blend
        }

    FILE *f = fopen("out.ppm", "wb");
    fprintf(f, "P6 %d %d 255\n", W, H);
    for (float v : img) fputc((int)(v * 255), f);
    fclose(f);
}
