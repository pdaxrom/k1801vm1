#!/usr/bin/env python3
"""Check private C conversion semantics against exact Python Fraction values."""
import ctypes
import hashlib
import json
import random
import subprocess
from fractions import Fraction
from board_common import ROOT
from build_fpp import OUT
from fpp_reference import prepare
from fp_numbers import value
from fp_numbers import pack


def model(op,a,i,fps):
    d=bool(fps&0o200);long=bool(fps&0o100);chop=bool(fps&0o40)
    fec=0;v=0;cpu=15
    if op==0: # STEXP
        bits=((a>>55&255)-128)&65535
        flags=(8 if bits&32768 else 0)|(4 if not bits else 0)
        return bits,flags,0,flags
    if op==1: # STCfi
        n=int(value(a if d else a&0xffffffff00000000));width=32 if long else 16
        bad=not -(1<<(width-1))<=n<(1<<(width-1))
        if bad:n=0;fec=6 if fps&0o400 else 0
        flags=(8 if n<0 else 0)|(4 if n==0 else 0)|int(bad)
        return n&((1<<width)-1),flags,fec,flags
    if op==2 or op==5: # STCff / LDCff
        srcd=d if op==2 else not d;destd=not srcd
        raw=a if srcd else a&0xffffffff00000000
        bits,e,z=pack(value(raw),destd,chop)
        if e>255:
            v=2
            if fps&0o1000:fec=8
            else:bits=0
        flags=(8 if bits>>63 else 0)|(4 if not (bits>>55&255) else 0)|v
        return bits,flags,fec,cpu
    if op==3: # LDEXP, exact exponent replacement, signed source
        i=(i&65535)-(65536 if i&32768 else 0)
        bits=(a&~(255<<55))|(((i+128)&255)<<55)
        if not d:bits&=0xffffffff00000000
        if i>127:
            v=2
            if fps&0o1000:fec=8
            else:bits=0
        if i<=-128:
            if fps&0o2000:fec=10
            else:bits=0
    else: # LDCif
        width=32 if long else 16;i&=(1<<width)-1
        if i>>(width-1):i-=1<<width
        bits,_,_=pack(Fraction(i),d,chop)
    flags=(8 if bits>>63 else 0)|(4 if not (bits>>55&255) else 0)|v
    return bits,flags,fec,cpu


def run():
    out=OUT/'conversions';reference=out/'reference';adaptation=prepare(reference)
    wrapper=out/'reference.c'
    wrapper.write_text('#include "'+str(reference/'core.c')+'"\n'+r'''
static byte m[65536];
static word rd(regs *r,word a){return m[a]|((word)m[(word)(a+1)]<<8);}
static void wr(regs *r,word a,word v){m[a]=v;m[(word)(a+1)]=v>>8;}
void convert(unsigned op,uint64_t a,uint32_t input,unsigned fps,uint64_t *out){
    regs r={0};hwstub_set_memory(m,sizeof m);r.model=DCJ11;
    hwstub_connect(&r);core_init(&r);core_reset(&r);
    r.ram_fast=NULL;r.load_word=rd;r.store_word=wr;
    r.r[7]=01002;r.r[2]=020000;r.r[6]=030000;r.psw=0340|15;
    r.fpu_fps=fps|FPS_ID;r.fpu_fec=0;
    r.fpu_fr[2].h=a>>32;r.fpu_fr[2].l=a;
    for(int k=0;k<4;k++)wr(&r,020000+2*k,a>>(48-16*k));
    if(op==3 || op==4){
        wr(&r,020000,(op==4 && (fps&FPS_L))?input>>16:input);
        wr(&r,020002,input);
    }
    fp11(&r,0175000+0400*op+0200+012);
    if(op==0)out[0]=rd(&r,020000);
    else if(op==1)out[0]=(fps&FPS_L)?((uint32_t)rd(&r,020000)<<16)|rd(&r,020002):rd(&r,020000);
    else if(op==2){
        out[0]=0;for(int k=0;k<4;k++)out[0]=(out[0]<<16)|rd(&r,020000+2*k);
        if(fps&FPS_D)out[0]&=0xffffffff00000000ULL;
    }else{
        out[0]=((uint64_t)r.fpu_fr[2].h<<32)|r.fpu_fr[2].l;
        if(!(fps&FPS_D))out[0]&=0xffffffff00000000ULL;
    }
    out[1]=r.fpu_fps&15;out[2]=r.fpu_fec;out[3]=r.psw&15;
    core_fini(&r);hwstub_clear_memory_binding();
}
''')
    subprocess.run(['cc','-shared','-fPIC','-O2','-DENABLE_MMU=0','-I'+str(reference),'-include',str(reference/'hardware.h'),str(wrapper),str(reference/'hardware.c'),'-lm','-o',str(out/'reference.dylib')],check=True,capture_output=True)
    lib=ctypes.CDLL(str(out/'reference.dylib'));f=lib.convert;f.restype=None
    f.argtypes=[ctypes.c_uint,ctypes.c_uint64,ctypes.c_uint32,ctypes.c_uint,ctypes.POINTER(ctypes.c_uint64)]
    got=(ctypes.c_uint64*4)();checks=[0]*6;rng=random.Random(0x76f11a)
    def check(op,a,i,fps):
        want=model(op,a,i,fps);f(op,a,i,fps,got)
        assert tuple(got)==want,(op,hex(a),hex(i),oct(fps),want,tuple(got))
        checks[op]+=1
    for n in range(20000):
        a=rng.getrandbits(64);i=rng.getrandbits(32);fps=rng.randrange(1<<12)&~0o4000
        for op in range(6):check(op,a,i,fps)
    # Every 16-bit exponent operand, including wrapped signed boundaries.
    for i in range(65536):check(3,0xc14123456789abcd,i,(0o200 if i&1 else 0)|((i&3)<<9))
    for e in range(256):check(0,(e<<55)|0x804123456789abcd,0,0)
    for i in range(65536):check(4,0,i,0o200 if i&1 else 0)
    paths=[ROOT/'tools/test_fpp_conversions.py',ROOT/'tools/fp_numbers.py',ROOT/'tools/fp_numbers.py',ROOT/'tools/fpp_reference.py',wrapper]
    paths += [ROOT/p for p in adaptation['original']]+[ROOT/p for p in adaptation['adapted']]
    result=dict(passed=True,comparisons=sum(checks),per_family=checks,exhaustive_exponents=65536,exhaustive_short_integers=65536,
                inputs={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths})
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':run()
