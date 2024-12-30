#!/bin/sh

# compression experiments can be run locally.
# run this from the source tree base directory, i.e. as experiments/compression.sh

mkdir -p results/compression

FILE=results/compression/testdata.in

if ! [ -e "$FILE" ]; then
    bench/generate_testdata "$FILE" 256 1048576
fi

for i in $(seq 1 100); do
    echo $i
    bench/simple_compress        "$FILE" > results/compression/seq_${i}.json
    bench/parallel_compress      "$FILE" > results/compression/par_${i}.json
    bench/doca_simple_compress   "$FILE" > results/compression/plain_seq_${i}.json
    bench/doca_parallel_compress "$FILE" > results/compression/plain_par_${i}.json
done
