CXX ?= c++
CXXFLAGS ?= -O2 -Wall -pthread

# Backends swap at link time: same driver, same work, different kernel.
#   prefix  row-wise prefix sums, O(height) per candidate
#   direct  straight per-pixel accumulation, O(area)
KERNEL ?= kernel_prefix.cpp

DEPS = geo.h kernel.h approx.h pool.h

all: geo bench

geo: main.cpp $(KERNEL) $(DEPS)
	$(CXX) $(CXXFLAGS) -o $@ main.cpp $(KERNEL)

bench: bench.cpp $(KERNEL) $(DEPS)
	$(CXX) $(CXXFLAGS) -o $@ bench.cpp $(KERNEL)

# compare the two CPU kernels head to head
bench-direct: bench.cpp kernel_direct.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -o $@ bench.cpp kernel_direct.cpp

compare: bench bench-direct
	./bench-direct out/sample-target.ppm 200 2000 5 1
	./bench        out/sample-target.ppm 200 2000 5 1

# later, on the PC:
#   make bench KERNEL=kernel_avx2.cpp CXXFLAGS="-O2 -Wall -pthread -mavx2"
#   make bench KERNEL=kernel_hip.cpp CXX=hipcc

clean:
	rm -f geo bench bench-direct

.PHONY: all clean compare
