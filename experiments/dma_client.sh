#!/bin/bash

read -p $'Start dma_server on the DPU and press Return\n' -n 1 -s

mkdir -p results/dma

for i in $(seq 1 100); do
    echo $i
    SKIP_VERIFY=1 bench/dma_client > results/dma/seq_${i}.json
    SKIP_VERIFY=1 bench/dma_client 4 > results/dma/par_${i}.json
done

read -p $'Start doca_dma_server on the DPU and press Return\n' -n 1 -s

for i in $(seq 1 100); do
    echo $i
    echo seq
    SKIP_VERIFY=1 bench/doca_dma_client > results/dma/plain_seq_${i}.json
    echo par
    SKIP_VERIFY=1 bench/doca_dma_client 4 > results/dma/plain_par_${i}.json
done
