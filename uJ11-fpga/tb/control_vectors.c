/* CP11: actual DCJ11 control flow, link/SP/PC aliases and stack bus ordering. */
#include "trace_oracle.h"
static void init(word r[8]) {
    for(unsigned k=0;k<7;k++)r[k]=(word)(0x2000+k*0x100);
    r[6]=0x6000;r[7]=0x1000;
}
int main(void) {
    word r[8],p[4][2];unsigned start,drop;
    const word edges[]={0,1,2,0x7fff,0x8000,0xfffe,0xffff};
    start=count;drop=skipped;
    for(unsigned mode=1;mode<8;mode++)for(unsigned reg=0;reg<8;reg++)
    for(unsigned flags=0;flags<16;flags++) {
        init(r);p[0][0]=0x1002;p[0][1]=0x2000;
        fixture((word)(0000100|(mode<<3)|reg),flags,r,1,p);
    }
    for(unsigned reg=0;reg<7;reg++)for(unsigned parity=0;parity<2;parity++) {
        init(r);r[reg]=(word)(0xfffe + parity);p[0][0]=0x1002;p[0][1]=4;
        fixture((word)(0000160|reg),15,r,1,p); // wrapped EA, no target read
    }
    fprintf(stderr,"JMP: %u completed, %u excluded\n",count-start,skipped-drop);
    start=count;drop=skipped;
    for(unsigned link=0;link<8;link++)for(unsigned mode=1;mode<8;mode++)
    for(unsigned reg=0;reg<8;reg++)for(unsigned flags=0;flags<16;flags++) {
        init(r);p[0][0]=0x1002;p[0][1]=0x2000;
        fixture((word)(0004000|(link<<6)|(mode<<3)|reg),flags,r,1,p);
    }
    // Stack overlaps destination pointer or extension; link may be SP or PC.
    for(unsigned link=0;link<8;link++)for(unsigned mode=1;mode<8;mode++)
    for(unsigned reg=0;reg<8;reg++)for(unsigned kind=0;kind<4;kind++) {
        init(r);r[6]=kind==0?0x2002:kind==1?0x1004:kind==2?2:0xdffe;
        p[0][0]=0x1002;p[0][1]=0x2000;
        fixture((word)(0004000|(link<<6)|(mode<<3)|reg),kind,r,1,p);
    }
    fprintf(stderr,"JSR: %u completed, %u excluded\n",count-start,skipped-drop);
    start=count;drop=skipped;
    for(unsigned reg=0;reg<8;reg++)for(unsigned flags=0;flags<16;flags++)
    for(unsigned edge=0;edge<7;edge++) {
        init(r);p[0][0]=r[6];p[0][1]=edges[edge];
        fixture((word)(0000200|reg),flags,r,1,p);
    }
    for(unsigned reg=0;reg<8;reg++)for(unsigned kind=0;kind<3;kind++) {
        init(r);r[6]=kind==0?0:kind==1?0xdffe:0x1002;
        p[0][0]=r[6];p[0][1]=0x2345;
        fixture((word)(0000200|reg),15,r,1,p);
    }
    fprintf(stderr,"RTS: %u completed, %u excluded\n",count-start,skipped-drop);
    start=count;drop=skipped;
    for(unsigned reg=0;reg<8;reg++)for(unsigned disp=0;disp<64;disp++)
    for(unsigned edge=0;edge<7;edge++) {
        init(r);if(reg!=7)r[reg]=edges[edge];
        else r[7]=edge%3==0?0:edge%3==1?0x1000:0xdffe;
        fixture((word)(0077000|(reg<<6)|disp),(reg+disp+edge)&15,r,0,p);
    }
    for(unsigned reg=0;reg<8;reg++)for(unsigned flags=0;flags<16;flags++)
    for(unsigned edge=0;edge<7;edge++) {
        init(r);if(reg!=7)r[reg]=edges[edge];
        fixture((word)(0077000|(reg<<6)|63),flags,r,0,p);
    }
    fprintf(stderr,"SOB: %u completed, %u excluded\n",count-start,skipped-drop);
    fprintf(stderr,"Control DCJ11 ENABLE_MMU=0: %u completed cases; %u excluded.\n",count,skipped);
    fprintf(stderr,"Reasons (may overlap): abort=%u trap=%u I/O=%u odd-word=%u\n",
        excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3]);
    return 0;
}
