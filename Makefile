CXX ?= c++
CXXFLAGS ?= -O2 -Wall -pthread

# Backends are swapped at link time: same driver, same work, different kernel.
KERNEL ?= kernel_scalar.cpp

DEPS = geo.h kernel.h approx.h pool.h

all: geo bench

geo: main.cpp $(KERNEL) $(DEPS)
	$(CXX) $(CXXFLAGS) -o $@ main.cpp $(KERNEL)

bench: bench.cpp $(KERNEL) $(DEPS)
	$(CXX) $(CXXFLAGS) -o $@ bench.cpp $(KERNEL)

# later, on the PC:
#   make bench KERNEL=kernel_avx2.cpp CXXFLAGS="-O2 -Wall -mavx2"
#   make bench KERNEL=kernel_hip.cpp CXX=hipcc

clean:
	rm -f geo bench

.PHONY: all clean
