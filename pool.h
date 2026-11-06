#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

using namespace std;

// Workers are spawned once and parked between rounds. The greedy loop calls run()
// a few hundred times, and spawning threads per call would cost more than the
// scoring it parallelizes.
//
// n counts the caller: it takes work rather than blocking. Scheduling is dynamic
// -- threads pull small batches off an atomic cursor -- because candidate
// triangles vary ~70x in area, and static chunks left threads idle waiting on
// whoever drew the big ones.
class Pool {
public:
    explicit Pool(int n) : nt(n), round(0), left(0), next(0) {
        for (int i = 0; i < n - 1; i++) w.emplace_back([this, i] { loop(i); });
    }

    ~Pool() {
        { lock_guard<mutex> g(m); stop = true; }
        cv.notify_all();
        for (size_t i = 0; i < w.size(); i++) w[i].join();
    }

    // Run fn over [0, total) in GRAIN-sized batches, handed out on demand.
    void run(int total, const function<void(int, int)> &fn) {
        {
            lock_guard<mutex> g(m);
            job = &fn; n_total = total;
            next.store(0, memory_order_relaxed);
            left.store(nt - 1, memory_order_relaxed);
            round.store(round.load(memory_order_relaxed) + 1, memory_order_release);
        }
        cv.notify_all();

        drain();

        for (int i = 0; i < 4000; i++) {
            if (left.load(memory_order_acquire) == 0) return;
        }
        unique_lock<mutex> lk(m);
        fin.wait(lk, [this] { return left.load(memory_order_acquire) == 0; });
    }

    int size() const { return nt; }

private:
    void loop(int id) {
        int seen = 0;
        for (;;) {
            // fast path: the next round usually lands within microseconds
            int r = round.load(memory_order_acquire);
            for (int i = 0; i < 200000 && r == seen && !stop; i++) {
                r = round.load(memory_order_acquire);
            }
            if (r == seen) {
                unique_lock<mutex> lk(m);
                cv.wait(lk, [&] { return stop || round.load(memory_order_acquire) != seen; });
            }
            if (stop) return;
            seen = round.load(memory_order_acquire);
            int total = n_total;
            const function<void(int, int)> *f = job;

            (void)total; (void)f;
            drain();

            if (left.fetch_sub(1, memory_order_acq_rel) == 1) {
                lock_guard<mutex> g(m);
                fin.notify_one();
            }
        }
    }

    // batches are small enough to even out the spread in triangle area, large
    // enough that the atomic is not the bottleneck
    static const int GRAIN = 8;

    void drain() {
        for (;;) {
            int lo = next.fetch_add(GRAIN, memory_order_acq_rel);
            if (lo >= n_total) return;
            int hi = lo + GRAIN;
            if (hi > n_total) hi = n_total;
            (*job)(lo, hi);
        }
    }

    int nt, n_total = 0;
    atomic<int> round;
    atomic<int> left;
    atomic<int> next;
    bool stop = false;
    const function<void(int, int)> *job = nullptr;
    mutex m;
    condition_variable cv, fin;
    vector<thread> w;
};
