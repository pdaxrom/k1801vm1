/* Exact integer FP11-A MUL/DIV oracle. Independent of the firmware's
 * serial shift/add and restoring division; no host floating point. */
#include <stdint.h>
typedef struct { uint64_t bits; int exponent; int zero; } fp74_result;
static fp74_result fp74_muldiv(uint64_t a,uint64_t b,int d,int chop,int divide)
{
    const uint64_t mask=(UINT64_C(1)<<55)-1,sign=UINT64_C(1)<<63;
    if(!d){a&=~UINT64_C(0xffffffff);b&=~UINT64_C(0xffffffff);}
    int ae=(a>>55)&255,be=(b>>55)&255,p=d?56:24;
    fp74_result out={0,0,1};
    /* Divide-by-zero has no numeric result; caller handles it first. */
    if(divide && !be){out.zero=2;return out;}
    if(!ae || !be)return out;
    __uint128_t x=(a&mask)|(UINT64_C(1)<<55);
    __uint128_t y=(b&mask)|(UINT64_C(1)<<55),n,den;
    int e;
    if(divide){
        e=ae-be+129;
        if(x<y){e--;x<<=1;}
        n=x<<(p-1);den=y;
    }else{
        e=ae+be-128;n=x*y;
        if(!(n&((__uint128_t)1<<111))){n<<=1;e--;}
        den=(__uint128_t)1<<(112-p);
    }
    __uint128_t q=n/den,r=n%den;
    if(!chop && r*2>=den)q++;
    if(q==((__uint128_t)1<<p)){q>>=1;e++;}
    out.bits=((a^b)&sign)|((uint64_t)(e&255)<<55)|(((uint64_t)q<<(d?0:32))&mask);
    out.exponent=e;out.zero=0;return out;
}
