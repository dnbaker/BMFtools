#!/usr/bin/env python
import argparse
import os.path
import logging
import sys

import BMFMain.ProcessingSteps as ps
from utilBMF.HTSUtils import printlog as pl, FacePalm
from utilBMF import HTSUtils
import pudb

"""
Contains utilities for the completion of a variety of
tasks related to barcoded protocols for ultra-low
frequency variant detection, particularly for circulating tumor DNA
Structural Variant detection tools are in active development.
"""

# Global Variables
Logger = logging.getLogger("Primarylogger")


def main():
    # pudb.set_trace()
    parser = argparse.ArgumentParser()
    parser.add_argument(
        'fq',
        help="Provide your fastq file(s).",
        nargs="+",
        metavar=('reads'))
    parser.add_argument(
        "--idxFastq",
        "-i",
        help="Path to index fastq",
        metavar="indexFastq")
    parser.add_argument(
        '--conf',
        help="Path to config file with settings.",
        metavar="ConfigPath",
        default="/yggdrasil/workspace/BMFTools/demo/config.txt")
    parser.add_argument(
        '-s',
        '--single-end',
        action="store_true",
        help="Whether the experiment is single-end or not. Default: False",
        default=False)
    parser.add_argument(
        '--homing',
        help="Homing sequence for samples. If not set, defaults to GACGG.",
        metavar=('HomingSequence'),
        default="GACGG")
    parser.add_argument(
        '--shades',
        help="Use flag if using the shades barcode method.",
        default=True,
        action="store_true")
    parser.add_argument(
        '-a',
        '--aligner',
        help="Provide your aligner. Default: bwa",
        nargs='?',
        metavar='aligner',
        default='bwa')
    parser.add_argument(
        '-o',
        '--opts',
        help="Additional aligner opts. E.g.: --opts '-L 0' ",
        nargs='?',
        default='')
    parser.add_argument(
        '-b',
        '--BAM',
        help="BAM file, if alignment has already run.",
        default="default")
    parser.add_argument(
        '--bed',
        help="full path to bed file used for variant-calling steps."
             "Can be indicated by the config file.",
        metavar="BEDFile",
        default="default")
    parser.add_argument(
        '--initialStep',
        help="1: Fastq. 2: Bam. 3. VCF",
        default=1,
        type=int)
    parser.add_argument(
        '-l',
        '--logfile',
        help="To change default logfile location.",
        default="default")
    parser.add_argument(
        '-p',
        '--file-prefix',
        help="Set non-default prefix.",
        default="default")
    parser.add_argument(
        '--minMQ',
        help="Minimum mapping quality for variant call inclusion. "
             "Can be indicated by the config file.",
        default=20)
    parser.add_argument(
        '--minBQ',
        help="Minimum base quality for variant call inclusion. "
             "Can be indicated by the config file.",
        default=90)
    parser.add_argument(
        "--minCov",
        help="Minimum coverage for including a position"
        " in the BamToCoverageBed",
        default=5
        )
    parser.add_argument(
        "--ref",
        "-r",
        default="default",
        help="Path to reference index. Can be indicated by the config file.")
    parser.add_argument(
        "--abrapath",
        default="default",
        help="Path to abra jar. Can be indicated by the config file.")
    parser.add_argument(
        "--barcodeIndex",
        help="If starting with the BAM step, provide the "
             "path to your barcode index as created.")
    global Logger
    args = parser.parse_args()
    confDict = HTSUtils.parseConfig(args.conf)
    if("minMQ" in confDict.keys()):
        minMQ = int(confDict['minMQ'])
    else:
        minMQ = args.minMQ
    if("minBQ" in confDict.keys()):
        minBQ = int(confDict['minBQ'])
    else:
        minBQ = args.minBQ
    if("abrapath" in confDict.keys()):
        abrapath = confDict['abrapath']
    else:
        abrapath = args.abrapath
    # Begin logging
    if(args.logfile != "default"):
        logfile = args.logfile
    else:
        print("Basename for log: {}".format(os.path.basename(args.fq[0])))
        logfile = (os.getcwd() + "/" +
                   os.path.basename(args.fq[0]).split('.')[0] +
                   '.log')
    if(args.file_prefix != "default"):
        logfile = os.getcwd() + "/" + args.file_prefix + ".log"
    if(os.path.isfile(logfile)):
        os.remove(logfile)
        pl("Log file existed - deleting!")

    # Logger which holds both console and file loggers
    Logger.setLevel(logging.DEBUG)

    # Console handler - outputs to console.
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)

    # create formatter
    formatter = logging.Formatter(
        '%(asctime)s - %(name)s - %(levelname)s - %(message)s')

    # add formatter to ch
    ch.setFormatter(formatter)

    # add ch to logger
    Logger.addHandler(ch)

    # File logger - outputs to log file.
    fl = logging.FileHandler(filename=logfile)
    fl.setFormatter(formatter)
    fl.setLevel(logging.DEBUG)

    Logger.addHandler(fl)

    pl("Log file is {}".format(logfile))
    pl("Command string to call BMFTools: python {}".format(" ".join(sys.argv)))
    pl("Note: You may need to add quotes for more complicated options.")
    if("aligner" in confDict.keys()):
        aligner = confDict['aligner']
    else:
        aligner = args.aligner
    homing = args.homing
    if("ref" in confDict.keys()):
        ref = confDict['ref']
    else:
        if(args.ref != "default"):
            ref = args.ref
        else:
            HTSUtils.FacePalm("Reference fasta required either in "
                              "config file or by command-line options.!")
    opts = args.opts
    if("opts" in confDict.keys()):
        opts = args.opts
    if("bed" in confDict.keys()):
        bed = confDict['bed']
    else:
        if(args.bed != "default"):
            bed = args.bed
        else:
            FacePalm("Bed file required for analysis.")
    if(args.single_end is False):
        pl("Paired-end analysis chosen.")
    if(args.single_end is True):
        pl("Single-end analysis chosen.")
        HTSUtils.ThisIsMadness("Single-end analysis not currently "
                               "supported. Soon!")
    else:
        if(args.initialStep == 1):
            pl("Beginning fastq processing.")
            if(args.shades is True):
                trimfq1, trimfq2, barcodeIndex = ps.pairedFastqShades(
                    args.fq[0], args.fq[1], indexfq=args.idxFastq)
                procSortedBam = ps.pairedBamProc(
                    trimfq1,
                    trimfq2,
                    aligner=aligner,
                    ref=ref,
                    barIndex=barcodeIndex,
                    bed=bed,
                    mincov=int(args.minCov),
                    abrapath=abrapath)
            CleanParsedVCF = ps.pairedVCFProc(
                procSortedBam,
                ref=ref,
                opts=opts,
                bed=bed,
                minMQ=minMQ,
                minBQ=minBQ,
                commandStr=" ".join(sys.argv))
            pl(
                "Last stop! Watch your step.")
        elif(args.initialStep == 2):
            pl("Beginning BAM processing.")
            procSortedBam = ps.pairedBamProc(
                trimfq1,
                trimfq2,
                consfqSingle="default",
                aligner=aligner,
                ref=ref,
                abrapath=abrapath,
                mincov=int(args.minCov),
                barIndex=args.barcodeIndex)
            pl("Beginning VCF processing.")
            CleanParsedVCF = ps.pairedVCFProc(
                procSortedBam,
                ref=ref,
                opts=opts,
                bed=bed,
                minMQ=minMQ,
                minBQ=minBQ,
                commandStr=" ".join(sys.argv))
            pl(
                "Last stop! Watch your step")
        elif(args.initialStep == 3):
            pl("Beginning VCF processing.")
            CleanParsedVCF = ps.pairedVCFProc(
                args.BAM,
                ref=ref,
                opts=opts,
                bed=bed,
                minMQ=args.minMQ,
                minBQ=args.minBQ,
                reference=args.ref,
                commandStr=" ".join(sys.argv))
            pl("Last stop! Watch your step.")
        return

__version__ = "0.4.0"

if(__name__ == "__main__"):
    main()
