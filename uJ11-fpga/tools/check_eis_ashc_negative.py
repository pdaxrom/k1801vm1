#!/usr/bin/env python3
"""Require actual DCJ11 fixtures to reject ASHC regressions in frozen fit sources."""
import subprocess,tarfile,tempfile,shutil
from pathlib import Path
from check_psw_transfer_negative import records
ROOT=Path(__file__).resolve().parents[1]
NAMES=['missing-ashc','ignored-high-count','zero-keeps-carry','lost-count-sign','lost-sticky-overflow','lost-q-left','wrong-right-carry','odd-pair-nz','early-high-capture','early-low-capture','flags-before-read','odd-pair-z','ash-early-capture']
def main():
 items=list(records('eis_ashc'));faults=list(records('eis-ashc-fault',True))
 def choose(value,count,flags=0):
  return next(t for h,c,t in items if int(h[1],16)==0o73002 and int(h[2],16)==flags and (int(h[3],16)<<16|int(h[4],16))==value and int(h[5],16)==(0xa500|count) and int(c[0],16)==0)
 high_alias=next(t for h,c,t in items if int(h[1],16)==0o73020 and int(h[2],16)==0 and int(h[3],16)==0x4000 and int(c[0],16)==0 and int(t.splitlines()[-1].split()[2],16)==0)
 low_alias=next(t for h,c,t in items if int(h[1],16)==0o73021 and int(h[2],16)==0 and int(h[3],16)==0x8001 and int(h[4],16)==0x4000 and int(c[0],16)==0)
 odd=next(t for h,c,t in items if int(h[1],16)==0o73102 and int(h[2],16)==0 and int(h[4],16)==0x8000 and int(h[5],16)==0xa53f)
 odd_z=next(t for h,c,t in items if int(h[1],16)==0o73102 and int(h[2],16)==0 and int(h[4],16)==1 and int(h[5],16)==0xa510)
 ash_alias=next(t for h,c,t in records('eis_ash') if int(h[1],16)==0o72020 and int(h[2],16)==0 and int(h[3],16)==0x4000 and int(c[0],16)==0 and int(t.splitlines()[-1].split()[2],16)==0)
 fault=next(t for h,c,t in faults if int(h[1],16)==0o73010 and int(h[2],16)==3 and int(h[11],16)==1)
 cases=[
  ('missing-ashc','cp21a',choose(1,0),False,None),
  ('ignored-high-count','cp22c',next(t for h,c,t in items if int(h[1],16)==0o73002 and int(h[2],16)==0 and int(h[3],16)==0 and int(h[4],16)==1 and int(h[5],16)==0xffc0),False,('imm=63, b=T0','imm=127, b=T0')),
  ('zero-keeps-carry','cp22c',choose(1,0,1),False,('imm=63, b=T0, dst=RF, flags=NZVC','imm=63, b=T0, dst=RF, flags=NZV')),
  ('lost-count-sign','cp22c',choose(0x80000000,32),False,('imm=63, b=T0','imm=31, b=T0')),
  ('lost-sticky-overflow','cp22c',choose(0x80000000,2),False,('alu OR, a=T6, pair=AD, d=PSW, b=T6, dst=RF','alu PASSA, pair=DA, d=PSW, b=T6, dst=RF')),
  ('lost-q-left','cp22c',choose(0x8000,1),False,('dst=RFQ_L','dst=RF')),
  ('wrong-right-carry','cp22c',choose(0x10000,63),False,('alu LSR, a=T5, flags=NZVC','alu LSR, a=T4, flags=NZVC')),
  ('odd-pair-nz','cp22c',odd,False,('alu PASSA, a=T4, flags=NZV','alu PASSA, a=RS, flags=NZV')),
  ('early-high-capture','cp22c',high_alias,False,('CALL, target=DESTINATION_EA, prefetch=0\n    alu PASSA, a=RS, b=T4, dst=RF\n    alu PASSA, a=RS1, dst=Q','alu PASSA, a=RS, b=T4, dst=RF\n    CALL, target=DESTINATION_EA, prefetch=0\n    alu PASSA, a=RS1, dst=Q')),
  ('early-low-capture','cp22c',low_alias,False,('CALL, target=DESTINATION_EA, prefetch=0\n    alu PASSA, a=RS, b=T4, dst=RF\n    alu PASSA, a=RS1, dst=Q','alu PASSA, a=RS1, dst=Q\n    CALL, target=DESTINATION_EA, prefetch=0\n    alu PASSA, a=RS, b=T4, dst=RF')),
  ('flags-before-read','cp22c',fault,True,('alu PASSA, a=RS, b=T4, dst=RF','alu PASSA, a=RS, b=T4, dst=RF, flags=NZV')),
  ('odd-pair-z','cp22c',odd_z,False,('alu OR, a=T4, pair=AQ, flags=NZV','alu OR, a=RS, b=RS1, flags=NZV')),
  ('ash-early-capture','cp21a',ash_alias,False,None)]
 for name,archive,fixture,is_fault,mutation in cases:
  with tempfile.TemporaryDirectory(prefix='uj11-ashc-negative-') as temp:
   root=Path(temp)
   with tarfile.open(ROOT/'synth/reports'/archive/'source.tgz') as a:a.extractall(root,filter='data')
   (root/'tb').mkdir(exist_ok=True);(root/'build').mkdir(exist_ok=True)
   for rel in ['tb/tb_trace_bit.v','tb/tb_bus_fault.v','tb/uj11_ram.v','rtl/uj11_rom.v','reference/lsi11/spi_fram_model.v']:
    (root/rel).parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,root/rel)
   if mutation:
    f=root/'microcode/m0.uasm';before,part=f.read_text().split('; CP22 ASHC:',1)
    assert part.count(mutation[0])==1,(name,part.count(mutation[0]));f.write_text(before+'; CP22 ASHC:'+part.replace(*mutation))
   subprocess.run(['python3','microasm/uj11asm.py','microcode/m0.uasm','-o','microcode/generated/m0.mem'],cwd=root,check=True,stdout=subprocess.DEVNULL)
   is_ash=name=='ash-early-capture'
   stem='eis_ash' if is_ash else 'eis-ashc-fault' if is_fault else 'eis_ashc';top='tb_bus_fault' if is_fault else 'tb_trace_bit'
   (root/f'build/{stem}-vectors.txt').write_text(fixture)
   sources=[str(f.relative_to(root)) for f in sorted((root/'rtl').glob('*.v'))]
   subprocess.run(['iverilog','-g2012','-s',top,f'-P{top}.EIS_ASH=1' if is_ash else f'-P{top}.EIS_ASHC=1','-o','build/negative',f'tb/{top}.v','tb/uj11_ram.v']+sources+['reference/lsi11/spi_fram_model.v','reference/lsi11/spi_fram_guest_ram.v'],cwd=root,check=True)
   result=subprocess.run(['vvp','build/negative'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
   (ROOT/f'build/cp22-negative-{name}.log').write_text(f'Archive: {archive}; ASHC-only mutation: {mutation!r}\n'+result.stdout)
   marker='fault case' if is_fault else 'beat1 got wr0' if name=='missing-ashc' else 'R0 got' if name in ['early-high-capture','ash-early-capture'] else 'R1 got' if name=='early-low-capture' else 'trace case'
   if not result.returncode or 'FATAL' not in result.stdout or marker not in result.stdout:raise SystemExit(name+': expected failure missing: '+result.stdout)
   print(f'PASS ASHC negative: {name}; archived {archive}')
if __name__=='__main__':main()
