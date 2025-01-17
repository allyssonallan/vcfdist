import vcf
import numpy as np
import matplotlib.pyplot as plt

# chr20, with phab
truvari_prefix = "./truvari/giab-tr/result_"
vcfdist_prefix = "./vcfdist/giab-tr."
do_print = False

SIZE             = 0
SZ_SNP           = 0
SZ_INDEL_1_50    = 1
SZ_INDEL_50_500  = 2
SZ_INDEL_500PLUS = 3
SZ_DIMS = 4
sizes = ["SNP", "INDEL 1-50", "INDEL 50-500", "INDEL 500+"]

VD_NONE = 0
VD_FP   = 1
VD_PP   = 2
VD_TP   = 3
VD_DIMS = 4
vd_cats = ["None", "FP", "PP", "TP"]

TV_NONE = 0
TV_FP   = 1
TV_PP   = 2
TV_TP   = 3
TV_DIMS = 4
tv_cats = ["None", "FP", "PP", "TP"]

tv_min_seq_pct = 0.7
tv_min_size_pct = 0.7
tv_min_ovlp_pct = 0.0

counts = np.zeros((SZ_DIMS, VD_DIMS, TV_DIMS))

def get_size(ref : str, alt : str):
    if len(ref) == 1 and len(alt) == 1:
        return SZ_SNP
    else:
        size_diff = abs(len(ref) - len(alt))
        if size_diff == 0:
            print("ERROR: size 0 INDEL")
            exit(1)
        elif size_diff <= 50:
            return SZ_INDEL_1_50
        elif size_diff <= 500:
            return SZ_INDEL_50_500
        else:
            return SZ_INDEL_500PLUS

def get_vd_type(credit : float):
    if credit >= 0 and credit < 0.7:
        return VD_FP
    elif credit >= 0.7 and credit < 1:
        return VD_PP
    elif credit == 1:
        return VD_TP
    else:
        print("ERROR: credit out of range")
        exit(1)


def get_tv_type(tv_type : str, seqsim : float):
    if seqsim is None:
        return TV_FP
    if tv_type == "TP" and  seqsim < 1 and seqsim >= 0.7:
        return TV_PP
    if tv_type == "TP":
        return TV_TP
    if tv_type == "FP":
        return TV_FP


# for callset in ["query", "truth"]:
for callset in ["query"]:

    # parse vcfdist and truvari summary VCFs
    vcfdist_vcf = vcf.Reader(open(f"{vcfdist_prefix}summary.vcf", "r"))
    truvari_vcf = vcf.Reader(open(f"{truvari_prefix}{callset}.vcf", "r"))
    name = callset.upper()
    print(name)
    tv_used, vd_used, vd_2used = True, True, False
    this_ctg = "chr1"
    print(this_ctg)

    while True:

        # get next valid records for each
        if tv_used: tv_rec = next(truvari_vcf, None)
        if vd_used: vd_rec = next(vcfdist_vcf, None)
        if vd_2used: vd_rec = next(vcfdist_vcf, None)
        while vd_rec != None and vd_rec.genotype(name)['BD'] == None: # skip other callset
            vd_rec = next(vcfdist_vcf, None)
        while tv_rec != None and tv_rec.ALT[0] == "*": # skip nested var
            tv_rec = next(truvari_vcf, None)
        tv_used, vd_used, vd_2used = False, False, False

        if do_print:
            print("============================================================")
            print("Truvari:", tv_rec)
            print("vcfdist:", vd_rec)

        # we've finished iterating through both VCFs
        if tv_rec == None and vd_rec == None: break

        # we've finished this contig for both VCFs
        if (tv_rec == None or tv_rec.CHROM != this_ctg) and \
                (vd_rec == None or vd_rec.CHROM != this_ctg):
            if tv_rec == None:
                this_ctg = vd_rec.CHROM
                print(this_ctg)
            elif vd_rec == None:
                this_ctg = tv_rec.CHROM
                print(this_ctg)
            elif tv_rec.CHROM == vd_rec.CHROM:
                this_ctg = tv_rec.CHROM
                print(this_ctg)
            else:
                print("ERROR: different contigs up next")

        # if we've finished only one VCF, set high position
        tv_pos = 2_000_000_000 if tv_rec == None else (
                1_000_000_000 if tv_rec.CHROM != this_ctg else tv_rec.POS)
        vd_pos = 2_000_000_000 if vd_rec == None else (
                1_000_000_000 if vd_rec.CHROM != this_ctg else vd_rec.POS)

        if tv_pos == vd_pos: # position match
            if do_print: print(f"TV VD {name} {tv_rec.genotype(name)['GT']} {vd_rec.genotype(name)['GT']} {tv_rec.CHROM}:{tv_rec.POS}\t{tv_rec.REF}\t{tv_rec.ALT[0]}")
            if tv_rec.REF == vd_rec.REF and tv_rec.ALT[0] == vd_rec.ALT[0]: # full match
                tv_used, vd_used = True, True
                if tv_rec.genotype(name)['GT'] == '1|1' or tv_rec.genotype(name)['GT'] == '1/1':
                    vd_2used = True # skip second split of this GT
                size = get_size(tv_rec.REF, tv_rec.ALT[0])
                vd_type = get_vd_type(float(vd_rec.genotype(name)['BC']))
                tv_type = get_tv_type(tv_rec.genotype(name)['BD'], tv_rec.INFO['PctSeqSimilarity'])
                counts[size][vd_type][tv_type] += 2 if vd_2used else 1
                if vd_type == VD_TP and tv_type == TV_FP:
                    if do_print: print("vcfdist TP, Truvari FP")
                if vd_type == VD_FP and tv_type == TV_TP:
                    if do_print: print("vcfdist FP, Truvari TP")

                # skip if info unavailable
                if tv_rec.INFO['PctSeqSimilarity'] == None or \
                        tv_rec.INFO['PctSizeSimilarity'] == None or \
                        tv_rec.INFO['PctRecOverlap'] == None:
                    counts[size][vd_type][TV_FP] += 1
                    continue

            # vcfdist SNP adjacent to INS
            elif len(vd_rec.REF) == 1 and len(vd_rec.ALT[0]) == 1:
                vd_used = True
                size = get_size(vd_rec.REF, vd_rec.ALT[0])
                vd_type = get_vd_type(float(vd_rec.genotype(name)['BC']))
                tv_type = TV_NONE
                counts[size][vd_type][tv_type] += 1

            else: # position but not allele match, likely different phasing order

                # get next record for each
                tv_rec_next = next(truvari_vcf, None)
                vd_rec_next = next(vcfdist_vcf, None)
                while vd_rec_next != None and vd_rec_next.genotype(name)['BD'] == None: # skip other callset
                    vd_rec_next = next(vcfdist_vcf, None)
                while tv_rec_next != None and tv_rec_next.ALT[0] == "*": # skip nested var
                    tv_rec_next = next(truvari_vcf, None)

                # test if swapping causes matches
                if tv_rec.REF == vd_rec_next.REF and \
                        tv_rec.ALT[0] == vd_rec_next.ALT[0] and \
                        tv_rec_next.REF == vd_rec.REF and \
                        tv_rec_next.ALT[0] == vd_rec.ALT[0]:
                    # 1: tv_rec, vd_rec_next
                    size1 = get_size(tv_rec.REF, tv_rec.ALT[0])
                    vd_type1 = get_vd_type(float(vd_rec_next.genotype(name)['BC']))
                    tv_type1 = get_tv_type(tv_rec_next.genotype(name)['BD'], tv_rec.INFO['PctSeqSimilarity'])
                    counts[size1][vd_type1][tv_type1] += 1
                    # 2: tv_rec_next, vd_rec
                    size2 = get_size(tv_rec_next.REF, tv_rec_next.ALT[0])
                    vd_type2 = get_vd_type(float(vd_rec.genotype(name)['BC']))
                    tv_type2 = get_tv_type(tv_rec_next.genotype(name)['BD'], tv_rec.INFO['PctSeqSimilarity'])
                    counts[size2][vd_type2][tv_type2] += 1
                    tv_used, vd_used = True, True
                else:
                    print("ERROR: failed to match")

                    # discard current two, pretend we haven't looked at next two
                    counts[size][VD_NONE][TV_NONE] += 2
                    tv_rec = tv_rec_next
                    vd_rec = vd_rec_next


        elif vd_pos < tv_pos: # vcfdist only
            vd_used = True
            size = get_size(vd_rec.REF, vd_rec.ALT[0])
            if do_print: 
                print(f"   VD {name} {vd_rec.genotype(name)['GT']} {vd_rec.CHROM}:{vd_rec.POS}\t{vd_rec.REF}\t{vd_rec.ALT[0]}\t")
            if size != SZ_SNP:
                print(f"   VD {name} {vd_rec.genotype(name)['GT']} {vd_rec.CHROM}:{vd_rec.POS}\t{vd_rec.REF}\t{vd_rec.ALT[0]}\t")
                print("WARN: vcfdist only, not SNP")
            vd_type = get_vd_type(float(vd_rec.genotype(name)['BC']))
            tv_type = TV_NONE
            counts[size][vd_type][tv_type] += 1

        elif tv_pos < vd_pos: # truvari only
            print(f"TV    {name} {tv_rec.genotype(name)['GT']} {tv_rec.CHROM}:{tv_rec.POS}\t{tv_rec.REF}\t{tv_rec.ALT[0]}")
            print("WARN: Truvari only")
            tv_used = True

            # skip unphased truvari variants
            gt = tv_rec.genotype(name)['GT']
            if gt[1] == '/' and gt[0] != gt[2]:
                continue

            size = get_size(tv_rec.REF, tv_rec.ALT[0])
            vd_type = VD_NONE
            tv_type = get_tv_type(tv_rec.genotype(name)['BD'], tv_rec.INFO['PctSeqSimilarity'])
            counts[size][vd_type][tv_type] += 1

            # skip if info unavailable
            if tv_rec.INFO['PctSeqSimilarity'] == None or \
                    tv_rec.INFO['PctSizeSimilarity'] == None or \
                    tv_rec.INFO['PctRecOverlap'] == None:
                counts[size][vd_type][TV_FP] += 1
                continue
 
    for size_idx in range(SZ_DIMS):
        fig, ax = plt.subplots()
        ax.matshow(np.log(counts[size_idx,1:,1:] + 0.1), cmap="Blues")
        plt.title(f"{name} {sizes[size_idx]} Confusion Matrix")
        plt.ylabel("vcfdist")
        ax.set_yticks(list(range(VD_DIMS-1)))
        ax.set_yticklabels(vd_cats[1:])
        plt.xlabel("Truvari")
        ax.set_xticks(list(range(TV_DIMS-1)))
        ax.set_xticklabels(tv_cats[1:])
        for (i,j), z in np.ndenumerate(counts[size_idx,1:,1:]):
            ax.text(j, i, f"{int(z)}", ha='center', va='center',
                bbox=dict(boxstyle='round', facecolor='white', edgecolor='0.3'))
        plt.savefig(f"img/tv_vd_{callset}_{size_idx}_cm.png", dpi=200)
