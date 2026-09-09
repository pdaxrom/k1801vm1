/* Byte double operands: existing DCJ11 callbacks distinguish byte/word bus beats. */
#include "trace_oracle.h"
int main(int argc,char **argv) {
    const word ops[]={0110000,0120000,0130000,0140000,0150000};
    unsigned nops=argc>1?(unsigned)atoi(argv[1]):5;
    if(nops!=1 && nops!=5)return 2;
    const word edges[]={0,1,2,0x7e,0x7f,0x80,0x81,0xfe,0xff};
    word r[8],p[4][2];
    for(unsigned op=0;op<nops;op++)for(unsigned sm=0;sm<8;sm++)for(unsigned dm=0;dm<8;dm++)
    for(unsigned sr=0;sr<8;sr++)for(unsigned dr=0;dr<8;dr++) {
        for(unsigned k=0;k<7;k++)r[k]=(word)(0x2000+k*0x100);
        r[7]=0x1000;
        fixture((word)(ops[op]|(sm<<9)|(sr<<6)|(dm<<3)|dr),(sm+dm+sr+dr)&15,r,0,p);
    }
    // Full operand edge cross product, arbitrary register high byte; odd/even
    // memory operands, immediate bytes, and register writeback sign/merge.
    for(unsigned op=0;op<nops;op++)for(unsigned kind=0;kind<4;kind++)
    for(unsigned a=0;a<9;a++)for(unsigned b=0;b<9;b++)for(unsigned c=0;c<2;c++) {
        for(unsigned k=0;k<7;k++)r[k]=(word)(0x2000+k*0x100);
        r[7]=0x1000;r[0]=0xa500|edges[a];r[1]=kind==0?(0x5a00|edges[b]):kind==1?0x4000:0x4001;
        p[0][0]=kind==3?0x1002:0x2000;p[0][1]=(word)(0x3500|edges[a]);
        p[1][0]=0x4000;p[1][1]=kind>=2?(word)((edges[b]<<8)|0x69):(word)(0x6900|edges[b]);
        unsigned source=kind==0?0:kind==3?027:010,destination=kind==0?1:011;
        fixture((word)(ops[op]|(source<<6)|destination),c,r,2,p);
    }
    // Odd direct source/destination lanes including 16-bit data wrap. Each
    // addressing mode still uses the actual DCJ11 pointer/extension behavior.
    for(unsigned op=0;op<nops;op++)for(unsigned sm=0;sm<8;sm++)for(unsigned dm=0;dm<8;dm++)
    for(unsigned parity=0;parity<2;parity++) {
        for(unsigned k=0;k<7;k++)r[k]=(word)(0x2000+k*0x100);
        r[0]=0x2000+parity;r[1]=0x4001;r[7]=0x1000;
        p[0][0]=0x1002;p[0][1]=1;p[1][0]=0x1004;p[1][1]=0xffff;
        fixture((word)(ops[op]|(sm<<9)|(dm<<3)|1),15,r,2,p);
    }
    for(unsigned op=0;op<nops;op++)for(unsigned reg=0;reg<8;reg++)for(unsigned dir=0;dir<2;dir++) {
        for(unsigned k=0;k<7;k++)r[k]=dir?2:0xdfff;
        r[6]=0x6000;r[7]=0x1000;
        fixture((word)(ops[op]|(((dir?040:020)|reg)<<6)|1),1,r,0,p);
    }
    // Wrapped EA arithmetic ending in RAM, avoiding the PSW CSR at fffe/ffff
    // whose accesses the emulator handles internally (outside bus callbacks).
    for(unsigned op=0;op<nops;op++)for(unsigned reg=0;reg<6;reg++) {
        for(unsigned k=0;k<7;k++)r[k]=0xffff;
        r[6]=0x6000;r[7]=0x1000;p[0][0]=0x1002;p[0][1]=2;
        fixture((word)(ops[op]|((060|reg)<<6)|1),1,r,1,p);
    }
    fprintf(stderr,"Byte DCJ11 ENABLE_MMU=0: %u classes, %u completed cases; %u excluded.\n",nops,count,skipped);
    fprintf(stderr,"Reasons (may overlap): abort=%u trap=%u I/O=%u odd-word=%u\n",
        excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3]);
    return 0;
}
