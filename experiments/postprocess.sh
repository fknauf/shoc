#!/bin/bash

EXPERIMENT="$1"
DIR="results/${EXPERIMENT}"
DATARATES="${DIR}/datarates.tsv"

echo $'seq\tdoca_seq\tpar\tdoca_par' > "${DATARATES}"

for i in $(seq 1000); do
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
    function average(values) {
        sum = 0
        count = 0

        for(k in values) {
            sum += values[k]
            ++count
        }

        return sum / count
    }

    function standard_deviation(values, average) {
        errsqsum = 0
        count = 0

        for(k in values) {
            errsqsum += (values[k] - average) ** 2
            ++count
        }

        return sqrt(errsqsum / (count - 1))
    }

    function calculate_perfloss_percentages(values_docoro, values_plain, dest) {
        for(k in values_docoro) {
            dest[k] = (1 - values_docoro[k] / values_plain[k]) * 100
        }
    }

    function outperformed_percentage(values_docoro, values_plain) {
        sum = 0
        count = 0

        for(k in values_docoro) {
            ++count
            if(values_docoro[k] > values_plain[k]) {
                ++sum
            }
        }

        return sum / count * 100
    }

    function format_99ci(mean, sem) {
        lower = mean - 2.57583 * sem
        upper = mean + 2.57583 * sem

        return sprintf("%.3f-%.3f", lower, upper)
    }

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
        calculate_perfloss_percentages(values[1], values[2], seq_loss)
        calculate_perfloss_percentages(values[3], values[4], par_loss)
        seq_loss_avg = average(seq_loss)
        par_loss_avg = average(par_loss)
        seq_loss_stddev = standard_deviation(seq_loss, seq_loss_avg)
        par_loss_stddev = standard_deviation(par_loss, par_loss_avg)
        seq_loss_sem = seq_loss_stddev / sqrt(count)
        par_loss_sem = par_loss_stddev / sqrt(count)
        seq_outperformed = outperformed_percentage(values[1], values[2])
        par_outperformed = outperformed_percentage(values[3], values[4])

        for(i in keys) {
            avg[i] = average(values[i])
            stddev[i] = standard_deviation(values[i], avg[i])
            sem[i] = stddev[i] / sqrt(count)

            asort(values[i])
            if(count % 2 == 0) {
                median[i] = (values[i][mid] + values[i][mid + 1]) / 2
            } else {
                median[i] = values[i][mid]
            }
        }

        label["dma"] = "DMA"
        label["comch"] = "Comch"
        label["compression"] = "Deflate"

        NF = 0

        field = 0
        $++field = "    " label[experiment] " seq."
        
        $++field = sprintf("%.3f", avg[1])
        if(avg[1] > avg[2]) {
            $field = "\\textbf{" $field "}"
        }
        $++field = sprintf("%.3f", stddev[1])
        #$++field = sprintf("%.3e", sem[1])
        $++field = format_99ci(avg[1], sem[1])

        $++field = sprintf("%.3f", avg[2])
        if(avg[1] <= avg[2]) {
            $field = "\\textbf{" $field "}"
        }
        $++field = sprintf("%.3f", stddev[2])
        #$++field = sprintf("%.3e", sem[2])
        $++field = format_99ci(avg[2], sem[2])
        
        $++field = sprintf("%.3f", seq_loss_avg)
        $++field = sprintf("%.3f", seq_loss_stddev)
        #$++field = sprintf("%.3e", seq_loss_sem)
        $++field = format_99ci(seq_loss_avg, seq_loss_sem)
        $++field = sprintf("%.1f\\%%", seq_outperformed)

        print

        if(experiment == "comch") {
            exit
        }

        field = 0
        $++field = "    " label[experiment] " par."
        $++field = sprintf("%.3f", avg[3])
        if(avg[3] > avg[4]) {
            $field = "\\textbf{" $field "}"
        }
        $++field = sprintf("%.3f", stddev[3])
        #$++field = sprintf("%.3e", sem[3])
        $++field = format_99ci(avg[3], sem[3])

        $++field = sprintf("%.3f", avg[4])
        if(avg[3] <= avg[4]) {
            $field = "\\textbf{" $field "}"
        }
        $++field = sprintf("%.3f", stddev[4])
        #$++field = sprintf("%.3e", sem[4])
        $++field = format_99ci(avg[4], sem[4])

        $++field = sprintf("%.3f", par_loss_avg)
        $++field = sprintf("%.3f", par_loss_stddev)
        #$++field = sprintf("%.3e", par_loss_sem)
        $++field = format_99ci(par_loss_avg, par_loss_sem)
        $++field = sprintf("%.1f\\%%", par_outperformed)

        print
    }
' "${DATARATES}" | sed 's/e-0/e-/g'
