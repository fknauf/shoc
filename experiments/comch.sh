#!/bin/bash

read -p $'Start comch_data_server on the DPU and press Return\n' -n 1 -s

mkdir -p results/comch

for i in $(seq 1 100); do
    echo $i
    bench/comch_data_client > results/comch/seq_${i}.json
done

read -p $'Start doca_comch_data_server on the DPU and press Return\n' -n 1 -s

for i in $(seq 1 100); do
    echo $i
    bench/doca_comch_data_client > results/comch/plain_seq_${i}.json
done
