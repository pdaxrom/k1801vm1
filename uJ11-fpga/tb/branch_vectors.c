/* CP9: all branch conditions, offsets and architectural PC wrapping. */
#include "trace_oracle.h"
int main(void) {
    const word ops[]={0000400,0001000,0001400,0002000,0002400,0003000,0003400,
                      0100000,0100400,0101000,0101400,0102000,0102400,0103000,0103400};
    const word pcs[]={0,0x1000,0xdffe};
    const int offsets[]={-128,-1,0,1,127};
    word r[8],p[1][2];
    for(unsigned op=0;op<15;op++)for(unsigned flags=0;flags<16;flags++)
    for(unsigned pc=0;pc<3;pc++)for(unsigned disp=0;disp<5;disp++) {
        for(unsigned k=0;k<7;k++)r[k]=(word)(0x2000+k*0x100);
        r[7]=pcs[pc];fixture((word)(ops[op]|(offsets[disp]&255)),flags,r,0,p);
    }
    for(unsigned op=0;op<15;op++)for(unsigned disp=0;disp<256;disp++) {
        for(unsigned k=0;k<7;k++)r[k]=(word)(0x2000+k*0x100);
        r[7]=0x1000;fixture((word)(ops[op]|disp),(op^disp)&15,r,0,p);
    }
    fprintf(stderr,"Branch DCJ11 ENABLE_MMU=0: %u completed cases; %u excluded.\n",count,skipped);
    fprintf(stderr,"Reasons (may overlap): abort=%u trap=%u I/O=%u odd=%u\n",
        excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3]);
    return 0;
}
