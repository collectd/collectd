#!/bin/bash

# Change into distbench/ directory if not already.
cd "$(dirname "$(readlink -f "$0")")"

# (Re)build, just in case.
make

# Run all three benchmarks.
for program in sshmidt margalit bkjg; 
do
    rm "$program".csv	
    echo "Number of buckets, Update linear, Percentile linear, Mixed linear, Update exponential, Percentile exponential, Mixed exponential, Update custom, Percentile custom, Mixed custom, Update all, Percentile all, Mixed all\n"          >> "$program".csv	  
    for i in {50..5000..50}
    do
        echo "Running $program for $i"
        ./"$program" $i >> "$program".csv
    done
done
