#!/usr/bin/env python3
"""Independent 128-bit and Fraction models of the FP11-A 59-bit MOD product."""
import ctypes
import hashlib
import json
import random
import subprocess
from fractions import Fraction
from board_common import ROOT
from build_fp11_cp75 import OUT
from test_fp_math_cp73 import value


def pack(x,d,chop):
    if not x:return 0,0,1
    n,den=abs(x).as_integer_ratio();log=n.bit_length()-den.bit_length()
    if Fraction(2)**log>abs(x):log-=1
    e=log+129;p=56 if d else 24;units=abs(x)*Fraction(2)**(p+128-e)
    q,r=divmod(units.numerator,units.denominator)
    if not chop and 2*r>=units.denominator:q+=1
    if q==1<<p:q>>=1;e+=1
    return (int(x<0)<<63)|((e&255)<<55)|((q<<(56-p))&((1<<55)-1)),e,0


def model(a,b,d,chop):
    if not d:a&=~0xffffffff;b&=~0xffffffff
    exact=value(a)*value(b)
    if not exact:return ((0,0,1),(0,0,1)),False
    quantum=Fraction(2)**(((a>>55)&255)+((b>>55)&255)-256-59)
    units=abs(exact)/quantum
    product=(units.numerator//units.denominator)*quantum
    integer=Fraction(product.numerator//product.denominator);fraction=product-integer
    if product>=Fraction(2)**(56 if d else 24):integer=product;fraction=Fraction(0)
    if exact<0:integer=-integer;fraction=-fraction
    return (pack(integer,d,True),pack(fraction,d,chop)),abs(exact)!=product


def run():
    out=OUT/'math';out.mkdir(parents=True,exist_ok=True)
    source=ROOT/'tb/reference_fp_mod_cp75.h';wrapper=out/'reference.c'
    wrapper.write_text('#include "'+str(source)+'"\nfp75_mod reference(uint64_t a,uint64_t b,int d,int t){return fp75_split(a,b,d,t);}\n')
    subprocess.run(['cc','-shared','-fPIC','-O2','-Wall',str(wrapper),'-o',str(out/'reference.dylib')],check=True,capture_output=True)
    class Part(ctypes.Structure):
        _fields_=[('bits',ctypes.c_uint64),('exponent',ctypes.c_int),('zero',ctypes.c_int)]
    class Result(ctypes.Structure):
        _fields_=[('integer',Part),('fraction',Part)]
    lib=ctypes.CDLL(str(out/'reference.dylib'));f=lib.reference;f.restype=Result
    f.argtypes=[ctypes.c_uint64,ctypes.c_uint64,ctypes.c_int,ctypes.c_int]
    # Expose the unchanged shared MOD helper in a private translation unit.
    original=out/'original.c'
    original.write_text('#include "'+str(ROOT/'../core/core.c')+'"\n'+r'''
void original_mod(uint64_t a,uint64_t b,int d,int t,uint64_t *out){
    regs r={0};r.model=DCJ11;
    r.fpu_fps=FPS_IU|FPS_IV|FPS_ID|(d?FPS_D:0)|(t?FPS_T:0);
    if(!d){a&=0xffffffff00000000ULL;b&=0xffffffff00000000ULL;}
    fpac_t integer={.h=(uint32_t)(a>>32),.l=(uint32_t)a};
    fpac_t source={.h=(uint32_t)(b>>32),.l=(uint32_t)b},fraction;
    out[2]=modfp11(&r,&integer,&source,&fraction);
    out[0]=((uint64_t)integer.h<<32)|integer.l;
    out[1]=((uint64_t)fraction.h<<32)|fraction.l;out[3]=r.fpu_fec;
    /* F_STORE writes only the high half after the numeric helper. */
    if(!d){out[0]&=0xffffffff00000000ULL;out[1]&=0xffffffff00000000ULL;}
}
''')
    subprocess.run(['cc','-shared','-fPIC','-O2','-DENABLE_MMU=0','-I..',str(original),'../core/hardware.c','-lm','-o',str(out/'original.dylib')],cwd=ROOT,check=True,capture_output=True)
    oracle=ctypes.CDLL(str(out/'original.dylib'));o=oracle.original_mod;o.restype=None
    o.argtypes=[ctypes.c_uint64,ctypes.c_uint64,ctypes.c_int,ctypes.c_int,ctypes.POINTER(ctypes.c_uint64)]
    observed=(ctypes.c_uint64*4)()
    rng=random.Random(0x75f11a);checks=lost=nonzero=0
    for i in range(50000):
        a=rng.getrandbits(64);b=rng.getrandbits(64)
        # Half the samples concentrate on products with both nonzero parts.
        if i&1:
            a=(a&~(255<<55))|((129+i%28)<<55)
            b=(b&~(255<<55))|(129<<55)
        for d in (0,1):
            for t in (0,1):
                want,discarded=model(a,b,d,t);got=f(a,b,d,t)
                actual=tuple((p.bits,p.exponent,p.zero) for p in (got.integer,got.fraction))
                assert actual==want,(hex(a),hex(b),d,t,want,actual)
                o(a,b,d,t,observed)
                overflow=not want[0][2] and want[0][1]>255
                underflow=not want[1][2] and want[1][1]<=0
                expected=(want[0][0],want[1][0],2 if overflow else 0,0o10 if overflow else 0o12 if underflow else 0)
                assert tuple(observed)==expected,(hex(a),hex(b),d,t,expected,tuple(observed))
                checks+=1;lost+=discarded;nonzero+=not want[0][2] and not want[1][2]
    inputs={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (source,ROOT/'tools/test_fp_math_cp75.py',ROOT/'tools/test_fp_math_cp73.py',*[ROOT/'../core'/n for n in ('core.c','core.h','hardware.c','hardware.h','pdp11_fp.c')])}
    record=dict(passed=True,comparisons=checks,original_mod_comparisons=checks,quantized_product_cases=lost,both_parts_nonzero=nonzero,inputs=inputs)
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))


if __name__=='__main__':run()
