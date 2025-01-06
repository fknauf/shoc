#!/bin/bash

EXPERIMENT="$1"
DIR="results/${EXPERIMENT}"
DATARATES="${DIR}/datarates.tsv"

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

gawk -v experiment="$EXPERIMENT" '
    BEGIN {
        OFS = " & "
        ORS = " \\\\\n"
    }
    NR == 1 {
        for(i = 1; i <= NF; ++i) {
            keys[i] = $i
        }
        num_fields = NF
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
            sem[i] = stddevs[i] / sqrt(count)
        }

        label["dma"] = "DMA"
        label["comch"] = "Comch"
        label["compression"] = "Deflate"

        $1 = "    " label[experiment] " seq."
        $2 = sprintf("%.3f", avgs[1])
        #$3 = sprintf("%.2e", sem[1])
        $3 = sprintf("%.3f-%.3f", avgs[1] - 2.57583 * sem[1], avgs[1] + 2.57583 * sem[1])
        $4 = sprintf("%.3f", avgs[2])
        #$5 = sprintf("%.2e", sem[2])
        $5 = sprintf("%.3f-%.3f", avgs[2] - 2.57583 * sem[2], avgs[2] + 2.57583 * sem[2])
        $6 = sprintf("%.3f\\%%", (1 - avgs[1] / avgs[2]) * 100)

        if(avgs[1] > avgs[2]) {
            $2 = "\\textbf{" $2 "}"
        } else {
            $4 = "\\textbf{" $4 "}"
        }
        print

        if(experiment == "comch") {
            exit
        }

        $1 = "    " label[experiment] " par."
        $2 = sprintf("%.3f", avgs[3])
        #$3 = sprintf("%.2e", sem[3])
        $3 = sprintf("%.3f-%.3f", avgs[3] - 2.57583 * sem[3], avgs[3] + 2.57583 * sem[3])
        $4 = sprintf("%.3f", avgs[4])
        #$5 = sprintf("%.2e", sem[4])
        $5 = sprintf("%.3f-%.3f", avgs[4] - 2.57583 * sem[4], avgs[4] + 2.57583 * sem[4])
        $6 = sprintf("%.3f\\%%", (1 - avgs[3] / avgs[4]) * 100)

        if(avgs[3] > avgs[4]) {
            $2 = "\\textbf{" $2 "}"
        } else {
            $4 = "\\textbf{" $4 "}"
        }
        print
    }
' "${DATARATES}"
