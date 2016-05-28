#ifndef UNIQUE_OBS_H
#define UNIQUE_OBS_H
#include <cmath>
#include <unordered_map>
#include "dlib/bam_util.h"
#include "dlib/vcf_util.h"


#define DEFAULT_MAX_DEPTH (1 << 18)

namespace bmf {

    struct stack_conf_t {
        float min_fr; // Minimum fraction of family members agreed on base
        float minAF; // Minimum aligned fraction
        int max_depth;
        uint32_t minFM;
        uint32_t minFA;
        uint32_t minPV;
        uint32_t minmq;
        int min_count;
        int min_duplex;
        int min_overlap;
        int skip_improper;
        uint32_t skip_flag; // Skip reads with any bits set to true
        int output_bcf;
    };

    class SampleVCFPos;

    class UniqueObservation {
    friend SampleVCFPos;
    std::string qname;
    int16_t cycle1;
    int16_t cycle2; // Masked, from other read, if it was found.
    uint32_t quality:16;
    uint32_t mq1:8;
    uint32_t mq2:8;
    uint32_t rv:16;
    uint32_t discordant:1;
    uint32_t is_duplex1:1;
    uint32_t is_duplex2:1;
    uint32_t is_reverse1:1;
    uint32_t is_reverse2:1;
    uint32_t is_overlap:1;
    uint32_t pass:1;
    double pvalue;
    int flag; // BAM flag
    char base1;
    char base2; // Masked, from other read
    char base_call;
    uint32_t agreed:16;
    uint32_t size:32;
    //size_t size;
    public:
        int is_pass() {
            return pass;
        }
        uint32_t get_size() {
            return size;
        }
        uint32_t get_agreed() {
            return agreed;
        }
        bool mate_added() {
            return mq2 != (uint8_t)-1;
        }
        int get_reverse() {
            return is_reverse1 + mate_added() ? is_reverse2: 0;
        }
        void set_pass(int _pass) {
            pass = _pass;
        }
        uint32_t get_meanMQ() {
            return mate_added() ? mq1: ((mq2 + mq1 + 0.5) / 2);
        }
        double get_FA() {
            return (double)agreed / size;
        }
        int get_overlap() {return is_overlap;}
        uint32_t get_quality() {return quality;}
        int get_duplex() {
            return is_duplex1 + mate_added() ? is_duplex2: 0;
        }
        UniqueObservation() {
            memset(this, 0, sizeof(*this));
        }
        UniqueObservation(const bam_pileup1_t& plp):
            qname(bam_get_qname(plp.b)),
            cycle1(dlib::arr_qpos(&plp)),
            cycle2(-1),
            quality(((uint32_t *)dlib::array_tag(plp.b, "PV"))[cycle1]),
            mq1(plp.b->core.qual),
            mq2((uint8_t)-1),
            rv((uint32_t)bam_itag(plp.b, "RV")),
            discordant(0),
            is_duplex1(bam_itag(plp.b, "DR")),
            is_duplex2(0),
            is_reverse1((plp.b->core.flag & BAM_FREVERSE) != 0),
            is_reverse2(0),
            is_overlap(0),
            pass(1),
            pvalue(std::pow(10, quality - 0.1)),
            flag(plp.b->core.flag),
            base1(seq_nt16_str[bam_seqi(bam_get_seq(plp.b), plp.qpos)]),
            base2('\0'),
            base_call(base1),
            agreed(((uint32_t *)dlib::array_tag(plp.b, "FA"))[cycle1]),
            size(bam_itag(plp.b, "FM"))
        {
        }
        void add_obs(const bam_pileup1_t& plp);
    };

    struct stack_aux_t {
        stack_conf_t conf;
        dlib::BamHandle tumor;
        dlib::BamHandle normal;
        dlib::VcfHandle vcf;
        faidx_t *fai;
        khash_t(bed) *bed;
        int last_tid;
        char *ref_seq;
        void pair_region_itr(int bamtid, int start, int stop, int &tn_plp, int &tpos, int &ttid, int &nn_plp, int &npos, int &ntid) {
            bam_plp_reset(tumor.plp);
            bam_plp_reset(normal.plp);
            if(tumor.iter) hts_itr_destroy(tumor.iter);
            if(normal.iter) hts_itr_destroy(normal.iter);
            tumor.iter = bam_itr_queryi(tumor.idx, bamtid, start, stop);
            normal.iter = bam_itr_queryi(normal.idx, bamtid, start, stop);
            while((tumor.pileups = bam_plp_auto(tumor.plp, &ttid, &tpos, &tn_plp)) != 0) {
                if(tpos < start && ttid == bamtid) continue;
                if(tpos >= start) {
                    //LOG_DEBUG("Breaking? %i >= start (%i)\n", tpos, start);
                    break;
                }
                LOG_EXIT("Wrong tid (ttid: %i, bamtid %i)? wrong pos? tpos, stop %i, %i", ttid, bamtid, tpos, stop);
            }
            while((normal.pileups = bam_plp_auto(normal.plp, &ntid, &npos, &nn_plp)) != 0) {
                if(npos < start && ntid == bamtid) continue;
                if(npos >= start) {
                    //LOG_DEBUG("Breaking? %i >= start (%i)\n", npos, start);
                    break;
                }
            }
            assert(npos == tpos && ntid == ttid);
            //LOG_DEBUG("Advanced to %i:%i/%i:%i.\n", ttid, bamtid, npos, start);
            //LOG_DEBUG("N pileups: %i, %i.\n", tn_plp, nn_plp);
        }
        int next_paired_pileup(int *ttid, int *tpos, int *tn_plp, int *ntid, int *npos, int *nn_plp, int stop) {
            if((tumor.pileups = bam_plp_auto(tumor.plp, ttid, tpos, tn_plp)) != 0 &&
                    (normal.pileups = bam_plp_auto(normal.plp, ntid, npos, nn_plp)) != 0) {
                return *npos < stop;
            }
            return 0;
        }
        stack_aux_t(char *tumor_path, char *normal_path, char *vcf_path, bcf_hdr_t *vh, stack_conf_t conf):
            conf(conf),
            tumor(tumor_path),
            normal(normal_path),
            vcf(vcf_path, vh, conf.output_bcf ? "wb": "w"),
            fai(nullptr),
            bed(nullptr),
            last_tid(-1),
            ref_seq(nullptr)
        {
            dlib::bcf_add_bam_contigs(vcf.vh, tumor.header);
            if(!conf.max_depth) conf.max_depth = DEFAULT_MAX_DEPTH;
        }
        char get_ref_base(int tid, int pos) {
            //LOG_DEBUG("fai ptr %p.\n", (void *)fai);
            int len;
            if(tid != last_tid) {
                //LOG_DEBUG("Loading ref_seq\n");
                if(ref_seq) free(ref_seq);
                ref_seq = fai_fetch(fai, tumor.header->target_name[tid], &len);
                last_tid = tid;
            }
            //LOG_DEBUG("Returning ref base\n");
            return ref_seq[pos];
        }
        ~stack_aux_t() {
            if(bed) dlib::bed_destroy_hash((void *)bed);
            if(ref_seq) free(ref_seq);
        }
    };

    class PairVCFPos;

    class SampleVCFPos {
        friend PairVCFPos;
        std::unordered_map<char, std::vector<UniqueObservation *>> templates;
        size_t size;
        int32_t pos;
        int32_t tid;
    public:
        void to_bcf(bcf1_t *vrec, bcf_hdr_t *hdr, char refbase);
        SampleVCFPos(std::unordered_map<std::string, UniqueObservation>& obs, int32_t _tid, int32_t _pos);
    };

    class PairVCFPos {
        SampleVCFPos tumor;
        SampleVCFPos normal;
    public:
        void to_bcf(bcf1_t *vrec, stack_aux_t *aux, int ttid, int tpos);
        PairVCFPos(std::unordered_map<std::string, UniqueObservation>& tobs,
                   std::unordered_map<std::string, UniqueObservation>& nobs,
                   int32_t tid, int32_t pos):
                        tumor(tobs, tid, pos),
                        normal(nobs, tid, pos)
        {
        }
    };
    static inline int expected_count(std::vector<uint32_t> &phred_vector) {
        // Do this instead of
        // ret += 1 - (std::pow(10., i * -.1));
        // That way, we only increment once. Does it really matter? No, but it's elegant.
        double ret = phred_vector.size();
        for(int i: phred_vector)
            ret -= std::pow(10., i * -.1);
        return (int)(ret + 0.5);
    }
    static inline int expected_incorrect(std::vector<std::vector<uint32_t>> &conf_vec, std::vector<std::vector<uint32_t>> &susp_vec, int j) {
        double ret = 0.;
        for(unsigned i = 0; i != conf_vec.size(); ++i) {
            if(i != (unsigned)j) {
                for(auto k: conf_vec[i])
                    // Probability the base call is incorrect, over 3, as the incorrect base call could have been any of the other 3.
                    ret += std::pow(10., k * -.1) / 3;
                for(auto k: susp_vec[i])
                    ret += std::pow(10., k * -.1) / 3;
            }
        }
        return (int)(ret + 0.5);
    }
    static inline int estimate_quantity(std::vector<std::vector<uint32_t>> &confident_phreds, std::vector<std::vector<uint32_t>> &suspect_phreds, int j) {
        const int ret = confident_phreds[j].size();
        const int putative_suspects = expected_count(suspect_phreds[j]); // Trust all of the confident base calls as real.
        const int expected_false_positives = expected_incorrect(confident_phreds, suspect_phreds, j);
        return (expected_false_positives >= putative_suspects) ? ret
                                                               : ret + putative_suspects - expected_false_positives;
    }
    void add_stack_lines(bcf_hdr_t *hdr);

} /* namespace bmf */

#endif
