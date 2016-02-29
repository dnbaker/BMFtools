#include "bmf_target.h"

int target_usage(FILE *fp, int retcode)
{
    fprintf(fp, "Usage: bmftools target -b <path.to.bed> <in.bam> \n"
            "Optional arguments:\n"
            "-m\tSet minimum mapping quality for inclusion.\n"
            "-p\tSet padding - number of bases around target region to consider as on-target. Default: 0.\n"
            "-n\tSet notification interval - number of reads between logging statements. Default: 1000000.\n");
    exit(retcode);
    return retcode; // This never happens.
}

target_counts_t target_core(char *bedpath, char *bampath, uint32_t padding, uint32_t minMQ, uint64_t notification_interval)
{
    samFile *fp = NULL;
    bam_hdr_t *header = NULL;
    if ((fp = sam_open(bampath, "r")) == NULL) {
        LOG_EXIT("Cannot open input file \"%s\"", bampath);
    }
    if ((header = sam_hdr_read(fp)) == NULL) {
        LOG_EXIT("Failed to read header for \"%s\"\n", bampath);
    }
    khash_t(bed) *bed = parse_bed_hash(bedpath, header, padding);
    target_counts_t counts;
    memset(&counts, 0, sizeof(target_counts_t));
    bam1_t *b = bam_init1();
    int c;
    while (LIKELY((c = sam_read1(fp, header, b)) >= 0)) {
        if((b->core.qual < minMQ) || (b->core.flag & (2820))) { // 2820 is unmapped, secondary, supplementary, qcfail
            ++counts.n_skipped;
            continue;
        }
        if(bed_test(b, bed)) ++counts.target;
        if(UNLIKELY(++counts.count % notification_interval == 0)) {
            LOG_INFO("Number of records processed: %lu.\n", ++counts.count);
        }
    }
    bam_destroy1(b);
    bam_hdr_destroy(header);
    sam_close(fp);
    bed_destroy_hash(bed);
    return counts;
}


int target_main(int argc, char *argv[])
{
    char *bedpath = NULL;
    uint32_t padding = (uint32_t)-1, minMQ = 0;
    uint64_t notification_interval = 1000000;


    if(argc < 4) return target_usage(stderr, EXIT_SUCCESS);

    if(strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) return target_usage(stderr, EXIT_SUCCESS);

    int c;
    while ((c = getopt(argc, argv, "m:b:p:n:h?")) >= 0) {
        switch (c) {
        case 'm':
            minMQ = strtoul(optarg, NULL, 0); break;
        case 'b':
            bedpath = strdup(optarg); break;
        case 'p':
            padding = strtoul(optarg, NULL, 0); break;
        case 'n':
            notification_interval = strtoull(optarg, NULL, 0); break;
        case '?': case 'h':
            return target_usage(stderr, EXIT_SUCCESS);
        }
    }


    if(padding == (uint32_t)-1) {
        padding = DEFAULT_PADDING;
        LOG_INFO("Padding not set. Setting to default (%u).\n", padding);
    }

    if (argc != optind+1)
        return (argc == optind) ? target_usage(stdout, EXIT_SUCCESS): target_usage(stderr, EXIT_FAILURE);

    target_counts_t counts = target_core(bedpath, argv[optind], padding, minMQ, notification_interval);

    if(!bedpath) {
        fprintf(stderr, "[E:%s] Bed path required for bmftools target. See usage.\n", __func__);
        return target_usage(stderr, EXIT_FAILURE);
    }

    // Open [smt]am file.

    if(bedpath) free(bedpath);


    LOG_INFO("Number of reads skipped: %lu.\n", counts.n_skipped);
    LOG_INFO("Number of reads total: %lu.\n", counts.count);
    LOG_INFO("Number of reads on target: %lu.\n", counts.target);
    fprintf(stdout, "Fraction of reads on target with padding of %u bases and %i minMQ: %0.12f.\n",
            padding, minMQ, (double)counts.target / counts.count);
    LOG_DEBUG("target: %lu. Count: %lu.\n", counts.target, counts.count);
    return EXIT_SUCCESS;
}
