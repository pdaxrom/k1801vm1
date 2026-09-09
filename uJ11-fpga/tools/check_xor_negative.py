#!/usr/bin/env python3
"""Require real C fixtures to reject eight deliberate XOR implementation bugs."""
import json,subprocess,tarfile,tempfile,shutil
from pathlib import Path
from check_psw_transfer_negative import records
ROOT=Path(__file__).resolve().parents[1]

def main():
    items=list(records('eis_xor'));faults=list(records('eis-xor-fault',True))
    def rr(a,b,flags=0):
        return next(t for h,c,t in items if int(h[1],16)==0o74001 and int(h[2],16)==flags and int(h[3],16)==a and int(h[4],16)==b and int(c[0],16)==0)
    alias=next(t for h,c,t in items if int(h[1],16)==0o74020 and int(h[2],16)==0 and int(h[3],16)==0x4000 and int(c[0],16)==0)
    failed_write=next(t for h,c,t in faults if int(h[1],16)==0o74010 and int(h[2],16)==3 and int(h[11],16)==2)
    memory=next(t for h,c,t in items if int(h[1],16)==0o74010 and int(h[2],16)==0 and int(c[0],16)==0)
    rr_word='alu XOR, a=RS, b=RD, dst=RF, flags=NZV, seq=FETCH'
    cases=[
      ('missing-opcode','cp23c',rr(1,0),False,[]),
      ('inclusive-or','cp24a',rr(0xffff,0xffff),False,[('microcode/m0.uasm',rr_word,rr_word.replace('alu XOR','alu OR'))]),
      ('wrong-source','cp24a',rr(1,0),False,[('microcode/m0.uasm',rr_word,rr_word.replace('a=RS','a=RD'))]),
      ('lost-carry','cp24a',rr(1,0,1),False,[('microcode/m0.uasm',rr_word,rr_word.replace('flags=NZV','flags=NZVC'))]),
      ('set-overflow','cp24a',rr(1,0),False,[('rtl/uj11_alu.v',"4'd8: result = a ^ b;","4'd8: begin result = a ^ b; v=1; end")]),
      ('early-source','cp24a',alias,False,[
        ('microcode/m0.uasm','XOR_EA:\n    CALL, target=DESTINATION_EA, prefetch=0','XOR_EA:\n    JUMP, target=XOR_EARLY, prefetch=0\n.org $312\nXOR_EARLY:\n    alu PASSA, a=RS, b=T4, dst=RF\n    CALL, target=DESTINATION_EA, prefetch=0'),
        ('microcode/m0.uasm','alu XOR, a=RS, pair=AD, d=MDR, b=T2, dst=RF','alu XOR, a=T4, pair=AD, d=MDR, b=T2, dst=RF')]),
      ('flags-before-write','cp24a',failed_write,True,[('microcode/m0.uasm','alu XOR, a=RS, pair=AD, d=MDR, b=T2, dst=RF','alu XOR, a=RS, pair=AD, d=MDR, b=T2, dst=RF, flags=NZV')]),
      ('byte-write','cp24a',memory,False,[('microcode/m0.uasm','WRITE, a=T1, b=T2, target=BIS_MEMORY_FLAGS','WRITE, a=T1, b=T2, byte=1, target=BIS_MEMORY_FLAGS')]),
    ]
    results=[]
    for name,archive,fixture,fault,mutations in cases:
        with tempfile.TemporaryDirectory(prefix='uj11-xor-negative-') as temp:
            root=Path(temp)
            with tarfile.open(ROOT/'synth/reports'/archive/'source.tgz') as a:a.extractall(root,filter='data')
            (root/'build').mkdir(exist_ok=True)
            for rel in ['tb/tb_trace_bit.v','tb/tb_bus_fault.v','tb/uj11_ram.v','rtl/uj11_rom.v','rtl/uj11_stream.v','rtl/uj11_prefetch_control.v','reference/lsi11/spi_fram_model.v','reference/lsi11/spi_fram_guest_ram.v']:
                (root/rel).parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,root/rel)
            for rel,before,after in mutations:
                p=root/rel;s=p.read_text();assert s.count(before)==1,(name,before);p.write_text(s.replace(before,after))
            subprocess.run(['python3','microasm/uj11asm.py','microcode/m0.uasm','-o','microcode/generated/m0.mem'],cwd=root,check=True,stdout=subprocess.DEVNULL)
            stem='eis-xor-fault' if fault else 'eis_xor';top='tb_bus_fault' if fault else 'tb_trace_bit'
            (root/f'build/{stem}-vectors.txt').write_text(fixture)
            sources=[str(p.relative_to(root)) for p in sorted((root/'rtl').glob('*.v'))]
            subprocess.run(['iverilog','-g2012','-s',top,f'-P{top}.EIS_XOR=1','-o','build/negative',f'tb/{top}.v','tb/uj11_ram.v']+sources+['reference/lsi11/spi_fram_model.v','reference/lsi11/spi_fram_guest_ram.v'],cwd=root,check=True)
            r=subprocess.run(['vvp','build/negative'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            (ROOT/f'build/cp24-negative-{name}.log').write_text(f'Archive: {archive}; mutations: {mutations!r}\n'+r.stdout)
            assert r.returncode and 'FATAL' in r.stdout and any(m in r.stdout for m in ['trace case','fault case','beat1 got','beat2 got']),(name,r.stdout)
            results.append(dict(name=name,archive=archive,rejected=True))
            print('PASS XOR negative:',name)
    (ROOT/'build/cp24-negative-controls.json').write_text(json.dumps(results,indent=2)+'\n')
if __name__=='__main__':main()
