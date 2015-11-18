#include "famstats.h"

int RVWarn = 1;
#ifndef notification_interval
#define notification_interval 1000000
#endif

int frac_usage_exit(FILE *fp, int code) {
	fprintf(fp, "famstats frac <opts> <in.bam>\n"
			"Opts:\n-m minFM to accept. REQUIRED.\n"
			"-h, -?: Return usage.\n");
	exit(code);
}


KHASH_MAP_INIT_INT(bed, region_set_t)

static void print_hashstats(famstats_t *stats)
{
	fprintf(stdout, "#Family size\tNumber of families\n");
	for(stats->ki = kh_begin(stats->fm); stats->ki != kh_end(stats->fm); ++stats->ki) {
		if(!kh_exist(stats->fm, stats->ki))
			continue;
		fprintf(stdout, "%"PRIu64"\t%"PRIu64"\n", kh_key(stats->fm, stats->ki), kh_val(stats->fm, stats->ki));
	}
	fprintf(stderr, "#RV'd in family\tNumber of families\n");
	for(stats->ki = kh_begin(stats->rc); stats->ki != kh_end(stats->rc); ++stats->ki) {
		if(!kh_exist(stats->rc, stats->ki))
			continue;
		fprintf(stderr, "%"PRIu64"\t%"PRIu64"\n", kh_key(stats->rc, stats->ki), kh_val(stats->rc, stats->ki));
	}
	return;
}


static inline int bed_test(bam1_t *b, khash_t(bed) *h)
{
	khint_t k;
	k = kh_get(bed, h, b->core.tid);
	if(k == kh_end(h)) {
		return 0;
	}
	uint32_t pos_plus_len = b->core.pos + b->core.l_qseq;
	for(uint64_t i = 0; i < kh_val(h, k).n; ++i) {
		if(pos_plus_len >= kh_val(h, k).intervals[i].start && b->core.pos <= kh_val(h, k).intervals[i].end) {
			return 1;
		}
	}
	return 0;
}


int intcmp(const void *a, const void *b)
{
	size_t diff = (*(interval_t *)a).start - (*(interval_t *)b).start;
	return (diff) ? diff: (*(interval_t *)a).end - (*(interval_t *)b).end;
}


static void sort_bed(khash_t(bed) *bed)
{
	khint_t k;
	for(k = kh_begin(bed); k != kh_end(bed); ++k) {
		if(!kh_exist(bed, k))
			continue;
		qsort(kh_val(bed, k).intervals, kh_val(bed, k).n, sizeof(region_set_t), &intcmp);
	}
	return;
}


static khash_t(bed) *parse_bed(char *path, bam_hdr_t *header, int padding)
{
	khash_t(bed) *ret = kh_init(bed);
	FILE *ifp = fopen(path, "r");
	char *line = NULL;
	char *tok = NULL;
	size_t len = 0;
	ssize_t read;
	uint32_t tid, start, stop;
	int khr;
	khint_t k;
	while ((read = getline(&line, &len, ifp)) != -1) {
		tok = strtok(line, "\t");
		tid = (uint32_t)bam_name2id(header, tok);
#if !NDEBUG
		fprintf(stderr, "Transcript id for tok %s is %"PRIu32"\n", tok, tid);
#endif
		tok = strtok(NULL, "\t");
		start = strtoul(tok, NULL, 10);
		tok = strtok(NULL, "\t");
		stop = strtoul(tok, NULL, 10);
		k = kh_get(bed, ret, tid);
		if(k == kh_end(ret)) {
#if !NDEBUG
			fprintf(stderr, "New contig in bed hashmap: %"PRIu32".\n", tid);
#endif
			k = kh_put(bed, ret, tid, &khr);
			kh_val(ret, k).intervals = (interval_t *)calloc(1, sizeof(interval_t));
			kh_val(ret, k).intervals[0].start = start - padding > 0 ? start - padding : 0;
			kh_val(ret, k).intervals[0].end = stop + padding;
			kh_val(ret, k).n = 1;
		}
		else {
			kh_val(ret, k).intervals = (interval_t *)realloc(kh_val(ret, k).intervals, ++kh_val(ret, k).n * sizeof(interval_t));
			if(!kh_val(ret, k).intervals) {
				fprintf(stderr, "Could not allocate memory. Abort mission!\n");
				exit(EXIT_FAILURE);
			}
			kh_val(ret, k).intervals[kh_val(ret, k).n - 1].start = start;
			kh_val(ret, k).intervals[kh_val(ret, k).n - 1].end = stop;
#if !NDEBUG
			fprintf(stderr, "Number of intervals in bed file for contig %"PRIu32": %"PRIu64"\n", tid, kh_val(ret, k).n);
#endif
		}
	}
	sort_bed(ret);
	return ret;
}

void target_usage_exit(FILE *fp, int success)
{
	fprintf(fp, "Usage: famstats target <opts> <in.bam>\nOpts:\n-b Path to bed file.\n"
			"-p padding. Number of bases around bed regions to pad. Default: 25.\n"
			"-h, -?: Return usage.\n");
	exit(success);
}

void bed_destroy(khash_t(bed) *b)
{
	khint_t ki;
	for(ki = kh_begin(b); ki != kh_end(b); ++ki) {
		if(!kh_exist(b, ki))
			continue;
		cond_free(kh_val(b, ki).intervals);
		kh_val(b, ki).n = 0;
	}
	cond_free(b);
}

static inline void target_loop(bam1_t *b, khash_t(bed) *bed, uint64_t *fm_target, uint64_t *total_fm)
{
	if(bed_test(b, bed)) {
		*fm_target += bam_aux2i(bam_aux_get(b, "FM"));
	}
	else {
		*total_fm += bam_aux2i(bam_aux_get(b, "FM"));
	}
}


int target_main(int argc, char *argv[])
{
	samFile *fp;
	bam_hdr_t *header;
	int c;
	khash_t(bed) *bed = NULL;
	char *bedpath = NULL;
	int padding = -1;

	if(argc < 4) {
		target_usage_exit(stderr, EXIT_FAILURE);
	}

	if(strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) target_usage_exit(stderr, EXIT_SUCCESS);

	while ((c = getopt(argc, argv, "b:p:n:h?")) >= 0) {
		switch (c) {
		case 'b':
			bedpath = strdup(optarg);
			break;
		case 'p':
			padding = atoi(optarg);
			break;
		case '?':
		case 'h':
			target_usage_exit(stderr, EXIT_SUCCESS);
		default:
			target_usage_exit(stderr, EXIT_FAILURE);
		}
	}


	if(padding < 0) {
		padding = 25;
		fprintf(stderr, "[famstat_target_main]: Padding not set. Set to 25.\n");
	}

	if (argc != optind+1) {
		if (argc == optind) target_usage_exit(stdout, EXIT_SUCCESS);
		else target_usage_exit(stderr, EXIT_FAILURE);
	}

	if(!bedpath) {
		fprintf(stderr, "Bed path required for famstats target. See usage!\n");
		target_usage_exit(stderr, EXIT_FAILURE);
	}

	fp = sam_open(argv[optind], "r");
	if (fp == NULL) {
		fprintf(stderr, "[famstat_target_main]: Cannot open input file \"%s\"", argv[optind]);
		exit(EXIT_FAILURE);
	}

	header = sam_hdr_read(fp);
	if (header == NULL) {
		fprintf(stderr, "[famstat_target_main]: Failed to read header for \"%s\"\n", argv[optind]);
		exit(EXIT_FAILURE);
	}
	bed = parse_bed(bedpath, header, padding);
	uint64_t fm_target = 0, total_fm = 0, count = 0;
	bam1_t *b = bam_init1();
	while ((c = sam_read1(fp, header, b)) >= 0) {
		target_loop(b, bed, &fm_target, &total_fm);
		if(++count % notification_interval == 0)
			fprintf(stderr, "[famstat_target_core] Number of records processed: %"PRIu64".\n", count);
	}
	bam_destroy1(b);
	bam_hdr_destroy(header);
	sam_close(fp);
	bed_destroy(bed);
	free(bedpath);
	fprintf(stdout, "Fraction of raw reads on target: %f. \nTotal raw reads: %"PRIu64". Raw reads on target: %"PRIu64".\n",
			(double)fm_target / total_fm, total_fm, fm_target);
	return 0;
}


static void print_stats(famstats_t *stats)
{
	fprintf(stderr, "#Number passing filters: %"PRIu64".\n", stats->n_pass);
	fprintf(stderr, "#Number failing filters: %"PRIu64".\n", stats->n_fail);
	fprintf(stderr, "#Summed FM (total founding reads): %"PRIu64".\n", stats->allfm_sum);
	fprintf(stderr, "#Summed FM (total founding reads), (FM > 1): %"PRIu64".\n", stats->realfm_sum);
	fprintf(stderr, "#Summed RV (total reverse-complemented reads): %"PRIu64".\n", stats->allrc_sum);
	fprintf(stderr, "#Summed RV (total reverse-complemented reads), (FM > 1): %"PRIu64".\n", stats->realrc_sum);
	fprintf(stderr, "#RV fraction for all read families: %lf.\n", (double)stats->allrc_sum / (double)stats->allfm_sum);
	fprintf(stderr, "#RV fraction for real read families: %lf.\n", (double)stats->realrc_sum / (double)stats->realfm_sum);
	fprintf(stderr, "#Mean Family Size (all)\t%lf\n", (double)stats->allfm_sum / (double)stats->allfm_counts);
	fprintf(stderr, "#Mean Family Size (real)\t%lf\n", (double)stats->realfm_sum / (double)stats->realfm_counts);
	print_hashstats(stats);
}

static inline void tag_test(uint8_t *data, const char *tag)
{
	if(data)
		return;
	fprintf(stderr, "Required bam tag '%s' not found. Abort mission!\n", tag);
	exit(EXIT_FAILURE);
}


static inline void famstat_loop(famstats_t *s, bam1_t *b, famstat_settings_t *settings)
{
	++s->n_pass;
	uint8_t *data;
	data = bam_aux_get(b, "FM");
	tag_test(data, "FM");
	int FM = bam_aux2i(data);
#if DBG
	fprintf(stderr, "FM tag: %i.\n", FM);
#endif
	if(b->core.flag & 2944 || b->core.qual < settings->minMQ || FM < settings->minFM) {
		++s->n_fail;
		return;
		// Skips supp/second/read2/qc fail/marked duplicate
		// 2944 is equivalent to BAM_FSECONDARY | BAM_FSUPPLEMENTARY | BAM_QCFAIL | BAM_FISREAD2
	}
	data = bam_aux_get(b, "RV");
	if(!data && RVWarn) {
		RVWarn = 0;
		fprintf(stderr, "[famstat_core]: Warning: RV tag not found. Continue.\n");
	}
	int RV = data ? bam_aux2i(data): 0;
#if DBG
	fprintf(stderr, "RV tag: %i.\n", RV);
#endif
	if(FM > 1) {
		++s->realfm_counts; s->realfm_sum += FM; s->realrc_sum += RV;
	}
	++s->allfm_counts; s->allfm_sum += FM; s->allrc_sum += RV;
	s->ki = kh_get(fm, s->fm, FM);
	if(s->ki == kh_end(s->fm)) {
		s->ki = kh_put(fm, s->fm, FM, &s->khr);
		kh_val(s->fm, s->ki) = 1;
	}
	else {
		++kh_val(s->fm, s->ki);
	}
	s->ki = kh_get(rc, s->rc, RV);
	if(s->ki == kh_end(s->rc)) {
		s->ki = kh_put(rc, s->rc, RV, &s->khr);
		kh_val(s->rc, s->ki) = 1;
	}
	else {
		++kh_val(s->rc, s->ki);
	}
}


famstats_t *famstat_core(samFile *fp, bam_hdr_t *h, famstat_settings_t *settings)
{
	uint64_t count = 0;
	famstats_t *s;
	bam1_t *b;
	int ret;
	s = (famstats_t*)calloc(1, sizeof(famstats_t));
	s->fm = kh_init(fm);
	s->rc = kh_init(rc);
	s->data = NULL;
	b = bam_init1();
	while ((ret = sam_read1(fp, h, b)) >= 0) {
		famstat_loop(s, b, settings);
		if(!(++count % notification_interval))
			fprintf(stderr, "[famstat_core] Number of records processed: %"PRIu64".\n", count);
	}
	bam_destroy1(b);
	if (ret != -1)
		fprintf(stderr, "[famstat_core] Truncated file? Continue anyway.\n");
	return s;
}

static void usage_exit(FILE *fp, int exit_status)
{
	fprintf(fp, "Usage: famstat <in.bam>\n");
	fprintf(fp, "Subcommands: \nfm\tFamily Size stats\n"
			"frac\tFraction of raw reads in family sizes >= minFM parameter.\n"
			"target\tFraction of raw reads on target.\n");
	exit(exit_status);
}

static void fm_usage_exit(FILE *fp, int exit_status)
{
	fprintf(fp, "Usage: famstat fm <opts> <in.bam>\n");
	fprintf(fp, "-m Set minimum mapping quality. Default: 0.\n");
	fprintf(fp, "-f Set minimum family size. Default: 0.\n");
	exit(exit_status);
}

int fm_main(int argc, char *argv[])
{
	samFile *fp;
	bam_hdr_t *header;
	famstats_t *s;
	int c;


	famstat_settings_t *settings = (famstat_settings_t *)calloc(1, sizeof(famstat_settings_t));
	settings->minMQ = 0;
	settings->minFM = 0;

	while ((c = getopt(argc, argv, "m:f:n:h")) >= 0) {
		switch (c) {
		case 'm':
			settings->minMQ = atoi(optarg); break;
			break;
		case 'f':
			settings->minFM = atoi(optarg); break;
			break;
		case '?': // Fall-through!
		case 'h':
			fm_usage_exit(stderr, EXIT_SUCCESS);
		default:
			fm_usage_exit(stderr, EXIT_FAILURE);
		}
	}

	if (argc != optind+1) {
		if (argc == optind) fm_usage_exit(stdout, EXIT_SUCCESS);
		else fm_usage_exit(stderr, EXIT_FAILURE);
	}

	fprintf(stderr, "[famstat_main]: Running main with minMQ %i and minFM %i.\n", settings->minMQ, settings->minFM);

	fp = sam_open(argv[optind], "r");
	if (fp == NULL) {
		fprintf(stderr, "[famstat_main]: Cannot open input file \"%s\"", argv[optind]);
		exit(EXIT_FAILURE);
	}

	header = sam_hdr_read(fp);
	if (header == NULL) {
		fprintf(stderr, "[famstat_main]: Failed to read header for \"%s\"\n", argv[optind]);
		exit(EXIT_FAILURE);
	}
	s = famstat_core(fp, header, settings);
	print_stats(s);
	kh_destroy(fm, s->fm);
	kh_destroy(rc, s->rc);
	free(s);
	free(settings);
	bam_hdr_destroy(header);
	sam_close(fp);
	return 0;
}

static inline void frac_loop(bam1_t *b, int minFM, uint64_t *fm_above, uint64_t *fm_total)
{
	uint8_t *data = bam_aux_get(b, "FM");
	tag_test(data, "FM");
	int FM = bam_aux2i(data);
	if(b->core.flag & 2944) {
		return;
		// Skips supp/second/read2/qc fail/marked duplicate
		// 2944 is equivalent to BAM_FSECONDARY | BAM_FSUPPLEMENTARY | BAM_QCFAIL | BAM_FISREAD2
	}
	if(FM >= minFM)
		*fm_above += FM;
	*fm_total += FM;
}


int frac_main(int argc, char *argv[])
{
	samFile *fp;
	bam_hdr_t *header;
	int c;
	uint32_t minFM = 0;

	if(argc < 4) {
		frac_usage_exit(stderr, EXIT_FAILURE);
	}

	while ((c = getopt(argc, argv, "n:m:h?")) >= 0) {
		switch (c) {
		case 'm':
			minFM = (uint32_t)atoi(optarg); break;
			break;
		case '?':
		case 'h':
			frac_usage_exit(stderr, EXIT_SUCCESS);
		default:
			frac_usage_exit(stderr, EXIT_FAILURE);
		}
	}

	if(strcmp(argv[1], "--help") == 0) frac_usage_exit(stderr, EXIT_SUCCESS);

	if(!minFM) {
		fprintf(stderr, "minFM not set. frac_main meaningless without it. Result: 1.0.\n");
		return EXIT_FAILURE;
	}
	fprintf(stderr, "[famstat_frac_main]: Running frac main minFM %i.\n", minFM);

	if (argc != optind+1) {
		if (argc == optind) frac_usage_exit(stdout, EXIT_SUCCESS);
		else frac_usage_exit(stderr, EXIT_FAILURE);
	}
	fp = sam_open(argv[optind], "r");
	if (fp == NULL) {
		fprintf(stderr, "[famstat_frac_main]: Cannot open input file \"%s\"", argv[optind]);
		exit(EXIT_FAILURE);
	}

	header = sam_hdr_read(fp);
	if (header == NULL) {
		fprintf(stderr, "[famstat_main]: Failed to read header for \"%s\"\n", argv[optind]);
		exit(EXIT_FAILURE);
	}
	uint64_t fm_above = 0, total_fm = 0, count = 0;
	bam1_t *b = bam_init1();
	while ((c = sam_read1(fp, header, b)) >= 0) {
		frac_loop(b, minFM, &fm_above, &total_fm);
		if(!(++count % notification_interval))
			fprintf(stderr, "[famstat_frac_core] Number of records processed: %"PRIu64".\n", count);
	}
	fprintf(stderr, "#Fraction of raw reads with >= minFM %i: %f.\n", minFM, (double)fm_above / total_fm);
	bam_destroy1(b);
	bam_hdr_destroy(header);
	sam_close(fp);
	return 0;
}

int main(int argc, char *argv[])
{
	if(argc < 2) {
		usage_exit(stderr, 1);
	}
	if(strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
		usage_exit(stderr, 0);
	}
	if(strcmp(argv[1], "fm") == 0) {
		return fm_main(argc - 1, argv + 1);
	}
	if(strcmp(argv[1], "frac") == 0) {
		return frac_main(argc - 1, argv + 1);
	}
	if(strcmp(argv[1], "target") == 0) {
		return target_main(argc - 1, argv + 1);
	}
	fprintf(stderr, "Unrecognized subcommand. See usage.\n");
	fprintf(stderr, "Unrecognized subcommand. See usage.\n");
	usage_exit(stderr, 1);
}