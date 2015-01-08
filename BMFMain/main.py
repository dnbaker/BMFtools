#!/usr/bin/env python
import argparse
import os.path
import logging
import sys

import BMFMain.ProcessingSteps as ps
from utilBMF.HTSUtils import printlog as pl
"""
Contains utilities for the completion of a variety of
tasks related to barcoded protocols for ultra-low
frequency variant detection, particularly for circulating tumor DNA
Structural Variant detection tools are in active development.
"""

# Global Variables
Logger = logging.getLogger("Primarylogger")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '-i',
        '--fq',
        help="Provide your fastq file(s).",
        nargs="+",
        metavar=('reads'))
    parser.add_argument(
        '--conf',
        help="Path to config file with settings.",
        metavar="ConfigPath",
        default="/yggdrasil/workspace/BMFTools/demo/SampleJson.json")
    parser.add_argument(
        '-r',
        '--ref',
        help="Prefix for the reference index. Required.",
        default="default")
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
        '-s',
        '--single-end',
        action="store_true",
        help="Whether the experiment is paired-end or not. Default: True",
        default=False)
    parser.add_argument(
        '-a',
        '--aligner',
        help="Provide your aligner. Default: bwa",
        nargs='?',
        metavar='aligner',
        default='bwa')
    parser.add_argument(
        "--abrapath",
        help="Path to ABRA jar",
        default="/mounts/bin/abra-0.86-SNAPSHOT-jar-with-dependencies.jar")
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
        help="full path to bed file used for variant-calling steps.",
        metavar="BEDFile",
        required=True)
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
        help="Minimum mapping quality for variant call inclusion.",
        default=20)
    parser.add_argument(
        '--minBQ',
        help="Minimum base quality for variant call inclusion.",
        default=30)
    parser.add_argument(
        "--minCov",
        help="Minimum coverage for including a position"
        " in the BamToCoverageBed",
        default=5
        )
    global Logger
    args = parser.parse_args()
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
    aligner, homing = args.aligner, args.homing
    if(args.ref != "default"):
        ref = args.ref
    opts, bed = args.opts, args.bed

    '''
    config = HTSUtils.Configurations(args.conf)
    confDict = {}
    if(args.conf != "default"):
        confLines = [line.strip().split("=") for line in open(
                     args.conf, "r").readlines() if line[0] != "#"]
        for line in confLines:
            confDict[line[0]] = line[1]
        try:
            ref = confDict["ref"]
        except KeyError:
            ref = args.ref
            pl("Config file does not contain a ref key.")
            if(ref == "default"):
                HTSUtils.FacePalm("Reference required either "
                                  "as command line option or config file.")
    '''
    if(args.single_end is True):
        pl("Single-end analysis chosen.")
    else:
        pl("Paired-end analysis chosen.")
    if(args.single_end is True):
        if(args.initialStep == 1):
            pl("Beginning fastq processing.")
            consFq = ps.singleFastqProc(args.fq[0], homing=homing)
            pl("Beginning BAM processing.")
            TaggedBam = ps.singleBamProc(
                consFq,
                ref,
                opts,
                aligner=aligner)
            pl("Beginning VCF processing.")
            CleanParsedVCF = ps.singleVCFProc(TaggedBam, bed, ref)
            pl("Last stop! Watch your step.")
            return
        elif(args.initialStep == 2):
            consFq = args.fq[0]
            pl("Beginning BAM processing.")
            TaggedBam = ps.singleBamProc(
                consFq,
                ref,
                opts,
                aligner=aligner)
            pl("Beginning VCF processing.")
            CleanParsedVCF = ps.singleVCFProc(TaggedBam, bed, ref)
            pl("Last stop! Watch your step.")
            return
        elif(args.initialStep == 3):
            ConsensusBam = args.BAM
            pl("Beginning VCF processing.")
            CleanParsedVCF = ps.singleVCFProc(
                ConsensusBam,
                bed,
                ref)
            pl("Last stop! Watch your step.")
            return
        else:
            raise ValueError("You have chosen an illegal initial step.")
    else:
        if(args.initialStep == 1):
            pl("Beginning fastq processing.")
            if(args.shades is True):
                trimfq1, trimfq2, barcodeIndex = ps.pairedFastqShades(
                    args.fq[0], args.fq[1], args.fq[2])
                procSortedBam = ps.pairedBamProc(
                    trimfq1,
                    trimfq2,
                    aligner=aligner,
                    ref=ref,
                    barIndex=barcodeIndex,
                    bed=bed,
                    mincov=int(args.minCov),
                    abrapath=args.abrapath)
            CleanParsedVCF = ps.pairedVCFProc(
                procSortedBam,
                ref=ref,
                opts=opts,
                bed=bed,
                minMQ=args.minMQ,
                minBQ=args.minBQ,
                reference=args.ref,
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
                abrapath=args.abrapath,
                mincov=int(args.minCov))
            pl("Beginning VCF processing.")
            CleanParsedVCF = ps.pairedVCFProc(
                procSortedBam,
                ref=ref,
                opts=opts,
                bed=bed,
                minMQ=args.minMQ,
                minBQ=args.minBQ,
                reference=args.ref,
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
