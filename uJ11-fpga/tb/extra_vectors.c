/* SWAB/SXT/MARK via the existing DCJ11 executor. */
#include "trace_oracle.h"
static void init(word r[8]) {
    for(unsigned k=0;k<7;k++)r[k]=(word)(0x2000+k*0x100);
    r[6]=0x6000;r[7]=0x1000;
}
int main(void) {
    const word ops[]={0000300,0006700};
    const word edges[]={0,1,0xff,0x100,0x7f,0x80,0x7f00,0x8000,0xff00,0xffff,0x12ab,0x8001};
    word r[8],p[3][2];unsigned start=count;
    for(unsigned op=0;op<2;op++)for(unsigned mode=0;mode<8;mode++)
    for(unsigned reg=0;reg<8;reg++)for(unsigned c=0;c<2;c++) {
        init(r);p[0][0]=0x1002;p[0][1]=0x2000;
        if(!mode && reg!=7)r[reg]=edges[(reg+op+c)%12];
        fixture((word)(ops[op]|(mode<<3)|reg),((reg+mode+op)&7)*2+c,r,1,p);
    }
    for(unsigned op=0;op<2;op++)for(unsigned kind=0;kind<3;kind++)
    for(unsigned e=0;e<12;e++)for(unsigned flags=0;flags<16;flags++) {
        init(r);r[1]=kind==0?edges[e]:0x4000;
        p[0][0]=0x4000;p[0][1]=edges[e];p[1][0]=0x1002;p[1][1]=0x4000;
        fixture((word)(ops[op]|(kind==0?01:kind==1?011:037)),flags,r,2,p);
    }
    for(unsigned op=0;op<2;op++)for(unsigned reg=0;reg<8;reg++)for(unsigned disp=0;disp<4;disp++) {
        init(r);if(reg<6)r[reg]=disp<2?0xfffc:4;
        p[0][0]=0x1002;p[0][1]=disp==0?6:disp==1?8:disp==2?0xfffc:0xfffe;
        fixture((word)(ops[op]|060|reg),disp,r,1,p);
    }
    fprintf(stderr,"SWAB/SXT: %u completed\n",count-start);start=count;
    const word pcs[]={0,0x1000,0xdf80,0xdffe};
    for(unsigned pc=0;pc<4;pc++)for(unsigned nn=0;nn<64;nn++)for(unsigned flags=0;flags<16;flags++) {
        init(r);r[7]=pcs[pc];r[5]=(word)(0x2340+flags);
        p[0][0]=(word)(r[7]+2+2*nn);p[0][1]=edges[nn%12];
        fixture((word)(0006400|nn),flags,r,1,p);
    }
    fprintf(stderr,"MARK: %u completed\n",count-start);
    fprintf(stderr,"Extra DCJ11 ENABLE_MMU=0: %u completed cases; %u excluded.\n",count,skipped);
    fprintf(stderr,"Reasons (may overlap): abort=%u trap=%u I/O=%u odd-word=%u\n",
        excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3]);
    return 0;
}
