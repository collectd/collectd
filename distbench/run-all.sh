#!/bin/sh

# Change into distbench/ directory if not already.
cd "$(dirname "$(readlink -f "$0")")"

# (Re)build, just in case.
make

# Run all three benchmarks.
for program in bkjg margalit sshmidt; do
    echo "Running $program"
    ./"$program"
    echo
done
