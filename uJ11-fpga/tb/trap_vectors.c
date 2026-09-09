/* Successful kernel/T=0 software traps and RTI through the actual DCJ11 core. */
#include "trace_oracle.h"
#ifndef ORACLE_TRAP_PROFILE
#error "This suite must audit, rather than exclude, its expected vector entry"
#endif
static word psw_value(unsigned n) {return (word)((n&15)|((n>>4)<<5));}
static void init(word r[8]) {
    for(unsigned k=0;k<7;k++)r[k]=(word)(0x2000+0x100*k);
    r[6]=0x6000;r[7]=0x1000;
}
int main(void) {
    const word ops[]={000003,000004,0104000,0104400};
    const word vectors[]={000014,000020,000030,000034};
    const word stacks[]={0x6000,0x1002,0x1004};
    const word return_pc[]={0,1,0x2000,0x7fff,0x8000,0x8001,0xdffe,0xffff};
    const word return_sp[]={0x0400,0x1000,0x1002,0xdffc};
    word r[8],p[2][2];unsigned start=count;
    for(unsigned op=0;op<4;op++)for(unsigned state=0;state<128;state++)for(unsigned sp=0;sp<3;sp++) {
        init(r);r[6]=stacks[sp];expected_vector=vectors[op];
        p[0][0]=vectors[op];p[0][1]=(word)(0x3000+(state&1));
        p[1][0]=(word)(vectors[op]+2);p[1][1]=psw_value((state*37+13)&127);
        fixture(ops[op],psw_value(state),r,2,p);
    }
    for(unsigned op=2;op<4;op++)for(unsigned code=0;code<256;code++)for(unsigned state=0;state<4;state++) {
        init(r);expected_vector=vectors[op];
        p[0][0]=vectors[op];p[0][1]=(word)(0x3200+2*code);
        p[1][0]=(word)(vectors[op]+2);p[1][1]=psw_value((code+state*31)&127);
        fixture((word)(ops[op]|code),psw_value(((code&7)<<4)|(state*5)),r,2,p);
    }
    fprintf(stderr,"Software traps: %u completed\n",count-start);start=count;expected_vector=0;
    for(unsigned old_ipl=0;old_ipl<8;old_ipl++)for(unsigned state=0;state<128;state++) {
        init(r);r[6]=return_sp[state%4];
        p[0][0]=r[6];p[0][1]=return_pc[(state+old_ipl)%8];
        p[1][0]=(word)(r[6]+2);p[1][1]=psw_value(state);
        fixture(000002,(word)((old_ipl<<5)|((state^15)&15)),r,2,p);
    }
    for(unsigned pc=0;pc<8;pc++)for(unsigned sp=0;sp<4;sp++) {
        init(r);r[6]=return_sp[sp];
        p[0][0]=r[6];p[0][1]=return_pc[pc];
        p[1][0]=(word)(r[6]+2);p[1][1]=psw_value((pc*13+sp)&127);
        fixture(000002,psw_value((pc*7+sp)&127),r,2,p);
    }
    fprintf(stderr,"RTI: %u completed\n",count-start);
    fprintf(stderr,"Trap DCJ11 kernel/T=0 ENABLE_MMU=0: %u completed cases; %u excluded.\n",count,skipped);
    fprintf(stderr,"Reasons (may overlap): abort=%u trap/vector=%u I/O=%u odd-word=%u\n",
        excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3]);
    return skipped?1:0;
}
