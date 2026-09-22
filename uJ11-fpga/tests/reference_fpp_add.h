/* DCJ11 User Guide 7.6: chop toward zero; round half away from zero.
 * Jam discarded alignment bits, retaining their contribution to subtraction.
 * Verified against arbitrary-precision exact sums by test_fp_math_cp79.py.
 * No instruction decoding, memory semantics or host floating arithmetic. */
#include <stdint.h>
typedef struct { uint64_t bits; int exponent; int zero; } fp79_sum;
static fp79_sum fp79_add(uint64_t a, uint64_t b, int d, int chop)
{
    const uint64_t frac=(UINT64_C(1)<<55)-1, hidden=UINT64_C(1)<<62;
    const uint64_t sign=UINT64_C(1)<<63;
    if(!d){a&=~UINT64_C(0xffffffff);b&=~UINT64_C(0xffffffff);}
    int ae=(a>>55)&255, be=(b>>55)&255;
    if(!ae)a=0;if(!be)b=0;
    if((a&~sign)<(b&~sign)){uint64_t t=a;a=b;b=t;int e=ae;ae=be;be=e;}
    fp79_sum out={a,ae,!ae};if(!ae||!be)return out;
    unsigned gap=ae-be;
    uint64_t x=((a&frac)|(UINT64_C(1)<<55))<<7;
    uint64_t y=((b&frac)|(UINT64_C(1)<<55))<<7;
    if(gap>=64)y=1;
    else if(gap)y=(y>>gap)|((y&((UINT64_C(1)<<gap)-1))!=0);
    if((a^b)&sign){x-=y;if(!x){out.bits=0;out.exponent=0;out.zero=1;return out;}
        while(!(x&hidden)){x<<=1;ae--;}}
    else {x+=y;if(x&(hidden<<1)){x>>=1;ae++;}}
    if(!chop){x+=UINT64_C(1)<<(6+(d?0:32));
        if(x&(hidden<<1)){x>>=1;ae++;}}
    out.bits=(a&sign)|((uint64_t)(ae&255)<<55)|((x>>7)&frac);
    if(!d)out.bits&=~UINT64_C(0xffffffff);
    out.exponent=ae;return out;
}
