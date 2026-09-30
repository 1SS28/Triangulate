#!/bin/bash
# Approximate an image from images/ with triangles.
#   ./run.sh cat.png [triangles] [candidates] [size]
# Builds, converts, runs, opens. Output goes to out/.

set -e

IMG=$1
N=${2:-200}
TRIES=${3:-2000}
SIZE=${4:-256}

if [ -z "$IMG" ]; then
    echo "usage: ./run.sh <image> [triangles] [candidates] [size]"
    echo ""
    echo "images available:"
    ls images
    exit 1
fi

# accept a bare name, images/name, or any path
if [ ! -f "$IMG" ]; then IMG="images/$IMG"; fi
if [ ! -f "$IMG" ]; then
    echo "no such image: $1"
    echo "put it in images/ first. currently there:"
    ls images
    exit 1
fi

NAME=$(basename "$IMG")
NAME=${NAME%.*}  # strip extension

mkdir -p out
c++ -O2 -o geo main.cpp
python3 tools/toppm.py "$IMG" "out/$NAME-target.ppm" "$SIZE"
./geo "out/$NAME-target.ppm" "$N" "$TRIES" "out/$NAME"
open "out/$NAME.ppm"
