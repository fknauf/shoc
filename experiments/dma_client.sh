#!/bin/bash

OUTDIR=results/dma

mkdir -p "${OUTDIR}"

read -p $'Start dma_server on the DPU and press Return\n' -n 1 -s

for i in $(seq 1 1000); do
    echo $i
    SKIP_VERIFY=1 bench/dma_client > "${OUTDIR}/seq_${i}.json"
    SKIP_VERIFY=1 bench/dma_client 4 > "${OUTDIR}/par_${i}.json"
done

read -p $'Start doca_dma_server on the DPU and press Return\n' -n 1 -s

for i in $(seq 1 1000); do
    echo $i
    SKIP_VERIFY=1 bench/doca_dma_client > "${OUTDIR}/plain_seq_${i}.json"
    SKIP_VERIFY=1 bench/doca_dma_client 4 > "${OUTDIR}/plain_par_${i}.json"
done
