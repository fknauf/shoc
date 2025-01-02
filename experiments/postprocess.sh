#!/bin/bash

EXPERIMENT="$1"
DIR="results/${EXPERIMENT}"
DATARATES="${DIR}/datarates.tsv"
STATS="${DIR}/stats.tsv"

echo $'seq\tdoca_seq\tpar\tdoca_par' > "${DATARATES}"

for i in $(seq 100); do
    jq .data_rate_gibps \
        "${DIR}/seq_${i}.json" \
        "${DIR}/plain_seq_${i}.json" \
        "${DIR}/par_${i}.json" \
        "${DIR}/plain_par_${i}.json" \
    | tr '\n' '\t' \
    | sed 's/\t$/\n/' \
    >> "${DATARATES}"
done

gawk '
    BEGIN {
        OFS = "\t"
        ORS = "\n"
    }
    NR == 1 {
        header = $0
        for(i = 1; i <= NF; ++i) {
            keys[i] = $i
        }
        num_fields = NF
        $1 = $1
        print
    }
    NR > 1 {
        for(i = 1; i <= NF; ++i) {
            values[i][NR - 1] = $i
        }
        count = NR - 1
        mid = (count / 2)
    }
    END {
        for(i in keys) {
            asort(values[i])

            sum = 0
            for(k in values[i]) {
                sum += values[i][k]
            }

            avgs[i] = sum / count

            if(count % 2 == 0) {
                medians[i] = (values[i][mid] + values[i][mid + 1]) / 2
            } else {
                medians[i] = values[i][mid]
            }

            errsqsum = 0
            for(k in values[i]) {
                errsqsum += (values[i][k] - avgs[i]) ^ 2
            }

            stddevs[i] = sqrt(errsqsum / (count - 1))
        }

        for(i = 1; i <= num_fields; ++i) {
            $i = avgs[i]
        }
        print

        for(i = 1; i <= num_fields; ++i) {
            $i = medians[i]
        }
        print

        for(i = 1; i <= num_fields; ++i) {
            $i = stddevs[i]
        }
        print
    }
' "${DATARATES}" > "${STATS}"
