#!/usr/bin/env python3
"""Exact Fraction arithmetic versus 128-bit C and serial 64-bit algorithms."""
import ctypes
import hashlib
import json
import random
import subprocess
from fractions import Fraction
from board_common import ROOT
from build_fp11_cp74 import OUT
from test_fp_math_cp73 import value


def model(a,b,d,chop,divide):
    if not d:a&=~0xffffffff;b&=~0xffffffff
    x,y=value(a),value(b)
    if divide and not y:return 0,0,2
    exact=x/y if divide else x*y
    if not exact:return 0,0,1
    n,den=abs(exact).as_integer_ratio()
    log=n.bit_length()-den.bit_length()
    if Fraction(2)**log>abs(exact):log-=1
    e=log+129;p=56 if d else 24
    units=abs(exact)*Fraction(2)**(p+128-e)
    q,r=divmod(units.numerator,units.denominator)
    if not chop and r*2>=units.denominator:q+=1
    if q==1<<p:q>>=1;e+=1
    bits=(int(exact<0)<<63)|((e&255)<<55)|((q<<(0 if d else 32))&((1<<55)-1))
    return bits,e,0


def serial(a,b,d,chop,divide):
    """64-bit shift/add/restoring loops, including F's shortened iteration."""
    if not d:a&=~0xffffffff;b&=~0xffffffff
    ae=(a>>55)&255;be=(b>>55)&255
    if divide and not be:return 0,0,2
    if not ae or not be:return 0,0,1
    mask=(1<<55)-1
    x=((a&mask)|(1<<55))<<7;y=((b&mask)|(1<<55))<<7;q=0
    if divide:
        e=ae-be+129
        for _ in range(63 if d else 31):
            q<<=1
            if x>=y:x-=y;q|=1<<(0 if d else 32)
            x=(x<<1)&((1<<64)-1)
    else:
        e=ae+be-128;y>>=7+(0 if d else 32)
        for _ in range(56 if d else 24):
            if y&1:q+=x
            q>>=1;y>>=1
    if not q&(1<<62):q<<=1;e-=1
    if not chop:q+=1<<(6+(0 if d else 32))
    if q&(1<<63):q>>=1;e+=1
    bits=((a^b)&(1<<63))|((e&255)<<55)|((q>>7)&mask)
    if not d:bits&=~0xffffffff
    return bits,e,0


def run():
    out=OUT/'math';out.mkdir(parents=True,exist_ok=True)
    source=ROOT/'tb/reference_fp_muldiv_cp74.h'
    wrapper=out/'reference.c';wrapper.write_text('#include "'+str(source)+'"\nfp74_result reference(uint64_t a,uint64_t b,int d,int t,int op){return fp74_muldiv(a,b,d,t,op);}\n')
    subprocess.run(['cc','-shared','-fPIC','-O2','-Wall',str(wrapper),'-o',str(out/'reference.dylib')],check=True,capture_output=True)
    class Result(ctypes.Structure):
        _fields_=[('bits',ctypes.c_uint64),('exponent',ctypes.c_int),('zero',ctypes.c_int)]
    lib=ctypes.CDLL(str(out/'reference.dylib'));f=lib.reference;f.restype=Result
    f.argtypes=[ctypes.c_uint64,ctypes.c_uint64,ctypes.c_int,ctypes.c_int,ctypes.c_int]
    rng=random.Random(0x74f11a);checks=0;bounds=0;loops=0
    for i in range(25000):
        a=rng.getrandbits(64);b=rng.getrandbits(64)
        if i%8==0:b=a
        elif i%8==1:b=0x4080000000000000
        for d in (0,1):
            for t in (0,1):
                for op in (0,1):
                    want=model(a,b,d,t,op);got=f(a,b,d,t,op)
                    assert (got.bits,got.exponent,got.zero)==want,(hex(a),hex(b),d,t,op,want,(got.bits,got.exponent,got.zero))
                    assert serial(a,b,d,t,op)==want,(hex(a),hex(b),d,t,op,want,serial(a,b,d,t,op))
                    checks+=1;loops+=1
                    if 1<=want[1]<=255 and not want[2]:
                        aa=value(a if d else a&~0xffffffff);bb=value(b if d else b&~0xffffffff)
                        exact=aa/bb if op else aa*bb
                        ulp=Fraction(2)**(want[1]-128-(56 if d else 24))
                        assert abs(value(want[0])-exact)<=ulp*(1 if t else Fraction(1,2))
                        bounds+=1
    inputs={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (source,ROOT/'tools/test_fp_math_cp74.py',ROOT/'tools/test_fp_math_cp73.py')}
    record=dict(passed=True,comparisons=checks,serial_comparisons=loops,exact_error_bounds=bounds,inputs=inputs)
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))


if __name__=='__main__':run()
