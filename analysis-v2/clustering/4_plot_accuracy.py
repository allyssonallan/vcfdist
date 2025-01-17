import os
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
# plt.rcParams.update({"figure.facecolor": (0,0,0,0)})

fig, ax = plt.subplots(1, 2, figsize=(10,6))

partial_credit = True

types = ["gap", "gap", "gap", "gap", "gap", "wfa_len"]
vals = [10, 20, 50, 100, 200, 1000]

for t, v in zip(types, vals):
    print(f"plotting {t} {v}")
    with open(f"./{t}s/{v}.precision-recall.tsv") as csv:
        indel_recall = []
        indel_prec = []
        snp_recall = []
        snp_prec = []
        next(csv) # skip header

        for line in csv:
            typ, qual, prec, recall, f1, f1q, truth_tot, truth_tp, truth_pp, truth_fn, \
                query_tot, query_tp, query_pp, query_fp = line.split('\t')
            if partial_credit: # normal vcfdist prec/recall calc, already done
                if typ == "INDEL":
                    indel_recall.append(float(recall))
                    indel_prec.append(float(prec))
                elif typ == "SNP":
                    snp_recall.append(float(recall))
                    snp_prec.append(float(prec))
            else: # recalculate using TP/FP/FN
                if typ == "INDEL":
                    indel_recall.append(float(truth_tp) / float(truth_tot))
                    if int(truth_tp) + int(query_fp) == 0:
                        indel_prec.append(1)
                    else:
                        indel_prec.append(float(truth_tp) / 
                                (float(truth_tp) + float(query_fp)))
                elif typ == "SNP":
                    snp_recall.append(float(truth_tp) / float(truth_tot))
                    if int(truth_tp) + int(query_fp) == 0:
                        snp_prec.append(1)
                    else:
                        snp_prec.append(float(truth_tp) / 
                                (float(truth_tp) + float(query_fp)))
        ax[0].plot(snp_recall, snp_prec, linestyle='', marker='.')
        ax[1].plot(indel_recall, indel_prec, linestyle='', marker='.')
         
# SNP plot
ax[0].set_title("SNPs")
ax[0].set_xlabel("Recall", fontsize=15)
ax[0].set_ylabel("Precision", fontsize=15)
ax[0].set_xlim((0.9,1))
ax[0].set_ylim((0.9,1))

# INDEL plot
ax[1].set_title("INDELs")
ax[1].set_xlabel("Recall", fontsize=15)
ax[1].set_ylabel("Precision", fontsize=15)
ax[1].set_xlim((0.9,1))
ax[1].set_ylim((0.9,1))

ax[0].legend(["Gap 10", "Gap 20", "Gap 50", "Gap 100", "Gap 200", "WFA"])
plt.tight_layout()
plt.savefig('img/accuracy.png', dpi=200)
