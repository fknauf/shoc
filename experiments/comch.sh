#!/bin/bash

mkdir -p results/comch

read -p $'Start comch_data_server on the DPU and press Return\n' -n 1 -s

for i in $(seq 1 1000); do
    echo $i
    bench/comch_data_client > results/comch/seq_${i}.json
    # to fool postprocessing script
    ln -sf seq_${i}.json results/comch/par_${i}.json
done

read -p $'Start doca_comch_data_server on the DPU and press Return\n' -n 1 -s

for i in $(seq 1 1000); do
    echo $i
    bench/doca_comch_data_client > results/comch/plain_seq_${i}.json
    ln -sf plain_seq_${i}.json results/comch/plain_par_${i}.json
done
