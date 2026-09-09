#include "trace_oracle.h"
int main(void) {
    const word ops[]={0010000,0020000,0060000,0030000,0040000,0050000,0160000};
    const unsigned nops=sizeof ops/sizeof ops[0];
    const word edges[]={0,1,2,0x7ffe,0x7fff,0x8000,0x8001,0xfffe,0xffff};
    word r[8],p[4][2];
    for(unsigned op=0;op<nops;op++)for(unsigned sm=0;sm<8;sm++)for(unsigned dm=0;dm<8;dm++)
    for(unsigned sr=0;sr<8;sr++)for(unsigned dr=0;dr<8;dr++) {
        for(unsigned k=0;k<7;k++)r[k]=(word)(0x2000+k*0x100);
        r[7]=0x1000;
        fixture((word)(ops[op]|(sm<<9)|(sr<<6)|(dm<<3)|dr),(sm+dm+sr+dr)&15,r,0,p);
    }
    // Memory/immediate data edges are independent of pointer values.
    for(unsigned op=0;op<nops;op++)for(unsigned kind=0;kind<3;kind++)
    for(unsigned a=0;a<9;a++)for(unsigned b=0;b<9;b++)for(unsigned carry=0;carry<2;carry++) {
        for(unsigned k=0;k<7;k++)r[k]=(word)(0x2000+k*0x100);
        r[1]=0x4000;r[7]=0x1000;
        p[0][0]=kind==2 ? 0x1002 : 0x2000;p[0][1]=edges[a];
        p[1][0]=0x4000;p[1][1]=edges[b];
        unsigned source=kind==2 ? 027 : 010,destination=kind==1 ? 1 : 011;
        if(kind==1)r[1]=edges[b];
        fixture((word)(ops[op]|(source<<6)|destination),carry,r,2,p);
    }
    // Indexed arithmetic wraps to 16 bits; both signs and PC-relative forms.
    for(unsigned op=0;op<nops;op++)for(unsigned sr=0;sr<8;sr++)for(unsigned disp=0;disp<4;disp++) {
        for(unsigned k=0;k<7;k++)r[k]=disp<2 ? 0xfffc : 4;
        r[6]=0x6000;r[7]=0x1000;
        p[0][0]=0x1002;p[0][1]=disp==0 ? 6 : disp==1 ? 8 : disp==2 ? 0xfffc : 0xfffe;
        fixture((word)(ops[op]|(6<<9)|(sr<<6)|1),disp,r,1,p);
    }
    fprintf(stderr,"EA DCJ11 ENABLE_MMU=0: %u completed cases; %u trapping/I-O cases excluded (Stage 2).\n",count,skipped);
    for(unsigned op=0;op<nops;op++)fprintf(stderr,"  opcode class %02o: %u complete, %u excluded\n",
        ops[op]>>12,completed_by_op[ops[op]>>12],excluded_by_op[ops[op]>>12]);
    fprintf(stderr,"  exclusion reasons (may overlap): abort=%u trap=%u I/O=%u odd=%u\n",
        excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3]);
    return 0;
}
