#include "bmf_stack.h"
namespace {
	const char *vcf_header_lines[] =  {
			"##INFO=<ID=BMF_VET,Number=A,Type=Integer,Description=\"1 if the variant passes vetting, 0 otherwise.\">",
			"##INFO=<ID=BMF_UNIOBS,Number=A,Type=Integer,Description=\"Number of unique observations supporting variant at position.\">",
			"##INFO=<ID=BMF_DUPLEX,Number=A,Type=Integer,Description=\"Number of duplex reads supporting variant at position.\">",
			"##INFO=<ID=BMF_FAIL,Number=A,Type=Integer,Description=\"Number of unique observations at position failing filters.\">",
			"##INFO=<ID=DUPLEX_DEPTH,Number=1,Type=Integer,Description=\"Number of duplex reads passing filters at position.\">",
			"##INFO=<ID=DISC_OVERLAP,Number=1,Type=Integer,Description=\"Number of read pairs at position with discordant base calls.\">",
			"##INFO=<ID=OVERLAP,Number=1,Type=Integer,Description=\"Number of overlapping read pairs combined into single observations at position.\">"
	};
}




void stack_usage(int retcode)
{
	const char *buf =
			"Usage:\nbmftools stack -o <out.vcf [stdout]> <in.srt.indexed.bam>\n"
			"Optional arguments:\n"
			"-b, --bedpath\tPath to bed file to only validate variants in said region. REQUIRED.\n"
			"-c, --min-count\tMinimum number of observations for a given allele passing filters to pass variant. Default: 1.\n"
			"-s, --min-family-size\tMinimum number of reads in a family to include a that collapsed observation\n"
			"-2, --skip-secondary\tSkip secondary alignments.\n"
			"-S, --skip-supplementary\tSkip supplementary alignments.\n"
			"-q, --skip-qcfail\tSkip reads marked as QC fail.\n"
			"-f, --min-fraction-agreed\tMinimum fraction of reads in a family agreed on a base call\n"
			"-v, --min-phred-quality\tMinimum calculated p-value on a base call in phred space\n"
			"-p, --padding\tNumber of bases outside of bed region to pad.\n"
			"-a, --min-family-agreed\tMinimum number of reads in a family agreed on a base call\n"
			"-m, --min-mapping-quality\tMinimum mapping quality for reads for inclusion\n"
			"-B, --emit-bcf-format\tEmit bcf-formatted output. (Defaults to vcf).\n";
	fprintf(stderr, buf);
	exit(retcode);
}


static int read_bam(BamHandle *data, bam1_t *b)
{
	int ret;
	for(;;)
	{
		if(!data->iter) LOG_EXIT("Need to access bam with index.\n");
		ret = sam_itr_next(data->fp, data->iter, b);
		if ( ret<0 ) break;
		// Skip unmapped, secondary, qcfail, duplicates.
		// Skip improper if option set
		// Skip MQ < minMQ
		// Skip FM < minFM
		// Skip AF < minAF
		if (b->core.flag & (BAM_FUNMAP | BAM_FSECONDARY | BAM_FQCFAIL | BAM_FDUP))
			continue;
		/*
		if ((b->core.flag & (BAM_FUNMAP | BAM_FSECONDARY | BAM_FQCFAIL | BAM_FDUP)) ||
			( ||
			(bam_aux2i(bam_aux_get(b, "FP")) == 0) || (data->minAF && bam_aux2f(bam_aux_get(b, "AF")) < data->minAF))
				continue;
		*/
		break;
	}
	return ret;
}

void process_matched_pileups(stack_aux_t *aux, bcf1_t *ret,
						int tn_plp, int tpos, int ttid,
						int nn_plp, int npos, int ntid) {
	char *qname;
	// Build overlap hash
	std::unordered_map<char *, BMF::UniqueObservation> tobs, nobs;
	std::unordered_map<char *, BMF::UniqueObservation>::iterator found;
	int flag_failed[2] = {0};
	int af_failed[2] = {0};
	int fa_failed[2] = {0};
	int fm_failed[2] = {0};
	int fp_failed[2] = {0};
	int fr_failed[2] = {0};
	int mq_failed[2] = {0};
	int improper_count[2] = {0};
	// Capturing found  by reference to avoid making unneeded temporary variables.
	std::for_each(aux->tumor->pileups, aux->tumor->pileups + tn_plp, [&](const bam_pileup1_t& plp) {
		if(plp.is_del || plp.is_refskip) return;
		if(aux->conf.skip_flag & plp.b->core.flag) {
			++flag_failed[0];
			return;
		}
		if((plp.b->core.flag & BAM_FPROPER_PAIR) == 0) {
			++improper_count[0];
			if(aux->conf.skip_improper) return;
		}
		// If a read's mate is here with a sufficient mapping quality, we should keep it, shouldn't we? Put this later.
		if(plp.b->core.qual < aux->conf.minMQ) {
			++mq_failed[0]; return;
		}
		const int FM = bam_aux2i(bam_aux_get(plp.b, "FM"));
		if(FM < aux->conf.minFM) {
			++fm_failed[0]; return;
		}
		if(bam_aux2i(bam_aux_get(plp.b, "FP")) == 0) {
			++fp_failed[0]; return;
		}
		if(bam_aux2f(bam_aux_get(plp.b, "AF")) < aux->conf.minAF) {
			++af_failed[0]; return;
		}
		const int qpos = arr_qpos(&plp);
		const uint32_t FA = ((uint32_t *)array_tag(plp.b, "FA"))[qpos];
		if(FA < aux->conf.minFA) {
			++fa_failed[0]; return;
		}
		if((float)FA / FM < aux->conf.minFR) {
			++fr_failed[0]; return;
		}
		// Should I be failing FA/FM/PV before merging overlapping reads? NO.
		qname = bam_get_qname(plp.b);
		if((found = tobs.find(qname)) == tobs.end())
			tobs[qname] = BMF::UniqueObservation(plp);
		else found->second.add_obs(plp);
	});
	std::for_each(aux->normal->pileups, aux->normal->pileups + tn_plp, [&](const bam_pileup1_t& plp) {
		if(plp.is_del || plp.is_refskip) return;
		if(aux->conf.skip_flag & plp.b->core.flag) {
			++flag_failed[1];
			return;
		}
		if((plp.b->core.flag & BAM_FPROPER_PAIR) == 0) {
			++improper_count[1];
			if(aux->conf.skip_improper) return;
		}
		// If a read's mate is here with a sufficient mapping quality, we should keep it, shouldn't we? Put this later.
		if(plp.b->core.qual < aux->conf.minMQ) {
			++mq_failed[1]; return;
		}
		const int FM = bam_aux2i(bam_aux_get(plp.b, "FM"));
		if(FM < aux->conf.minFM) {
			++fm_failed[1]; return;
		}
		if(bam_aux2i(bam_aux_get(plp.b, "FP")) == 0) {
			++fp_failed[1]; return;
		}
		if(bam_aux2f(bam_aux_get(plp.b, "AF")) < aux->conf.minAF) {
			++af_failed[1]; return;
		}
		const int qpos = arr_qpos(&plp);
		const uint32_t FA = ((uint32_t *)array_tag(plp.b, "FA"))[qpos];
		if(FA < aux->conf.minFA) {
			++fa_failed[1]; return;
		}
		if((float)FA / FM < aux->conf.minFR) {
			++fr_failed[1]; return;
		}
		// Should I be failing FA/FM/PV before merging overlapping reads? NO.
		qname = bam_get_qname(plp.b);
		if((found = nobs.find(qname)) == nobs.end())
			nobs[qname] = BMF::UniqueObservation(plp);
		else found->second.add_obs(plp);
	});
	// Build vcfline struct
	BMF::PairVCFPos vcfline = BMF::PairVCFPos(tobs, nobs, ttid, tpos);
	vcfline.to_bcf(ret, aux->vh, aux->get_ref_base(ttid, tpos));
	bcf_update_format_int32(aux->vh, ret, "FR_FAILED", (void *)&fr_failed, 2);
	bcf_update_format_int32(aux->vh, ret, "FA_FAILED", (void *)&fa_failed, 2);
	bcf_update_format_int32(aux->vh, ret, "FP_FAILED", (void *)&fp_failed, 2);
	bcf_update_format_int32(aux->vh, ret, "FM_FAILED", (void *)&fm_failed, 2);
	bcf_update_format_int32(aux->vh, ret, "MQ_FAILED", (void *)&mq_failed, 2);
	bcf_update_format_int32(aux->vh, ret, "AF_FAILED", (void *)&af_failed, 2);
	bcf_update_format_int32(aux->vh, ret, "IMPROPER", (void *)&improper_count, 2);
	bcf_write(aux->ofp, aux->vh, ret);
	bcf_clear(ret);
}

void process_pileup(bcf1_t *ret, const bam_pileup1_t *plp, int n_plp, int pos, int tid, stack_aux_t *aux) {
	char *qname;
	// Build overlap hash
	std::unordered_map<char *, BMF::UniqueObservation> obs;
	std::unordered_map<char *, BMF::UniqueObservation>::iterator found;
	int flag_failed = 0;
	int af_failed = 0;
	int fa_failed = 0;
	int fm_failed = 0;
	int fp_failed = 0;
	int fr_failed = 0;
	int mq_failed = 0;
	int improper_count = 0;
	// Capturing found  by reference to avoid making unneeded temporary variables.
	std::for_each(plp, plp + n_plp, [&](const bam_pileup1_t& plp) {
		if(plp.is_del || plp.is_refskip) return;
		if(aux->conf.skip_flag & plp.b->core.flag) {
			++flag_failed;
			return;
		}
		if((plp.b->core.flag & BAM_FPROPER_PAIR) == 0) {
			++improper_count;
			if(aux->conf.skip_improper) return;
		}
		// If a read's mate is here with a sufficient mapping quality, we should keep it, shouldn't we? Put this later.
		if(plp.b->core.qual < aux->conf.minMQ) {
			++mq_failed; return;
		}
		const int FM = bam_aux2i(bam_aux_get(plp.b, "FM"));
		if(FM < aux->conf.minFM) {
			++fm_failed; return;
		}
		if(bam_aux2i(bam_aux_get(plp.b, "FP")) == 0) {
			++fp_failed; return;
		}
		if(bam_aux2f(bam_aux_get(plp.b, "AF")) < aux->conf.minAF) {
			++af_failed; return;
		}
		const int qpos = arr_qpos(&plp);
		const uint32_t FA = ((uint32_t *)array_tag(plp.b, "FA"))[qpos];
		if(FA < aux->conf.minFA) {
			++fa_failed; return;
		}
		if((float)FA / FM < aux->conf.minFR) {
			++fr_failed; return;
		}
		// Should I be failing FA/FM/PV before merging overlapping reads? NO.
		qname = bam_get_qname(plp.b);
		if((found = obs.find(qname)) == obs.end())
			obs[qname] = BMF::UniqueObservation(plp);
		else found->second.add_obs(plp);
	});
	// Build vcfline struct
	BMF::SampleVCFPos vcfline = BMF::SampleVCFPos(obs, tid, pos);
	vcfline.to_bcf(ret, aux->vh, aux->get_ref_base(tid, pos));
	bcf_update_info_int32(aux->vh, ret, "FR_FAILED", (void *)&fr_failed, 1);
	bcf_update_info_int32(aux->vh, ret, "FA_FAILED", (void *)&fa_failed, 1);
	bcf_update_info_int32(aux->vh, ret, "FP_FAILED", (void *)&fp_failed, 1);
	bcf_update_info_int32(aux->vh, ret, "FM_FAILED", (void *)&fm_failed, 1);
	bcf_update_info_int32(aux->vh, ret, "MQ_FAILED", (void *)&mq_failed, 1);
	bcf_update_info_int32(aux->vh, ret, "AF_FAILED", (void *)&af_failed, 1);
	bcf_update_info_int32(aux->vh, ret, "IMPROPER", (void *)&improper_count, 1);
	bcf_write(aux->ofp, aux->vh, ret);
	bcf_clear(ret);
}

int stack_core(stack_aux_t *aux)
{
	aux->tumor->plp = bam_plp_init((bam_plp_auto_f)read_bam, (void *)aux->tumor);
	aux->normal->plp = bam_plp_init((bam_plp_auto_f)read_bam, (void *)aux->normal);
	bam_plp_set_maxcnt(aux->tumor->plp, aux->conf.max_depth);
	bam_plp_set_maxcnt(aux->normal->plp, aux->conf.max_depth);
	std::vector<khiter_t> sorted_keys = make_sorted_keys(aux->bed);
	int ttid, tpos, tn_plp, ntid, npos, nn_plp;
	bcf1_t *v = bcf_init1();
	for(auto& key: sorted_keys) {
		for(uint64_t i = 0; i < kh_val(aux->bed, key).n; ++i) {
			if(aux->tumor->iter) hts_itr_destroy(aux->tumor->iter);
			if(aux->normal->iter) hts_itr_destroy(aux->normal->iter);
			const int start = get_start(kh_val(aux->bed, key).intervals[i]);
			const int stop = get_stop(kh_val(aux->bed, key).intervals[i]);
			const int bamtid = (int)kh_key(aux->bed, key);
			aux->tumor->iter = bam_itr_queryi(aux->tumor->idx, kh_key(aux->bed, key), start, stop);
			aux->normal->iter = bam_itr_queryi(aux->normal->idx, kh_key(aux->bed, key), start, stop);
			while((aux->tumor->pileups = bam_plp_auto(aux->tumor->plp, &ttid, &tpos, &tn_plp)) >= 0) {
				if(tpos < start && ttid == bamtid) continue;
				if(tpos >= stop) break;
			}
			while((aux->normal->pileups = bam_plp_auto(aux->normal->plp, &ntid, &npos, &nn_plp)) >= 0) {
				if(npos < start && ntid == bamtid) continue;
				if(npos >= stop) break;
			}
			// Both bams should be at the same position now.
			assert(npos == tpos && ntid == ttid);
		}
	}
	bcf_destroy(v);
	return 0;
}

int stack_main(int argc, char *argv[]) {
	int c;
	unsigned padding = (unsigned)-1;
	if(argc < 2) stack_usage(EXIT_FAILURE);
	char *outvcf = NULL, *refpath = NULL;
	std::string bedpath = "";
	int output_bcf = 0;
	struct stack_conf conf = {0};
	while ((c = getopt(argc, argv, "R:D:q:r:2:S:d:a:s:m:p:f:b:v:o:O:c:BP?hV")) >= 0) {
		switch (c) {
		case 'B': output_bcf = 1; break;
		case 'a': conf.minFA = atoi(optarg); break;
		case 'c': conf.minCount = atoi(optarg); break;
		case 'D': conf.minDuplex = atoi(optarg); break;
		case 's': conf.minFM = atoi(optarg); break;
		case 'm': conf.minMQ = atoi(optarg); break;
		case 'v': conf.minPV = atoi(optarg); break;
		case '2': conf.skip_flag |= BAM_FSECONDARY; break;
		case 'S': conf.skip_flag |= BAM_FSUPPLEMENTARY; break;
		case 'q': conf.skip_flag |= BAM_FQCFAIL; break;
		case 'r': conf.skip_flag |= BAM_FDUP; break;
		case 'R': refpath = optarg; break;
		case 'P': conf.skip_improper = 1; break;
		case 'p': padding = atoi(optarg); break;
		case 'd': conf.max_depth = atoi(optarg); break;
		case 'f': conf.minFR = (float)atof(optarg); break;
		case 'b': bedpath = optarg; break;
		case 'o': outvcf = optarg; break;
		case 'O': conf.minOverlap = atoi(optarg); break;
		//case 'V': aux.vet_all = 1; break;
		case 'h': case '?': stack_usage(EXIT_SUCCESS);
		}
	}
	if(optind >= argc - 1) LOG_EXIT("Insufficient arguments. Input bam required!\n");
	if(padding < 0) {
		LOG_WARNING("Padding not set. Using default %i.\n", DEFAULT_PADDING);
		padding = DEFAULT_PADDING;
	}
	if(!refpath) {
		LOG_EXIT("refpath required. Abort!\n");
	}
	stack_aux_t aux = stack_aux_t(argv[optind], argv[optind + 1], conf);
	aux.fai = fai_load(refpath);
	if(!aux.fai) LOG_EXIT("failed to open fai. Abort!\n");
	// TODO: Make BCF header
	aux.vh = bcf_hdr_init(output_bcf ? "wb": "w");
	aux.bed = *bedpath.c_str() ? parse_bed_hash(bedpath.c_str(), aux.normal->header, padding): build_ref_hash(aux.normal->header);
	for(auto line: vcf_header_lines)
		if(bcf_hdr_append(aux.vh, line))
			LOG_EXIT("Could not add line %s to header. Abort!\n", line);
	// Add lines to the header for the bed file?
	if(!outvcf)
		outvcf = (char *)"-";
	aux.ofp = vcf_open(outvcf, output_bcf ? "wb": "w");
	bcf_hdr_write(aux.ofp, aux.vh);
	return stack_core(&aux);
}
