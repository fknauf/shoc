#!/bin/sh

echo 'seq,doca_seq,par,doca_par' > results/compression/compression_datarates.csv

for i in $(seq 100); do
    jq .data_rate_gibps \
        results/compression/seq_${i}.json \
        results/compression/plain_seq_${i}.json \
        results/compression/par_${i}.json \
        results/compression/plain_par_${i}.json \
    | tr '\n' , \
    | sed 's/,$/\n/' \
    >> results/compression/compression_datarates.csv
done
