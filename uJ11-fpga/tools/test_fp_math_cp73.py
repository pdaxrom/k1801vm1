#!/usr/bin/env python3
"""Independent Python integers versus C reference, including FP11-A guards.

Exact normalized arithmetic is also checked against DEC error bounds for
nonexceptional results. No Python float or Decimal rounding is involved.
"""
import ctypes
import hashlib
import json
import random
import subprocess
from fractions import Fraction
from board_common import ROOT
from build_fp11_cp73 import OUT

SIGN=1<<63;FRAC=(1<<55)-1


def model(a,b,d,t,guards=7):
    if not d:a &= ~0xffffffff;b &= ~0xffffffff
    def decode(raw):
        e=(raw>>55)&255
        return (e,0 if not e else ((raw&FRAC)|(1<<55))<<guards,raw>>63 if e else 0)
    x=decode(a);y=decode(b)
    if (x[0],x[1])<(y[0],y[1]):x,y=y,x
    e,m,sign=x;f,n,other=y
    if not m:return 0,0,True
    n >>= e-f
    m=m-n if sign!=other else m+n
    if not m:return 0,0,True
    length=m.bit_length();target=56+guards
    if length>target:m >>= length-target;e += length-target
    elif length<target:m <<= target-length;e -= target-length
    # Round to F/D precision using quotient and remainder, independently of
    # the C reference's addition of a rounding constant before packing.
    lost=guards+(0 if d else 32)
    q,r=divmod(m,1<<lost)
    if not t and r>=(1<<(lost-1)):q+=1
    precision=56 if d else 24
    if q==(1<<precision):q >>=1;e+=1
    bits=(sign<<63)|((e&255)<<55)|((q<<(0 if d else 32))&FRAC)
    return bits,e,False


def value(raw):
    e=(raw>>55)&255
    if not e:return Fraction(0)
    n=(raw&FRAC)|(1<<55)
    shift=e-128-56
    v=Fraction(n*(1<<shift)) if shift>=0 else Fraction(n,1<<(-shift))
    return -v if raw&SIGN else v


def run():
    out=OUT/'math';out.mkdir(parents=True,exist_ok=True)
    source=ROOT/'tb/reference_fp_add_cp73.h'
    wrapper=out/'reference.c';wrapper.write_text('#include "'+str(source)+'"\nfp73_sum reference(uint64_t a,uint64_t b,int d,int t,int g){return fp73_add(a,b,d,t,g);}\n')
    subprocess.run(['cc','-shared','-fPIC','-O2','-Wall',str(wrapper),'-o',str(out/'reference.dylib')],check=True,capture_output=True)
    class Result(ctypes.Structure):
        _fields_=[('bits',ctypes.c_uint64),('exponent',ctypes.c_int),('zero',ctypes.c_int)]
    lib=ctypes.CDLL(str(out/'reference.dylib'));f=lib.reference;f.restype=Result
    f.argtypes=[ctypes.c_uint64,ctypes.c_uint64,ctypes.c_int,ctypes.c_int,ctypes.c_int]
    rng=random.Random(0x73f11a);checks=0;bounds=0;diff=[]
    # Include close pairs, cancellation, large exponent gaps and all guards.
    for i in range(50000):
        a=rng.getrandbits(64);b=rng.getrandbits(64)
        if i%4==0:b=a^SIGN
        elif i%4==1:b=(a^SIGN)+rng.choice((-1,1,-2,2,-(1<<32),1<<32))&((1<<64)-1)
        for d in (0,1):
            for t in (0,1):
                want=model(a,b,d,t);got=f(a,b,d,t,7)
                assert (got.bits,got.exponent,bool(got.zero))==want,(hex(a),hex(b),d,t,want,(got.bits,got.exponent,got.zero))
                checks+=1
                if 1<=want[1]<=255:
                    aa=a if d else a&~0xffffffff;bb=b if d else b&~0xffffffff
                    exact=value(aa)+value(bb);actual=value(want[0])
                    ulp=Fraction(2)**(want[1]-128-(56 if d else 24))
                    # A/F rounded opposite-sign D bound: 33/64 ULP; all
                    # chopped operations <=1 ULP. Others rounded <=1/2 ULP.
                    limit=Fraction(1) if t else Fraction(33,64) if d and (aa^bb)&SIGN else Fraction(1,2)
                    assert abs(actual-exact)<=ulp*limit,(hex(a),hex(b),d,t,actual-exact,ulp)
                    bounds+=1
                old=model(a,b,d,t,3)
                if old!=want and len(diff)<12:diff.append(dict(a=f'{a:016x}',b=f'{b:016x}',d=d,truncate=t,fp11a=f'{want[0]:016x}',three_guard=f'{old[0]:016x}'))
    assert diff,'need explicit three-versus-seven-guard evidence'
    record=dict(passed=True,comparisons=checks,exact_error_bounds=bounds,guard_differences=diff,
                inputs={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (source,ROOT/'tools/test_fp_math_cp73.py')})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))


if __name__=='__main__':run()
