#!/usr/bin/env python3
"""Require real C fixtures to reject fifteen deliberate DIV implementation bugs."""
import json,subprocess,tarfile,tempfile,shutil
from pathlib import Path
from check_psw_transfer_negative import records
ROOT=Path(__file__).resolve().parents[1]

def main():
    items=list(records('eis_div'));faults=list(records('eis-div-fault',True))
    def rr(x,y,odd=False,flags=0):
        return next(t for h,c,t in items if int(h[1],16)==(0o71102 if odd else 0o71002) and int(h[2],16)==flags and int(h[4],16)==(x&65535) and (odd or int(h[3],16)==x>>16) and int(h[5],16)==y and int(c[0],16)==0)
    alias=next(t for h,c,t in items if int(h[1],16)==0o71021 and int(h[2],16)==0 and int(h[3],16)==0 and int(c[0],16)==0)
    failed_read=next(t for h,c,t in faults if int(h[1],16)==0o71010 and int(h[2],16)==3 and int(h[11],16)==1)
    def mutation(before,after):return [('microcode/m0.uasm',before,after)]
    cases=[
      ('missing-opcode','cp25a',rr(1,1),False,[]),
      ('fifteen-iterations','cp26a',rr(0x7fff,1),False,mutation('d=IMM, imm=16, b=T1, dst=RF\n    alu PASSA, pair=DA, d=ONE, b=T5','d=IMM, imm=15, b=T1, dst=RF\n    alu PASSA, pair=DA, d=ONE, b=T5')),
      ('unsigned-dividend','cp26a',rr(0xffff8000,1),False,mutation('CJUMP, cond=NOT_N, target=DIV_NORMALIZED','JUMP, target=DIV_NORMALIZED')),
      ('lost-negation-borrow','cp26a',rr(0xffff8000,1),False,mutation('alu SBC, pair=ZB, b=T4, dst=RF','alu SUB, pair=ZB, b=T4, dst=RF')),
      ('unsigned-divisor','cp26a',rr(0x7fff,0xffff),False,mutation('CJUMP, cond=NOT_N, target=DIV_MAGNITUDE','JUMP, target=DIV_MAGNITUDE')),
      ('lost-quotient-bit','cp26a',rr(0x7fff,1),False,mutation('alu OR, a=T5, pair=AQ, dst=Q','alu PASSB, a=T5, pair=AQ, dst=Q')),
      ('wrong-quotient-sign','cp26a',rr(0xffffffff,1),False,mutation('DIV_NEGATIVE_QUOTIENT:\n    alu SUB, pair=ZQ','DIV_NEGATIVE_QUOTIENT:\n    alu PASSB, pair=ZQ')),
      ('wrong-remainder-sign','cp26a',rr(0xffffffff,2),False,mutation('CJUMP, cond=NOT_N, target=DIV_COMMIT','JUMP, target=DIV_COMMIT')),
      ('positive-overflow-write','cp26a',rr(0x8000,1),False,mutation('DIV_POSITIVE_QUOTIENT:\n    alu PASSB, pair=ZQ, flags=NZVC\n    CJUMP, cond=N, target=DIV_OVERFLOW','DIV_POSITIVE_QUOTIENT:\n    alu PASSB, pair=ZQ, flags=NZVC\n    JUMP, target=DIV_REMAINDER')),
      ('reject-minus-32768','cp26a',rr(0xffff8000,1),False,mutation('CJUMP, cond=N, target=DIV_REMAINDER','CJUMP, cond=N, target=DIV_OVERFLOW')),
      ('zero-divisor-flags','cp26a',rr(1,0),False,mutation('imm=7, b=T5, dst=RF','imm=3, b=T5, dst=RF')),
      ('negative-overflow-flags','cp26a',rr(0xffff7fff,1),False,mutation('imm=10, b=T5, dst=RF','imm=2, b=T5, dst=RF')),
      ('odd-quotient-overwrites-remainder','cp26a',rr(1,0x7fff,odd=True),False,mutation('alu PASSB, pair=ZQ, b=RS, dst=RF, flags=NZVC','alu PASSA, a=T4, b=RS1, dst=RF')+mutation('alu PASSA, a=T4, b=RS1, dst=RF, seq=FETCH','alu PASSB, pair=ZQ, b=RS, dst=RF, flags=NZVC, seq=FETCH')),
      ('early-low-dividend','cp26a',alias,False,mutation('DIV_INIT:\n    alu PASSA, a=RS, b=T4, dst=RF\n    alu PASSA, a=RS1, dst=Q','DIV_INIT:\n    alu PASSA, a=RS, b=T4, dst=RF\n    alu SUB, a=RS1, pair=AD, d=TWO, dst=Q')),
      ('flags-on-failed-read','cp26a',failed_read,True,mutation('DIV_MEMORY:\n    CALL, target=DESTINATION_EA, prefetch=0','DIV_MEMORY:\n    JUMP, target=DIV_EARLY_FLAGS, prefetch=0')+mutation('; CP26 DIV:', '.org $045\nDIV_EARLY_FLAGS:\n    alu PASSA, a=RS, flags=NZVC\n    CALL, target=DESTINATION_EA, prefetch=0\n    JUMP, target=$077, prefetch=0\n\n; CP26 DIV:')),
    ]
    results=[]
    for name,archive,fixture,fault,mutations in cases:
        with tempfile.TemporaryDirectory(prefix='uj11-div-negative-') as temp:
            root=Path(temp)
            with tarfile.open(ROOT/'synth/reports'/archive/'source.tgz') as a:a.extractall(root,filter='data')
            (root/'build').mkdir(exist_ok=True)
            for rel in ['tb/tb_trace_bit.v','tb/tb_bus_fault.v','tb/uj11_ram.v','rtl/uj11_rom.v','rtl/uj11_stream.v','rtl/uj11_prefetch_control.v','reference/lsi11/spi_fram_model.v','reference/lsi11/spi_fram_guest_ram.v']:
                (root/rel).parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,root/rel)
            for rel,before,after in mutations:
                p=root/rel;s=p.read_text();assert s.count(before)==1,(name,before);p.write_text(s.replace(before,after))
            subprocess.run(['python3','microasm/uj11asm.py','microcode/m0.uasm','-o','microcode/generated/m0.mem'],cwd=root,check=True,stdout=subprocess.DEVNULL)
            stem='eis-div-fault' if fault else 'eis_div';top='tb_bus_fault' if fault else 'tb_trace_bit'
            (root/f'build/{stem}-vectors.txt').write_text(fixture)
            sources=[str(p.relative_to(root)) for p in sorted((root/'rtl').glob('*.v'))]
            subprocess.run(['iverilog','-g2012','-s',top,f'-P{top}.EIS_DIV=1','-o','build/negative',f'tb/{top}.v','tb/uj11_ram.v']+sources+['reference/lsi11/spi_fram_model.v','reference/lsi11/spi_fram_guest_ram.v'],cwd=root,check=True)
            r=subprocess.run(['vvp','build/negative'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            (ROOT/f'build/cp26-negative-{name}.log').write_text(f'Archive: {archive}; mutations: {mutations!r}\n'+r.stdout)
            assert r.returncode and 'FATAL' in r.stdout and any(m in r.stdout for m in ['trace case','fault case','beat1 got','beat2 got']),(name,r.stdout)
            results.append(dict(name=name,archive=archive,rejected=True))
            print('PASS DIV negative:',name)
    (ROOT/'build/cp26-negative-controls.json').write_text(json.dumps(results,indent=2)+'\n')
if __name__=='__main__':main()
