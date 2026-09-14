/* Independent unsigned-integer ADD reference for FP11-A (DEC 1981, p.317).
 * No host float/double. Inputs/outputs use FP11 register bit order. This
 * reference has no PDP-11 instruction/memory implementation. */
#include <stdint.h>
typedef struct { uint64_t bits; int exponent; int zero; } fp73_sum;
static fp73_sum fp73_add(uint64_t a, uint64_t b, int d, int truncate, int guard)
{
    const uint64_t frac=(UINT64_C(1)<<55)-1, hidden=UINT64_C(1)<<(55+guard);
    const uint64_t sign=UINT64_C(1)<<63;
    if(!d){a&=~UINT64_C(0xffffffff);b&=~UINT64_C(0xffffffff);}
    int ae=(a>>55)&255,be=(b>>55)&255;
    if(!ae)a=0;if(!be)b=0;
    if((a&~sign)<(b&~sign)){uint64_t t=a;a=b;b=t;int e=ae;ae=be;be=e;}
    fp73_sum out={a,ae,!ae};if(!ae||!be)return out;
    unsigned gap=ae-be;
    uint64_t x=((a&frac)|(UINT64_C(1)<<55))<<guard;
    uint64_t y=((b&frac)|(UINT64_C(1)<<55))<<guard;
    y=gap>=64?0:y>>gap;
    if((a^b)&sign){x-=y;if(!x){out.bits=0;out.exponent=0;out.zero=1;return out;}
        while(!(x&hidden)){x<<=1;ae--;}}
    else {x+=y;if(x&(hidden<<1)){x>>=1;ae++;}}
    if(!truncate){x+=UINT64_C(1)<<(guard-1+(d?0:32));
        if(x&(hidden<<1)){x>>=1;ae++;}}
    out.bits=(a&sign)|((uint64_t)(ae&255)<<55)|((x>>guard)&frac);
    if(!d)out.bits&=~UINT64_C(0xffffffff);
    out.exponent=ae;return out;
}
