from Bio import SeqIO
import pysam
from BarcodeUtils import *

def AdapterLoc(fq,adapter,bar_len=12,keepFailed=True):
    InFastq=SeqIO.parse(fq,"fastq")
    Tpref = '.'.join(fq.split('.')[0:-1])
    Prefix=Tpref.split('/')[-1]
    StdFilename, ElseFilename, ElseLoc = Prefix+'.{}.fastq'.format("adapter"+str(bar_len)), Prefix+ '.else.fastq', Prefix + '.else.supp'
    StdFastq=open(StdFilename,'w',0) #Adapter at expected Location
    ElseFastq=open(ElseFilename,'w',0) #Adapter sequence found elsewhere, even if it's simply part of the read itself
    ElseLocations=open(ElseLoc,'w',0)
    for read in InFastq:
        seq = str(read.seq)
        if(seq.find(adapter)==-1):
            if(keepFailed==False):
                raise NameError("Sanity check failure. Adapter Sequence Not Found. HTML 404")
            else:
                read.description+=" ###AdapterFail"
                SeqIO.write(read,StdFastq,"fastq")
        elif(seq[bar_len:bar_len+len(adapter)] == adapter):
            read.description+=" ###AdapterPass"
            SeqIO.write(read,StdFastq,"fastq") #Checks the expected adapter location. Avoiding find command, as sometimes the adapter sequence occurs before and at the expected location
        else:
            if(keepFailed==False):
                SeqIO.write(read,ElseFastq,"fastq")
                ElseLocations.write(repr(seq.find(adapter)) + "\t" + read.name + "\n")
            else:
                read.description=" ###AdapterFail"
                SeqIO.write(read,StdFastq,"fastq")
                ElseLocations.write(repr(seq.find(adapter)) + "\t" + read.name + "\n")
    StdFastq.close()
    ElseFastq.close()
    ElseLocations.close()
    return StdFilename,ElseFilename

def fastq_sort(in_fastq,out_fastq):
    import subprocess
    outfile=open(out_fastq,'w')
    command_str='cat {} | paste - - - - | sort -k1,1 -t " " | tr "\t" "\n"'.format(in_fastq)
    subprocess.call(command_str,stdout=outfile,shell=True)
    outfile.close()
    return(command_str)

def FastqRegex(fq,string,matchFile="default",missFile="default"):
    from subprocess import call
    if(matchFile=="default"):
        matchFile = ('.'.join(fq.split('.')[0:-1])+'.match.fastq').split('/')[-1]
    if(missFile=="default"):
        missFile = ('.'.join(fq.split('.')[0:-1])+'.miss.fastq').split('/')[-1]
    CommandStr = "cat {} | paste - - - - | grep '{}' | tr '\t' '\n' > {}".format(fq,string,matchFile)
    call(CommandStr,shell=True)
    CommandStr2 = "cat {} | paste - - - - | grep -v '{}' | tr '\t' '\n' > {}".format(fq,string,missFile)
    call(CommandStr2,shell=True)
    return(CommandStr,CommandStr2,matchFile,missFile)

def fastx_trim(infq, outfq, n):
    import subprocess
    command_str = ['fastx_trimmer','-l',str(n),'-i',infq,'-o',outfq]
    print(command_str)
    subprocess.call(command_str)
    return(command_str)

def GenerateSingleBarcodeIndex(tags_file,index_file="default"):
    from subprocess import call
    if(index_file=="default"):
        index_file = '.'.join(tags_file.split('.')[0:-1]) + ".barIdx"
    call("cat {} | sed 's:###: ###:g' | grep -v \"AdapterFail\" | paste - - - - | awk 'BEGIN {{FS=\"\t\";OFS=\"\t\"}};{{print $2}}' | sort | uniq -c | awk 'BEGIN {{OFS=\"\t\"}};{{print $1,$2}}' > {}".format(tags_file,index_file),shell=True)
    return index_file


def GetFamilySizeSingle(trimfq,BarcodeIndex,outfq="default",singlefq="default",keepFailed=True):
    infq = SeqIO.parse(trimfq, "fastq")
    if(outfq=="default"):
        outfq = '.'.join(trimfq.split('.')[0:-1])+".fam.fastq"
    outfqBuffers = open(outfq,"w",0)
    index = open(BarcodeIndex,"r")
    if(singlefq=="default"):
        singlefq = '.'.join(trimfq.split('.')[0:-1])+".lonely.hearts.club.band.fastq"
    singlefqBuffer = open(singlefq,"w",0)
    TotalReads, ReadsWithFamilies = 0,0
    dictEntries = [line.split() for line in index]
    BarDict = {}
    for entry in dictEntries:
        BarDict[entry[1]]=entry[0]
    for read in infq:
        index.seek(0)
        TotalReads+=1
        #print("description is {}".format(read.description)) #FOR DEBUGGING PURPOSES, UNCOMMENT
        #print("-1 index of description is {}".format(read.description.split("###")[-1].strip()))
        #print("-2 index of description is {}".format(read.description.split("###")[-2].strip()))
        readTag = read.description.split("###")[-1].strip()
        newRead = read
        #print("readTag is _" + readTag + "_")
        famSize = BarDict[readTag]
        newRead.description = read.description+" ###"+famSize
        #print("famSize = _{}_".format(str(famSize)))
        #print("The value of this comparison to 1 is {}".format(str(famSize=="1")))
        if(famSize == "1"):
            SeqIO.write(newRead,singlefqBuffer,"fastq")
            if(keepFailed==True):
                SeqIO.write(newRead,outfqBuffers,"fastq")
        else:
            ReadsWithFamilies+=1
            SeqIO.write(newRead,outfqBuffers,"fastq")
    return outfq,TotalReads, ReadsWithFamilies

def mergeSequencesFastq(fq1,fq2,output="default"):
    if(output=="default"):
        output = fq1.split('.')[0] + '.merged.fastq'
    from Bio.SeqRecord import SeqRecord
    from Bio.Seq import Seq
    reads1=SeqIO.parse(fq1,"fastq")
    reads2=SeqIO.parse(fq2,"fastq")
    outFastq = open(output,"w",0)
    while True:
        try:
            read1 = reads1.next()
            read2 = reads2.next()
            outread = read1
            outread=SeqRecord(
                Seq(str(read1.seq)+str(read2.seq),"fastq"), id=read1.id) 
            outread.letter_annotations['phred_quality']=read1.letter_annotations['phred_quality'] + read2.letter_annotations['phred_quality'] 
            SeqIO.write(outread, outFastq, "fastq")
        except StopIteration:
            break
    reads1.close()
    reads2.close()
    outFastq.close()
    return output

def reverseComplement(fq,dest="default"):
    if(dest=="default"):
        temp = '.'.join(fq.split('.')[0:-1])+"_rc.fastq"
        dest = temp.split('/')[-1]
    InFastq = SeqIO.parse(fq,"fastq")
    OutFastq = open(dest,"w",0)
    for record in InFastq:
        rc_record = record.reverse_complement(id=record.id+"_rc")
        SeqIO.write(rc_record,OutFastq,"fastq")
    OutFastq.close()
    return dest

def TrimAdapter(fq,adapter,trimfq="default",bar_len=12,tags_file="default",trim_err="default",start_trim=1,keepFailed=True):
    from Bio.SeqRecord import SeqRecord
    from Bio.Seq import Seq
    if(trim_err=="default"):
        temp = '.'.join(fq.split('.')[0:-1]) + '.err.fastq'
        trim_err = temp.split('/')[-1]
    if(trimfq == "default"):
        temp = '.'.join(fq.split('.')[0:-1]) + '.trim.fastq'
        trimfq = temp.split('/')[-1]
    if(tags_file == "default"):
        temp = '.'.join(fq.split('.')[0:-1]) + '.tags.fastq'
        tags_file = temp.split('/')[-1]
    tagsOpen = open(tags_file,"w",0)
    trimOpen = open(trimfq,"w",0)
    InFastq = SeqIO.parse(fq,"fastq")
    AdaptLen=len(adapter)
    TotalTrim=AdaptLen+bar_len+start_trim
    print("Adapter Length is {}".format(AdaptLen))
    for record in InFastq:
        pre_tag = SeqRecord(
                Seq(str(record.seq)[0:bar_len],"fastq"), \
                id=record.id)
        pre_tag.letter_annotations['phred_quality']=record.letter_annotations['phred_quality'][0:bar_len]
        '''
        if adapter not in pre_tag.seq:
            print("I'm sorry, but your adapter sequence is not in the tag. I will write this to an error fastq, which you are free to use or discard at your discretion")
            SeqIO.write(record,errOpen,"fastq")
            continue
        '''
        SeqIO.write(pre_tag,tagsOpen,"fastq")
        post_tag = SeqRecord(
                Seq(str(record.seq)[TotalTrim:],"fastq"), id=record.id,description=record.description+" ###" + str(record.seq[0:bar_len])) 
        post_tag.letter_annotations['phred_quality']=record.letter_annotations['phred_quality'][TotalTrim:]
        SeqIO.write(post_tag,trimOpen,"fastq")
    tagsOpen.close()
    trimOpen.close()
    return(tags_file,trimfq)
