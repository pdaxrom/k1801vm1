#!/usr/bin/env python3
"""Directed native instructions against DEC J11 PSW rules, in both FRAM banks."""
import argparse,hashlib,json,re,subprocess
from board_common import ROOT
from build_psw_cp78 import adapt,OUT


def vectors():
    rows=[]
    def add(bank,old,r0,ops,expected_r0,expected_psw,trap=0,irq=0):
        rows.append(f'{bank} {old:04x} {r0:04x} {len(ops)} '+
                    ' '.join(f'{v:04x}' for v in (ops+[0]*3)[:3])+
                    f' {expected_r0:04x} {expected_psw:04x} {trap:04x} {irq}')
    def write(old,value,byte=False,odd=False):
        # Explicit destination value replaces flags; T is protected (table 1-3).
        if byte:value=((old&255)|(value&255)<<8) if odd else ((old&0xff00)|(value&255))
        return ((value&~16)|(old&16))&0xf9ff
    for bank in (0,1):
        for old in (0,1,0xf,0xe0,0xef,0x10,0x1f,0xff,0xf900,0xffff):
            ps=old&0xf9ff
            # MOV/MOVB return pre-instruction PS; flags are ordinary MOV flags.
            for byte,odd in ((False,False),(True,False),(True,True)):
                value=(ps>>(8 if odd else 0))&255 if byte else ps
                rvalue=value|(0xff00 if byte and value&128 else 0)
                nz=(8 if value&(128 if byte else 32768) else 0)|(4 if value==0 else 0)
                add(bank,old,0x5555,[0o13700+(0o100000 if byte else 0),0o177776+odd],rvalue,(ps&~14)|nz)
                for val in (0,1,15,16,31,0x80,0xff,0x100,0x7fff,0x8000,0xffff):
                    add(bank,old,val,[0o10037+(0o100000 if byte else 0),0o177776+odd],val,write(ps,val,byte,odd))
            for byte in (False,True):
                width=8 if byte else 16;mask=(1<<width)-1;dest=ps&mask
                for name,base,fn in [
                    ('CLR',0o5000,lambda a:0),('COM',0o5100,lambda a:~a),
                    ('INC',0o5200,lambda a:a+1),('DEC',0o5300,lambda a:a-1),
                    ('NEG',0o5400,lambda a:-a),('ADC',0o5500,lambda a:a+(ps&1)),
                    ('SBC',0o5600,lambda a:a-(ps&1)),
                    ('ROR',0o6000,lambda a:(a>>1)|((ps&1)<<(width-1))),
                    ('ROL',0o6100,lambda a:(a<<1)|(ps&1)),
                    ('ASR',0o6200,lambda a:(a>>1)|(a&(1<<(width-1)))),
                    ('ASL',0o6300,lambda a:a<<1)]:
                    value=fn(dest)&mask
                    add(bank,old,0x5555,[base+0o37+(0o100000 if byte else 0),0o177776],0x5555,write(ps,value,byte))
                for base,fn in [(0o40000,lambda a,b:b&~a),(0o50000,lambda a,b:a|b)]:
                    for src in (1,0x1f,0x80,0xffff):
                        value=fn(src&mask,dest)&mask
                        add(bank,old,src,[base+0o37+(0o100000 if byte else 0),0o177776],src,write(ps,value,byte))
            for base,fn in ((0o60000,lambda a,b:a+b),(0o160000,lambda a,b:b-a)):
                for src in (1,0x1f,0x8000,0xffff):
                    add(bank,old,src,[base+0o37,0o177776],src,write(ps,fn(src,ps)&65535))
            add(bank,old,0x5555,[0o337,0o177776],0x5555,write(ps,((ps&255)<<8)|(ps>>8))) # SWAB
            add(bank,old,0x5555,[0o12737,0x123f,0o177776],0x5555,write(ps,0x123f))
            # Adjacent unmapped register and odd word retain vector-004 behavior.
            for address in (0o177774,0o177777):
                add(bank,old,0x5555,[0o13700,address],0x5555,0o340,trap=4)
        # Lowering IPL via MMIO takes an already pending interrupt immediately;
        # keeping IPL7 defers it. With T set, trace wins over IRQ.
        for old,value in ((0o340,0),(0o340,0o357),(0o360,0)):
            if bank==0:add(bank,old,value,[0o10037,0o177776],value,write(old,value),irq=1)
    for old in (0,0o357,0xffff):
        ps=old&0xf9ff
        for op,value in ((0o40,0x2468),(0o44,0x1357),(0o21,ps)):
            add(1,old,0xa55a,[op],value,ps)
        for op in (0o41,0o45):add(1,old,0xa55a,[op],0xa55a,ps)
        add(1,old,0xa55a,[0o31],0xa55a,write(ps,0xa55a))
    return '\n'.join(rows)+'\n'


def run(vendor=False,decode=1,aligned=1):
    core,_=adapt();out=ROOT/'build/cp78-tests'/f'{"vendor" if vendor else "rtl"}-d{decode}-a{aligned}'
    out.mkdir(parents=True,exist_ok=True);(out/'vectors.txt').write_text(vectors())
    sources=['tb/tb_psw_cp78.v']+core
    if vendor:
        sources+=[str((OUT/'uj11_m0_ebr.v').relative_to(ROOT))]+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')]
        cmd=['iverilog','-g2012','-DUJ11_VENDOR_ROM','-s','tb_psw_cp78',f'-Ptb_psw_cp78.ROM_DECODE={decode}',f'-Ptb_psw_cp78.ALIGNED_WORD_READS={aligned}','-o',str(out/'sim')]+sources
        sim=['vvp',str(out/'sim')]
    else:
        sources+=['rtl/uj11_rom.v']
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','--top-module','tb_psw_cp78',f'-GROM_DECODE={decode}',f'-GALIGNED_WORD_READS={aligned}','-j','4','--Mdir',str(out/'obj')]+sources
        sim=[str(out/'obj/Vtb_psw_cp78')]
    paths=sources+['tools/test_psw_cp78.py','tools/build_psw_cp78.py',str((out/'vectors.txt').relative_to(ROOT)),str((OUT/'inputs.json').relative_to(ROOT))]
    inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    (out/'inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:r=subprocess.run(sim+[f'+VECTORS={out}/vectors.txt'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    text=(out/'simulation.log').read_text();print(text[-1500:],flush=True);r.check_returncode()
    m=re.search(r'PASS CP78 PSW: (\d+) cases (\d+) checks (\d+) clocks',text);assert m
    for p,h in inputs.items():assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h,p
    (out/'result.json').write_text(json.dumps(dict(passed=True,cases=int(m[1]),checks=int(m[2]),clocks=int(m[3]),vendor=vendor,decode=decode,aligned=aligned,inputs=inputs),indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--vendor',action='store_true');p.add_argument('--decode',type=int,choices=(0,1),default=1);p.add_argument('--aligned',type=int,choices=(0,1),default=1);a=p.parse_args();run(a.vendor,a.decode,a.aligned)
