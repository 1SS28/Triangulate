#include <algorithm>
#include <chrono>

#include "approx.h"

using namespace std::chrono;

// Times the approximation loop only -- no file IO, no process startup. Reports the
// median of several trials, plus the final error so a faster kernel that quietly
// changed the answer is obvious.
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "target.ppm";
    int N = argc > 2 ? atoi(argv[2]) : 200;
    int tries = argc > 3 ? atoi(argv[3]) : 2000;
    int trials = argc > 4 ? atoi(argv[4]) : 5;
    int threads = argc > 5 ? atoi(argv[5]) : 1;
    int guided = argc > 6 ? atoi(argv[6]) : 1;
    int steps = argc > 7 ? atoi(argv[7]) : 64;
    int seed = argc > 8 ? atoi(argv[8]) : 12345;  // vary it to check a quality claim is not one lucky stream

    int W, H;
    vector<float> target = readPPM(path, W, H);

    vector<double> ms;
    double err = 0;
    for (int i = 0; i < trials; i++) {
        auto t0 = steady_clock::now();
        Run r = approximate(target, W, H, N, tries, seed, threads, guided, steps, false);
        auto t1 = steady_clock::now();
        ms.push_back(duration<double, milli>(t1 - t0).count());
        err = r.e;
    }
    sort(ms.begin(), ms.end());

    printf("%-8s %5dx%-5d N=%-5d tries=%-6d threads=%-3d %-8s refine=%-4d trials=%-3d  median %8.1f ms  error %.1f  psnr %.2f dB\n",
           KERNEL_NAME, W, H, N, tries, threads, guided ? "residual" : "uniform", steps, trials, ms[trials / 2], err, psnr(err, W, H));
}
