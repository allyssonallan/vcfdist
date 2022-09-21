#ifndef _VCF_H_
#define _VCF_H_

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

#include "htslib/vcf.h"

#include "fasta.h"

#define TYPE_REF 0
#define TYPE_SUB 1
#define TYPE_INS 2
#define TYPE_DEL 3
#define TYPE_GRP 4

#define HAP1 0
#define HAP2 1

#define FAIL 0
#define PASS 1

#define GT_DOT_DOT   0
#define GT_REF_REF   1
#define GT_REF_ALT1  2
#define GT_REF_ALT2  3
#define GT_ALT1_REF  4
#define GT_ALT1_ALT1 5
#define GT_ALT1_ALT2 6
#define GT_ALT2_REF  7
#define GT_ALT2_ALT1 8
#define GT_ALT2_ALT2 9
#define GT_OTHER     10

class ctgVariants {
public:

    // constructor
    ctgVariants();

    // helper functions
    void add_cluster(int g);
    void add_var(int pos, int rlen, uint8_t hap, uint8_t type, 
            std::string ref, std::string alt, float gq, float vq);

    // data
    std::vector<int> clusters;      // indices of clusters in this struct's vectors
    std::vector<int> poss;          // variant start positions (0-based)
    std::vector<int> rlens;         // reference lengths
    std::vector<uint8_t> haps;      // variant haplotype
    std::vector<uint8_t> types;     // variant type: NONE, SUB, INS, DEL, GRP
    std::vector<std::string> refs;  // variant reference allele
    std::vector<std::string> alts;  // variant alternate allele (always one)
    std::vector<float> gt_quals;    // genotype quality (0-60)
    std::vector<float> var_quals;   // variant quality (0-60)
    int n = 0;
};

class variantData {
public:
    // constructors
    variantData();
    variantData(std::string vcf_fn, std::shared_ptr<fastaData> reference);

    // functions
    void write(std::string vcf_fn);
    void print_variant(FILE* out_fp, std::string ctg, int pos, int type,
        std::string ref, std::string alt, float qual, std::string gt);

    // data
    std::string sample;
    std::vector<std::string> contigs;
    std::vector<int> lengths;
    std::vector< std::unordered_map<std::string, std::shared_ptr<ctgVariants> > > ctg_variants;

    std::shared_ptr<fastaData> ref;
};

#endif