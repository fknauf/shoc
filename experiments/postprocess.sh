#!/bin/sh

EXPERIMENT="$1"

echo 'seq,doca_seq,par,doca_par' > "results/$1/datarates.csv"

for i in $(seq 100); do
    jq .data_rate_gibps \
        "results/$EXPERIMENT/seq_${i}.json" \
        "results/$EXPERIMENT/plain_seq_${i}.json" \
        "results/$EXPERIMENT/par_${i}.json" \
        "results/$EXPERIMENT/plain_par_${i}.json" \
    | tr '\n' , \
    | sed 's/,$/\n/' \
    >> "results/$EXPERIMENT/datarates.csv"
done
