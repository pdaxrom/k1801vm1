/* CP9 unary-word oracle uses the existing DCJ11 executor and exact bus trace. */
#include "trace_oracle.h"
int main(void) {
#ifdef BYTE_VARIANT
    const word ops[]={0105000,0105100,0105200,0105300,0105400,0105500,
                      0105600,0105700,0106000,0106100,0106200,0106300};
    const word edges[]={0,1,2,0x7e,0x7f,0x80,0x81,0xfe,0xff};
#else
    const word ops[]={0005000,0005100,0005200,0005300,0005400,0005500,
                      0005600,0005700,0006000,0006100,0006200,0006300};
    const word edges[]={0,1,2,0x7ffe,0x7fff,0x8000,0x8001,0xfffe,0xffff};
#endif
    word r[8],p[4][2];
    for(unsigned op=0;op<12;op++)for(unsigned mode=0;mode<8;mode++)
    for(unsigned reg=0;reg<8;reg++)for(unsigned carry=0;carry<2;carry++) {
        for(unsigned k=0;k<7;k++)r[k]=(word)(0x2000+k*0x100);
        r[7]=0x1000;
        if(!mode && reg!=7)r[reg]=edges[(op+reg+carry)%9];
#ifdef BYTE_VARIANT
        if(!mode && reg!=7)r[reg]|=0xa500;
#endif
        fixture((word)(ops[op]|(mode<<3)|reg),((op+mode+reg)&7)*2+carry,r,0,p);
    }
    // Independent operand/flag edges for register, indirect and absolute forms.
    for(unsigned op=0;op<12;op++)for(unsigned kind=0;kind<3;kind++)
    for(unsigned e=0;e<9;e++)for(unsigned psw=0;psw<16;psw++) {
        for(unsigned k=0;k<7;k++)r[k]=(word)(0x2000+k*0x100);
        r[7]=0x1000;r[1]=kind==0?edges[e]:0x4000;
#ifdef BYTE_VARIANT
        if(kind==0)r[1]|=0x5a00;
#endif
        p[0][0]=0x4000;p[0][1]=edges[e];p[1][0]=0x1002;p[1][1]=0x4000;
        fixture((word)(ops[op]|(kind==0?01:kind==1?011:037)),psw,r,2,p);
    }
    for(unsigned op=0;op<12;op++)for(unsigned reg=0;reg<8;reg++)for(unsigned disp=0;disp<4;disp++) {
        for(unsigned k=0;k<7;k++)r[k]=disp<2?0xfffc:4;
        r[6]=0x6000;r[7]=0x1000;
        p[0][0]=0x1002;p[0][1]=disp==0?6:disp==1?8:disp==2?0xfffc:0xfffe;
        fixture((word)(ops[op]|060|reg),disp,r,1,p);
    }
    fprintf(stderr,"Single DCJ11 ENABLE_MMU=0: %u completed cases; %u excluded.\n",count,skipped);
    fprintf(stderr,"Reasons (may overlap): abort=%u trap=%u I/O=%u odd=%u\n",
        excluded_reason[0],excluded_reason[1],excluded_reason[2],excluded_reason[3]);
    return 0;
}
