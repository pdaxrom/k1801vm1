#!/usr/bin/env python3
"""Compare DCJ11 ADD/SUB reference to exact arbitrary-precision integer sums."""
import ctypes
import hashlib
import json
import random
import subprocess
from board_common import ROOT

SIGN=1<<63
MASK=(1<<55)-1


def exact(a,b,d,chop):
    if not d:a &= ~0xffffffff;b &= ~0xffffffff
    def decode(x):
        e=(x>>55)&255
        m=((x&MASK)|(1<<55)) if e else 0
        return e, -m if x&SIGN else m
    ae,am=decode(a);be,bm=decode(b);base=min(ae,be)
    # Keep every input bit regardless of exponent gap. No guard-bit model.
    total=(am<<(ae-base))+(bm<<(be-base))
    if not total:return 0,0,True
    sign=SIGN if total<0 else 0;n=abs(total);length=n.bit_length()
    exponent=base+length-56;precision=56 if d else 24
    shift=length-precision
    if shift>0:
        q,r=divmod(n,1<<shift)
        if not chop and 2*r>=1<<shift:q+=1
    else:q=n<<(-shift)
    if q==1<<precision:q>>=1;exponent+=1
    return sign|((exponent&255)<<55)|((q<<(56-precision))&MASK),exponent,False


def run():
    out=ROOT/'build/fpp/math';out.mkdir(parents=True,exist_ok=True)
    src=ROOT/'tests/reference_fpp_add.h'
    wrapper=out/'reference.c'
    wrapper.write_text('#include "'+str(src)+'"\nfp79_sum reference(uint64_t a,uint64_t b,int d,int t){return fp79_add(a,b,d,t);}\n')
    subprocess.run(['cc','-shared','-fPIC','-O2','-Wall',str(wrapper),'-o',str(out/'reference.dylib')],check=True,capture_output=True)
    class Result(ctypes.Structure):
        _fields_=[('bits',ctypes.c_uint64),('exponent',ctypes.c_int),('zero',ctypes.c_int)]
    f=ctypes.CDLL(str(out/'reference.dylib')).reference
    f.restype=Result;f.argtypes=[ctypes.c_uint64,ctypes.c_uint64,ctypes.c_int,ctypes.c_int]
    rng=random.Random(0x79dc11);checks=0
    pairs=[]
    for gap in range(255):
        for mant in (0,1,MASK,(1<<32)-1,1<<32):
            for sign in (0,SIGN):
                pairs.append(((255<<55)|mant,sign|((255-gap)<<55)|MASK))
    for i in range(60000):
        a=rng.getrandbits(64);b=rng.getrandbits(64)
        if i%4==0:b=a^SIGN
        elif i%4==1:b=((a^SIGN)+rng.choice((-1,1,-2,2,-(1<<32),1<<32)))&((1<<64)-1)
        pairs.append((a,b))
    for a,b in pairs:
        for d in (0,1):
            for chop in (0,1):
                want=exact(a,b,d,chop);r=f(a,b,d,chop);got=(r.bits,r.exponent,bool(r.zero))
                assert got==want,(hex(a),hex(b),d,chop,got,want)
                checks+=1
    record=dict(passed=True,comparisons=checks,exponent_gaps=255,
                inputs={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (src,ROOT/'tools/test_fpp_math.py')})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))


if __name__=='__main__':run()
