#include <vector>
#include <algorithm>

#include "phase.h"
#include "print.h"
#include "globals.h"

void phaseblockData::write_summary_vcf(std::string out_vcf_fn) {

    // VCF header
    if (g.verbosity >= 1) INFO("  Printing GA4GH-compatible summary VCF to '%s'", out_vcf_fn.data());
    FILE* out_vcf = fopen(out_vcf_fn.data(), "w");
    const std::chrono::time_point now{std::chrono::system_clock::now()};
    time_t tt = std::chrono::system_clock::to_time_t(now);
    tm local_time = *localtime(&tt);
    fprintf(out_vcf, "##fileformat=VCFv4.2\n");
    fprintf(out_vcf, "##fileDate=%04d%02d%02d\n", local_time.tm_year + 1900, 
            local_time.tm_mon + 1, local_time.tm_mday);
    fprintf(out_vcf, "##CL=%s\n", g.cmd.data()+1);
    for (size_t i = 0; i < this->contigs.size(); i++) {
        fprintf(out_vcf, "##contig=<ID=%s,length=%d,ploidy=%d>\n", 
                this->contigs[i].data(), this->lengths[i], this->ploidy[i]);
    }
    fprintf(out_vcf, "##FILTER=<ID=PASS,Description=\"All filters passed\">\n");
    fprintf(out_vcf, "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"GenoType\">\n");
    fprintf(out_vcf, "##FORMAT=<ID=BD,Number=1,Type=String,Description=\"Benchmark Decision for call (TP/FP/FN). PP conversion: TP if BC >= 0.5 else FP/FN (hap.py compatibility)\">\n");
    fprintf(out_vcf, "##FORMAT=<ID=BC,Number=1,Type=String,Description=\"Benchmark Credit (on the interval [0,1], based on sync group edit distance)\">\n");
    fprintf(out_vcf, "##FORMAT=<ID=BK,Number=1,Type=String,Description=\"BenchmarK category (for hap.py compatibility, always genotype match 'gm')\">\n");
    fprintf(out_vcf, "##FORMAT=<ID=QQ,Number=1,Type=Float,Description=\"variant Quality\">\n");
    fprintf(out_vcf, "##FORMAT=<ID=SC,Number=1,Type=Integer,Description=\"SuperCluster (index in contig)\">\n");
    fprintf(out_vcf, "##FORMAT=<ID=SG,Number=1,Type=Integer,Description=\"Sync Group (index in supercluster, for credit assignment)\">\n");
    fprintf(out_vcf, "##FORMAT=<ID=PS,Number=1,Type=Integer,Description=\"Phase Set identifier (input, per-variant)\">\n");
    fprintf(out_vcf, "##FORMAT=<ID=PB,Number=1,Type=Integer,Description=\"Phase Block (output, per-supercluster, index in contig)\">\n");
    fprintf(out_vcf, "##FORMAT=<ID=BS,Number=1,Type=Integer,Description=\"Block State (phaseblock truth-to-query mapping state; 0 = T1Q1:T2Q2, 1 = T1Q2:T2Q1)\">\n");
    fprintf(out_vcf, "##FORMAT=<ID=FE,Number=1,Type=Integer,Description=\"Flip Error (a per-supercluster error)\">\n");
    fprintf(out_vcf, "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tTRUTH\tQUERY\n");

    // write variants
    for (std::string ctg : this->contigs) {
        std::vector< std::vector<int> > ptrs = std::vector< std::vector<int> >(
                CALLSETS, std::vector<int>(HAPS, 0));
        std::vector< std::vector<int> > poss = std::vector< std::vector<int> >(
                CALLSETS, std::vector<int>(HAPS, 0));
        std::vector< std::vector<bool> > next = std::vector< std::vector<bool> >(
                CALLSETS, std::vector<bool>(HAPS, false));
        int sc_idx = 0;
        int ploidy = this->ploidy[std::find(contigs.begin(), contigs.end(), ctg)-contigs.begin()];
        std::shared_ptr<ctgPhaseblocks> ctg_pbs = this->phase_blocks[ctg];
        std::shared_ptr<ctgSuperclusters> ctg_scs = ctg_pbs->ctg_superclusters;
        if (ctg_scs->n == 0) continue;

        // set supercluster flip/swap based on phaseblock and sc phasing
        int phase_block = 0;
        bool phase_switch = ctg_pbs->block_states[phase_block];
        int phase_sc = ctg_scs->phase[sc_idx];
        bool phase_flip, swap;
        if (phase_switch) {
            if (phase_sc == PHASE_ORIG) { // flip error
                phase_flip = true;
                swap = false;
            } else { // PHASE_SWAP or PHASE_NONE
                phase_flip = false;
                swap = true;
            }
        } else { // no phase switch
            if (phase_sc == PHASE_SWAP) { // flip error
                phase_flip = true;
                swap = true;
            } else { // PHASE_ORIG or PHASE_NONE
                phase_flip = false;
                swap = false;
            }
        }

        auto & vars = ctg_scs->ctg_variants;
        while ( ptrs[QUERY][HAP1] < int(vars[QUERY][HAP1]->poss.size()) ||
                ptrs[QUERY][HAP2] < int(vars[QUERY][HAP2]->poss.size()) ||
                ptrs[TRUTH][HAP1] < int(vars[TRUTH][HAP1]->poss.size()) ||
                ptrs[TRUTH][HAP2] < int(vars[TRUTH][HAP2]->poss.size())) {

            // get next positions
            for (int c = 0; c < CALLSETS; c++) {
                for (int h = 0; h < HAPS; h++) {
                    poss[c][h] = ptrs[c][h] < int(vars[c][h]->poss.size()) ? 
                            vars[c][h]->poss[ptrs[c][h]] : 
                            std::numeric_limits<int>::max();
                    if (ptrs[c][h] < int(vars[c][h]->types.size()) &&
                            (vars[c][h]->types[ptrs[c][h]] == TYPE_INS ||
                            vars[c][h]->types[ptrs[c][h]] == TYPE_DEL)) poss[c][h]--;
                }
            }

            // set flags for next haps
            int pos = std::min( std::min(poss[QUERY][HAP1], poss[QUERY][HAP2]),
                    std::min(poss[TRUTH][HAP1], poss[TRUTH][HAP2]));
            for (int c = 0; c < CALLSETS; c++) {
                for (int h = 0; h < HAPS; h++) {
                    next[c][h] = (poss[c][h] == pos);
                }
            }

            // update supercluster and if phase is swapped
            if (pos >= ctg_scs->ends[sc_idx]) {
                // update supercluster
                sc_idx++;
                phase_sc = ctg_scs->phase[sc_idx];

                // update phase block
                if (sc_idx >= ctg_pbs->phase_blocks[phase_block+1])
                    phase_block++;
                phase_switch = ctg_pbs->block_states[phase_block];

                // update switch/flip status
                if (phase_switch) {
                    if (phase_sc == PHASE_ORIG) { // flip error
                        phase_flip = true;
                        swap = false;
                    } else { // PHASE_SWAP or PHASE_NONE
                        phase_flip = false;
                        swap = true;
                    }
                } else { // no phase switch
                    if (phase_sc == PHASE_SWAP) { // flip error
                        phase_flip = true;
                        swap = true;
                    } else { // PHASE_ORIG or PHASE_NONE
                        phase_flip = false;
                        swap = false;
                    }
                }
            }

            // store if query and truth contain same variant
            std::vector<bool> pair = {false, false};
            for (int h = 0; h < HAPS; h++) {
                pair[h] = next[QUERY][h] && next[TRUTH][swap^h] &&
                        vars[QUERY][h]->refs[ptrs[QUERY][h]] == 
                        vars[TRUTH][swap^h]->refs[ptrs[TRUTH][swap^h]] &&
                        vars[QUERY][h]->alts[ptrs[QUERY][h]] == 
                        vars[TRUTH][swap^h]->alts[ptrs[TRUTH][swap^h]];
            }

            if (next[QUERY][HAP1]) {
                if (next[QUERY][HAP2]) { // two query variants
                    // don't collapse homozygous variants (credit results may differ)
                    for(int h = 0; h < HAPS; h++) {
                        if (pair[h]) { // query matches truth
                            vars[QUERY][h]->print_var_info(out_vcf, this->ref, ctg, ptrs[QUERY][h]);
                            vars[TRUTH][h^swap]->print_var_sample(out_vcf, ptrs[TRUTH][h^swap], 
                                    (h^swap)?"0|1":"1|0", sc_idx, phase_block, phase_switch, phase_flip);
                            vars[QUERY][h]->print_var_sample(out_vcf, ptrs[QUERY][h], 
                                    h?"0|1":"1|0", sc_idx, phase_block, phase_switch, phase_flip, true);
                            ptrs[QUERY][h]++; ptrs[TRUTH][h^swap]++;
                        } else { // just query
                            vars[QUERY][h]->print_var_info(out_vcf, this->ref, ctg, ptrs[QUERY][h]);
                            vars[TRUTH][h^swap]->print_var_empty(out_vcf, sc_idx, phase_block);
                            vars[QUERY][h]->print_var_sample(out_vcf, ptrs[QUERY][h], 
                                    h?"0|1":"1|0", sc_idx, phase_block, phase_switch, phase_flip, true);
                            ptrs[QUERY][h]++;
                        }
                    }
                } else { // just query 1
                    if (pair[HAP1]) { // query matches truth
                        vars[QUERY][HAP1]->print_var_info(out_vcf, this->ref, ctg, ptrs[QUERY][HAP1]);
                        vars[TRUTH][HAP1^swap]->print_var_sample(out_vcf, ptrs[TRUTH][HAP1^swap], 
                                ploidy == 1 ? "1" : (HAP1^swap)?"0|1":"1|0", sc_idx, phase_block, phase_switch, phase_flip);
                        vars[QUERY][HAP1]->print_var_sample(out_vcf, ptrs[QUERY][HAP1], 
                                ploidy == 1 ? "1" : "1|0", sc_idx, phase_block, phase_switch, phase_flip, true);
                        ptrs[QUERY][HAP1]++; ptrs[TRUTH][HAP1^swap]++;
                    } else { // just query
                        vars[QUERY][HAP1]->print_var_info(out_vcf, this->ref, ctg, ptrs[QUERY][HAP1]);
                        vars[TRUTH][HAP1^swap]->print_var_empty(out_vcf, sc_idx, phase_block);
                        vars[QUERY][HAP1]->print_var_sample(out_vcf, ptrs[QUERY][HAP1], 
                                ploidy == 1 ? "1" : "1|0", sc_idx, phase_block, phase_switch, phase_flip, true);
                        ptrs[QUERY][HAP1]++;
                    }
                }
            } else if (next[QUERY][HAP2]) { // just query 2
                if (pair[HAP2]) { // query matches truth
                    vars[QUERY][HAP2]->print_var_info(out_vcf, this->ref, ctg, ptrs[QUERY][HAP2]);
                    vars[TRUTH][HAP2^swap]->print_var_sample(out_vcf, ptrs[TRUTH][HAP2^swap], 
                            ploidy == 1 ? "1" : (HAP2^swap)?"0|1":"1|0", sc_idx, phase_block, phase_switch, phase_flip);
                    vars[QUERY][HAP2]->print_var_sample(out_vcf, ptrs[QUERY][HAP2], 
                            ploidy == 1 ? "1" : "0|1", sc_idx, phase_block, phase_switch, phase_flip, true);
                    ptrs[QUERY][HAP2]++; ptrs[TRUTH][HAP2^swap]++;
                } else { // just query
                    vars[QUERY][HAP2]->print_var_info(out_vcf, this->ref, ctg, ptrs[QUERY][HAP2]);
                    vars[TRUTH][HAP2^swap]->print_var_empty(out_vcf, sc_idx, phase_block);
                    vars[QUERY][HAP2]->print_var_sample(out_vcf, ptrs[QUERY][HAP2], 
                            ploidy == 1 ? "1" : "0|1", sc_idx, phase_block, phase_switch, phase_flip, true);
                    ptrs[QUERY][HAP2]++;
                }
            } else { // no query variants, just truth
                if (next[TRUTH][HAP1^swap] && next[TRUTH][HAP2^swap]) { // two truth variants
                    // don't collapse homozygous variants (credit results may differ)
                    for(int h = 0; h < HAPS; h++) {
                        vars[TRUTH][h^swap]->print_var_info(out_vcf, this->ref, ctg, ptrs[TRUTH][h^swap]);
                        vars[TRUTH][h^swap]->print_var_sample(out_vcf, ptrs[TRUTH][h^swap], 
                                (h^swap)?"0|1":"1|0", sc_idx, phase_block, phase_switch, phase_flip);
                        vars[QUERY][h]->print_var_empty(out_vcf, sc_idx, phase_block, true);
                        ptrs[TRUTH][h^swap]++;
                    }
                } else { // one truth variant
                    if (next[TRUTH][HAP1^swap]) {
                        vars[TRUTH][HAP1^swap]->print_var_info(out_vcf, this->ref, ctg, ptrs[TRUTH][HAP1^swap]);
                        vars[TRUTH][HAP1^swap]->print_var_sample(out_vcf, ptrs[TRUTH][HAP1^swap], 
                                ploidy == 1 ? "1" : (HAP1^swap)?"0|1":"1|0", sc_idx, phase_block, phase_switch, phase_flip);
                        vars[QUERY][HAP1]->print_var_empty(out_vcf, sc_idx, phase_block, true);
                        ptrs[TRUTH][HAP1^swap]++;
                    } else if (next[TRUTH][HAP2^swap]) {
                        vars[TRUTH][HAP2^swap]->print_var_info(out_vcf, this->ref, ctg, ptrs[TRUTH][HAP2^swap]);
                        vars[TRUTH][HAP2^swap]->print_var_sample(out_vcf, ptrs[TRUTH][HAP2^swap], 
                                ploidy == 1 ? "1" : (HAP2^swap)?"0|1":"1|0", sc_idx, phase_block, phase_switch, phase_flip);
                        vars[QUERY][HAP2]->print_var_empty(out_vcf, sc_idx, phase_block, true);
                        ptrs[TRUTH][HAP2^swap]++;
                    } else {
                        ERROR("No variants are selected next.");
                    }
                }
            }
        }
    }
    fclose(out_vcf);
}


/*******************************************************************************/


phaseblockData::phaseblockData(std::shared_ptr<superclusterData> clusterdata_ptr)
{
    // copy contigs and reference
    for (int i = 0; i < int(clusterdata_ptr->contigs.size()); i++) {
        std::string ctg = clusterdata_ptr->contigs[i];
        this->contigs.push_back(ctg);
        this->lengths.push_back(clusterdata_ptr->lengths[i]);
        this->ploidy.push_back(clusterdata_ptr->ploidy[i]);
        this->phase_blocks[ctg] = std::shared_ptr<ctgPhaseblocks>(new ctgPhaseblocks());
    }
    this->ref = clusterdata_ptr->ref;

    // add pointers to clusters
    for (std::string ctg : clusterdata_ptr->contigs) {
        this->phase_blocks[ctg]->ctg_superclusters = clusterdata_ptr->superclusters[ctg];
    }
    this->phase();
}


/*******************************************************************************/


void phaseblockData::phase()
{
    if (g.verbosity >= 1) INFO(" ");
    if (g.verbosity >= 1) INFO("%s[7/8] Phasing superclusters%s",
            COLOR_PURPLE, COLOR_WHITE);

    // phase each contig separately
    for (std::string ctg : this->contigs) {
        std::shared_ptr<ctgPhaseblocks> ctg_pbs = this->phase_blocks[ctg];
        std::shared_ptr<ctgSuperclusters> ctg_scs = ctg_pbs->ctg_superclusters;
        std::vector< std::vector<int> > mat(2, std::vector<int>(ctg_scs->n+1));
        std::vector< std::vector<int> > ptrs(2, std::vector<int>(ctg_scs->n));

        // forward pass
        for (int i = 0; i < ctg_scs->n; i++) {

            // determine costs (penalized if this phasing deemed incorrect)
            std::vector<int> costs = {0, 0};
            switch (ctg_scs->phase[i]) {
                case PHASE_NONE: 
                    costs[PHASE_PTR_KEEP] = 0; 
                    costs[PHASE_PTR_SWAP] = 0; 
                    break;
                case PHASE_ORIG: 
                    costs[PHASE_PTR_KEEP] = 0; 
                    costs[PHASE_PTR_SWAP] = 1; 
                     break;
                case PHASE_SWAP:
                    costs[PHASE_PTR_KEEP] = 1; 
                    costs[PHASE_PTR_SWAP] = 0; 
                     break;
                default:
                    ERROR("Unexpected phase (%d)", ctg_scs->phase[i]);
                    break;
            }

            // no cost for phase switches if on border of phase set
            int cost_swap = 1;
            if (i+1 < ctg_scs->n && ctg_scs->phase_sets[i] != ctg_scs->phase_sets[i+1]) {
                cost_swap = 0;
            }

            for (int phase = 0; phase < 2; phase++) {
                if (mat[phase][i] + costs[phase] <= mat[phase^1][i] + cost_swap) {
                    mat[phase][i+1] = mat[phase][i] + costs[phase];
                    ptrs[phase][i] = PHASE_PTR_KEEP;
                }
                else {
                    mat[phase][i+1] = mat[phase^1][i] + cost_swap;
                    ptrs[phase][i] = PHASE_PTR_SWAP;
                }
            }
        }

        // backwards pass
        if (ctg_scs->n > 0) { // skip empty contigs

            // determine starting phase
            int phase = 0;
            if (mat[PHASE_SWAP][ctg_scs->n] < mat[PHASE_ORIG][ctg_scs->n])
                phase = 1;

            int i = ctg_scs->n-1;
            ctg_pbs->phase_blocks.push_back(i+1);
            while (i > 0) {
                if (ptrs[phase][i] == PHASE_PTR_SWAP) { // new phase block
                    ctg_pbs->n++;

                    // not a switch error if between phase sets
                    if (i+1 < ctg_scs->n && ctg_scs->phase_sets[i] == ctg_scs->phase_sets[i+1])
                        ctg_pbs->nswitches++;

                    ctg_pbs->phase_blocks.push_back(i+1);
                    ctg_pbs->block_states.push_back(phase);
                    phase ^= 1;
                } else if (ptrs[phase][i] == PHASE_PTR_KEEP) { // within phase block
                    if (ctg_scs->phase[i] != PHASE_NONE && ctg_scs->phase[i] != phase)
                        ctg_pbs->nflips++;
                }
                i--;
            }
            ctg_pbs->phase_blocks.push_back(0);
            std::reverse(ctg_pbs->phase_blocks.begin(), ctg_pbs->phase_blocks.end());
            ctg_pbs->block_states.push_back(phase);
            std::reverse(ctg_pbs->block_states.begin(), ctg_pbs->block_states.end());
        }
    }
    

    // print
    int switch_errors = 0;
    int flip_errors = 0;
    int phase_blocks = 0;
    int superclusters = 0;
    if (g.verbosity >= 1) INFO("  Contigs:");
    int id = 0;
    for (std::string ctg : this->contigs) {
        std::shared_ptr<ctgPhaseblocks> ctg_pbs = this->phase_blocks[ctg];
        std::shared_ptr<ctgSuperclusters> ctg_scs = ctg_pbs->ctg_superclusters;

        // verbose, print phase of each supercluster
        if (g.verbosity >= 2) {
            int s = 0;
            std::vector<std::string> colors {"\033[34m", "\033[32m"};
            int color_idx = 0;
            for (int i = 0; i < ctg_scs->n; i++) {
                if (i == ctg_pbs->phase_blocks[s]) {
                    printf("%s", colors[color_idx].data());
                    color_idx ^= 1;
                    s++;
                }
                int phase = ctg_scs->phase[i];
                printf("%s", phase_strs[phase].data());
            }
            printf("\033[0m\n");
        }

        // print errors per contig
        if (g.verbosity >= 1) {
            INFO("    [%2d] %s: %d switches, %d flips, %d phase blocks", id, ctg.data(), 
                    ctg_pbs->nswitches, ctg_pbs->nflips, ctg_pbs->n);
        }
        superclusters += ctg_scs->n;
        switch_errors += ctg_pbs->nswitches;
        flip_errors += ctg_pbs->nflips;
        phase_blocks += ctg_pbs->n;
        id++;
    }
    int ng50 = this->calculate_ng50();
    int ngc50 = this->calculate_ngc50();

    if (g.verbosity >= 1) INFO(" ");
    if (g.verbosity >= 1) INFO("   Total switch errors: %d", switch_errors);
    if (g.verbosity >= 1) INFO("   Total   flip errors: %d", flip_errors);
    if (g.verbosity >= 1) INFO("   Total  phase blocks: %d", phase_blocks);
    if (g.verbosity >= 1) INFO("     Phase block  NG50: %d", ng50);
    if (g.verbosity >= 1) INFO("     Phase block NGC50: %d", ngc50);
    if (g.verbosity >= 1 && superclusters) 
        INFO("  SC Switch error rate: %.6f%%", 100*switch_errors/float(superclusters));
    if (g.verbosity >= 1 && superclusters) 
        INFO("    SC Flip error rate: %.6f%%", 100*flip_errors/float(superclusters));
}


/*******************************************************************************/


int phaseblockData::calculate_ngc50() {

    // get total bases in genome
    size_t total_bases = 0;
    for (size_t i = 0; i < this->contigs.size(); i++) {
        total_bases += lengths[i];
    }

    // get sizes of each correct phase block (split on flips, not just switch)
    std::vector<int> correct_blocks;
    for (std::string ctg: this->contigs) {
        
        std::shared_ptr<ctgPhaseblocks> ctg_pbs = this->phase_blocks[ctg];
        std::shared_ptr<ctgSuperclusters> ctg_scs = ctg_pbs->ctg_superclusters;
        for (int pb = 0; pb < ctg_pbs->n && ctg_scs->n > 0; pb++) { // each phase block

            // init start of this correct block
            int sc = ctg_pbs->phase_blocks[pb];
            int beg = ctg_scs->begs[sc];
            int end = 0;
            for (; sc < ctg_pbs->phase_blocks[pb+1]; sc++) { // each superclsuter
                if (ctg_scs->phase[sc] != PHASE_NONE &&  // flip error
                        ctg_scs->phase[sc] != ctg_pbs->block_states[pb]) {
                    end = ctg_scs->ends[sc-1];
                    correct_blocks.push_back(end-beg);
                    beg = ctg_scs->begs[sc]; // reset
                }
            }
            end = ctg_scs->ends[sc-1];
            correct_blocks.push_back(end-beg);
        }
    }

    // return NGC50
    size_t total_correct = 0;
    std::sort(correct_blocks.begin(), correct_blocks.end(), std::greater<>());
    for (size_t i = 0; i < correct_blocks.size(); i++) {
        total_correct += correct_blocks[i];
        if (total_correct >= total_bases / 2)
            return correct_blocks[i];
    }
    return 0;
}


/*******************************************************************************/


int phaseblockData::calculate_ng50() {

    // get total bases in genome
    size_t total_bases = 0;
    for (size_t i = 0; i < this->contigs.size(); i++) {
        total_bases += lengths[i];
    }

    // get sizes of each phase block
    std::vector<int> phase_blocks;
    for (std::string ctg: this->contigs) {
        
        std::shared_ptr<ctgPhaseblocks> ctg_pbs = this->phase_blocks[ctg];
        std::shared_ptr<ctgSuperclusters> ctg_scs = ctg_pbs->ctg_superclusters;
        for (int i = 0; i < ctg_pbs->n && ctg_scs->n > 0; i++) {
            int beg_idx = ctg_pbs->phase_blocks[i];
            int end_idx = ctg_pbs->phase_blocks[i+1]-1;
            int beg = ctg_scs->begs[beg_idx];
            int end = ctg_scs->ends[end_idx];
            phase_blocks.push_back(end-beg);
        }
    }

    // return NG50
    size_t total_phased = 0;
    std::sort(phase_blocks.begin(), phase_blocks.end(), std::greater<>());
    for (size_t i = 0; i < phase_blocks.size(); i++) {
        total_phased += phase_blocks[i];
        if (total_phased >= total_bases / 2)
            return phase_blocks[i];
    }
    return 0;
}
