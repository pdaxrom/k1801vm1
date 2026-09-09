/* DCJ11 priority boundary oracle, one offered IRQ per instruction. */
#include "trace_oracle.h"
#ifndef ORACLE_IRQ_PROFILE
#error "IRQ suite requires the IRQ/vector/access audits"
#endif
static word flags(unsigned n){return (word)((n&15)|((n>>4)<<5));}
static void init(word r[8]) {
    for(unsigned k=0;k<7;k++)r[k]=(word)(0x2000+0x100*k);
    r[0]=0x8001;r[1]=1;r[6]=0x6000;r[7]=0x1000;
}
static void offer(unsigned priority,word vector,unsigned resulting_ipl) {
    irq_request=priority?(word)((priority<<9)|vector):0;
    expected_vector=priority>resulting_ipl?vector:0;
}
int main(void) {
    word r[8],p[4][2];
    const word ops[]={0010001,0000001,0077101,0077101};
    for(unsigned op=0;op<4;op++)for(unsigned state=0;state<128;state++)for(unsigned pri=0;pri<8;pri++) {
        init(r);if(op>=2)r[1]=(word)(op-1);offer(pri,0100,state>>4);
        p[0][0]=0100;p[0][1]=0x3000;p[1][0]=0102;p[1][1]=0345;
        fixture(ops[op],flags(state),r,2,p);
    }
    for(unsigned level=0;level<8;level++)for(unsigned state=0;state<128;state++) {
        init(r);offer(0,0100,level);fixture((word)(0000230|level),flags(state),r,0,p);
    }
    for(unsigned level=0;level<8;level++)for(unsigned old=0;old<8;old++)for(unsigned pri=0;pri<8;pri++) {
        init(r);offer(pri,0060,level);
        p[0][0]=0060;p[0][1]=0x3200;p[1][0]=0062;p[1][1]=0347;
        fixture((word)(0000230|level),(word)((old<<5)|((old+level+pri)&15)),r,2,p);
    }
    for(unsigned vector=2;vector<512;vector+=2)for(unsigned pattern=0;pattern<2;pattern++) {
        init(r);offer(1+vector%7,(word)vector,0);
        p[0][0]=(word)vector;p[0][1]=(word)(0x3400+2*vector);
        p[1][0]=(word)(vector+2);p[1][1]=(word)(0340|(pattern?15:0));
        fixture(0000001,(word)(pattern?15:0),r,2,p);
    }
    for(unsigned old=0;old<8;old++)for(unsigned level=0;level<8;level++)for(unsigned pri=0;pri<8;pri++) {
        init(r);offer(pri,0210,level);
        p[0][0]=0x6000;p[0][1]=0x1800;p[1][0]=0x6002;p[1][1]=(word)((level<<5)|((old+pri)&15));
        p[2][0]=0210;p[2][1]=0x3600;p[3][0]=0212;p[3][1]=0343;
        fixture(0000002,(word)((old<<5)|((level+pri)&15)),r,4,p);
    }
    fprintf(stderr,"IRQ/SPL/WAIT DCJ11 kernel/T=0 ENABLE_MMU=0: %u completed; %u excluded.\n",count,skipped);
    fprintf(stderr,"Reasons: abort=%u vector=%u I/O=%u odd=%u\n",excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3]);
    return skipped?1:0;
}
