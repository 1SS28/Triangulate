#!/bin/bash
# fails unless both kernels write the same image and triangles at every thread count
#   tools/check.sh [triangles] [candidates]
# make check runs this, and ci runs make check on every push

set -euo pipefail
cd "$(dirname "$0")/.."

CXX=${CXX:-c++}
CXXFLAGS=${CXXFLAGS:--O2 -Wall -pthread}
N=${1:-200}
TRIES=${2:-2000}
OUT=out/check

mkdir -p $OUT
python3 tools/toppm.py images/sample.png $OUT/target.ppm 0 > /dev/null  # 0 skips sips, so linux works

for kernel in prefix direct; do
    # own binaries: a ./geo left over from another KERNEL can't stand in
    bin=$OUT/geo-$kernel
    $CXX $CXXFLAGS -o $bin main.cpp kernel_$kernel.cpp

    for t in 1 2 3 8; do
        $bin $OUT/target.ppm $N $TRIES $OUT/$kernel-$t $t > $OUT/$kernel-$t.log
        if ! cmp -s $OUT/$kernel-1.ppm $OUT/$kernel-$t.ppm ||
           ! cmp -s $OUT/$kernel-1_tris.txt $OUT/$kernel-${t}_tris.txt; then
            echo "$kernel: $t threads changed the output"
            exit 1
        fi
    done

    # main.cpp prints the running error and a full recompute at %.1f, hence the slack
    line=$(grep '^final error' $OUT/$kernel-1.log)
    echo "$line" | tr -d '()' | awk -v k=$kernel '{ d = $3 - $5; if (d < 0) d = -d
        if (d > 0.1 + 1e-3 * $5) { print k ": running error drifted from the recompute"; exit 1 } }'

    echo "$kernel: same output at 1, 2, 3 and 8 threads; $line"
done
