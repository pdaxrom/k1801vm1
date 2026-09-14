/* Independent FP11-A MOD numeric model for the arithmetic audit only.
 * It is NOT substituted into the differential core_step oracle.
 * DEC FP11-A: retain the 59-bit product before normalization/splitting. */
#include <stdint.h>
typedef struct { uint64_t bits; int exponent,zero; } fp75_part;
typedef struct { fp75_part integer,fraction; } fp75_mod;
static fp75_part fp75_pack(__uint128_t m,int scale,int sign,int p,int chop){
    fp75_part r={0,0,1};if(!m)return r;
    __uint128_t tmp=m;int width=0;while(tmp){width++;tmp>>=1;}
    r.exponent=scale+width+128;r.zero=0;
    if(width>p){int shift=width-p;
        if(!chop)m+=((__uint128_t)1)<<(shift-1);
        m>>=shift;
        if(m==((__uint128_t)1<<p)){m>>=1;r.exponent++;}
    }else m<<=p-width;
    r.bits=((uint64_t)sign<<63)|((uint64_t)(r.exponent&255)<<55)|
        (((uint64_t)m<<(56-p))&0x7fffffffffffffULL);
    return r;
}
static fp75_mod fp75_split(uint64_t a,uint64_t b,int d,int chop){
    fp75_mod r={{0,0,1},{0,0,1}};
    if(!d){a&=0xffffffff00000000ULL;b&=0xffffffff00000000ULL;}
    int ae=(a>>55)&255,be=(b>>55)&255;if(!ae||!be)return r;
    __uint128_t am=(a&0x7fffffffffffffULL)|0x80000000000000ULL;
    __uint128_t bm=(b&0x7fffffffffffffULL)|0x80000000000000ULL;
    __uint128_t product=(am*bm)>>53;int scale=ae+be-256-59;
    int sign=(a^b)>>63,p=d?56:24;
    __uint128_t tmp=product;int width=0;while(tmp){width++;tmp>>=1;}
    /* DEC case 2 discards the entire fraction for |PROD| >= 2**L,
     * even when some low product bits still encode a proper fraction. */
    if(width+scale>p || scale>=0)r.integer=fp75_pack(product,scale,sign,p,1);
    else if(-scale>=128)r.fraction=fp75_pack(product,scale,sign,p,chop);
    else{
        r.integer=fp75_pack(product>>(-scale),0,sign,p,1);
        r.fraction=fp75_pack(product&(((__uint128_t)1<<(-scale))-1),scale,sign,p,chop);
    }
    return r;
}
