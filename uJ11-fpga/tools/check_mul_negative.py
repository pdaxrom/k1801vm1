#!/usr/bin/env python3
"""Require real C fixtures to reject twelve deliberate MUL implementation bugs."""
import json,subprocess,tarfile,tempfile,shutil
from pathlib import Path
from check_psw_transfer_negative import records
ROOT=Path(__file__).resolve().parents[1]

def main():
    items=list(records('eis_mul'));faults=list(records('eis-mul-fault',True))
    def rr(a,b,flags=0,odd=False):
        return next(t for h,c,t in items if int(h[1],16)==(0o70102 if odd else 0o70002) and int(h[2],16)==flags and int(h[4 if odd else 3],16)==a and int(h[5],16)==b and int(c[0],16)==0)
    alias=next(t for h,c,t in items if int(h[1],16)==0o70020 and int(h[2],16)==0 and int(h[3],16)==0x4000 and int(c[0],16)==0)
    failed_read=next(t for h,c,t in faults if int(h[1],16)==0o70010 and int(h[2],16)==3 and int(h[11],16)==1)
    def mutation(before,after):return [('microcode/m0.uasm',before,after)]
    cases=[
      ('missing-opcode','cp24a',rr(1,0),False,[]),
      ('fifteen-iterations','cp25a',rr(2,0x4000),False,mutation('d=IMM, imm=16, b=T1','d=IMM, imm=15, b=T1')),
      ('unsigned-multiplicand','cp25a',rr(2,0x8000),False,mutation('CJUMP, cond=NOT_N, target=MUL_LOOP','JUMP, target=MUL_LOOP')),
      ('unsigned-register','cp25a',rr(0x8000,1),False,mutation('CJUMP, cond=NOT_N, target=MUL_FLAGS','JUMP, target=MUL_FLAGS')),
      ('lost-low-carry','cp25a',rr(0x7fff,0x7fff),False,mutation('alu ADC, a=T5, b=T4, dst=RF','alu ADD, a=T5, b=T4, dst=RF')),
      ('lost-overflow-carry','cp25a',rr(2,0x4000),False,mutation('CJUMP, cond=Z, target=MUL_NZ','JUMP, target=MUL_NZ')),
      ('set-carry-at-32767','cp25a',rr(1,0x7fff),False,mutation('MUL_CARRY_COMPARE:\n    alu SUB, a=T4, b=T5, flags=NZVC','MUL_CARRY_COMPARE:\n    alu SUB, a=T0, b=T5, flags=NZVC')),
      ('low-word-negative','cp25a',rr(0x8000,0xffff),False,mutation('MUL_NZ:\n    alu PASSA, a=T4, flags=NZVC','MUL_NZ:\n    alu PASSB, pair=ZQ, flags=NZVC')),
      ('low-word-zero','cp25a',rr(2,0x8000),False,mutation('alu OR, a=T4, pair=AQ, flags=NZV\n    alu PASSA, pair=DA, d=PSW, b=T5, dst=RF, seq=PAGE, next=MUL_COMMIT','alu PASSB, pair=ZQ, flags=NZV\n    alu PASSA, pair=DA, d=PSW, b=T5, dst=RF, seq=PAGE, next=MUL_COMMIT')),
      ('odd-high-overwrites-low','cp25a',rr(2,0x4000,odd=True),False,mutation('alu PASSA, a=T4, b=RS, dst=RF\n    alu PASSB, pair=ZQ, b=RS1, dst=RF, seq=FETCH','alu PASSB, pair=ZQ, b=RS1, dst=RF\n    alu PASSA, a=T4, b=RS, dst=RF, seq=FETCH')),
      ('early-register','cp25a',alias,False,mutation('MUL_INIT:\n    alu PASSA, a=RS, b=T0, dst=RF','MUL_INIT:\n    alu SUB, a=RS, pair=AD, d=TWO, b=T0, dst=RF')),
      ('flags-on-failed-read','cp25a',failed_read,True,mutation('MUL_MEMORY:\n    CALL, target=DESTINATION_EA, prefetch=0','MUL_MEMORY:\n    JUMP, target=MUL_EARLY_FLAGS, prefetch=0') + mutation('; CP25 MUL:', '.org $045\nMUL_EARLY_FLAGS:\n    alu PASSA, a=RS, flags=NZVC\n    CALL, target=DESTINATION_EA, prefetch=0\n    JUMP, target=$067, prefetch=0\n\n; CP25 MUL:')),
    ]
    results=[]
    for name,archive,fixture,fault,mutations in cases:
        with tempfile.TemporaryDirectory(prefix='uj11-mul-negative-') as temp:
            root=Path(temp)
            with tarfile.open(ROOT/'synth/reports'/archive/'source.tgz') as a:a.extractall(root,filter='data')
            (root/'build').mkdir(exist_ok=True)
            for rel in ['tb/tb_trace_bit.v','tb/tb_bus_fault.v','tb/uj11_ram.v','rtl/uj11_rom.v','rtl/uj11_stream.v','rtl/uj11_prefetch_control.v','reference/lsi11/spi_fram_model.v','reference/lsi11/spi_fram_guest_ram.v']:
                (root/rel).parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,root/rel)
            for rel,before,after in mutations:
                p=root/rel;s=p.read_text();assert s.count(before)==1,(name,before);p.write_text(s.replace(before,after))
            subprocess.run(['python3','microasm/uj11asm.py','microcode/m0.uasm','-o','microcode/generated/m0.mem'],cwd=root,check=True,stdout=subprocess.DEVNULL)
            stem='eis-mul-fault' if fault else 'eis_mul';top='tb_bus_fault' if fault else 'tb_trace_bit'
            (root/f'build/{stem}-vectors.txt').write_text(fixture)
            sources=[str(p.relative_to(root)) for p in sorted((root/'rtl').glob('*.v'))]
            subprocess.run(['iverilog','-g2012','-s',top,f'-P{top}.EIS_MUL=1','-o','build/negative',f'tb/{top}.v','tb/uj11_ram.v']+sources+['reference/lsi11/spi_fram_model.v','reference/lsi11/spi_fram_guest_ram.v'],cwd=root,check=True)
            r=subprocess.run(['vvp','build/negative'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            (ROOT/f'build/cp25-negative-{name}.log').write_text(f'Archive: {archive}; mutations: {mutations!r}\n'+r.stdout)
            assert r.returncode and 'FATAL' in r.stdout and any(m in r.stdout for m in ['trace case','fault case','beat1 got','beat2 got']),(name,r.stdout)
            results.append(dict(name=name,archive=archive,rejected=True))
            print('PASS MUL negative:',name)
    (ROOT/'build/cp25-negative-controls.json').write_text(json.dumps(results,indent=2)+'\n')
if __name__=='__main__':main()
