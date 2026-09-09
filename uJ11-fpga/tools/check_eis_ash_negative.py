#!/usr/bin/env python3
"""Reject ASH regressions against the corrected DCJ11 reference (CP22)."""
import subprocess,tarfile,tempfile,shutil
from pathlib import Path
from check_psw_transfer_negative import records
ROOT=Path(__file__).resolve().parents[1]
NAMES=['missing-ash','ignored-high-count','zero-keeps-carry','lost-count-sign','lost-sticky-overflow','logical-right','early-register-capture','flags-before-read']
def main():
 items=list(records('eis_ash'));faults=list(records('eis-ash-fault',True))
 def choose(value,count,flags=0):
  return next(t for h,c,t in items if int(h[1],16)==0o72001 and int(h[2],16)==flags and int(h[3],16)==value and int(h[4],16)==(0xa500|count) and int(c[0],16)==0)
 alias=next(t for h,c,t in items if int(h[1],16)==0o72020 and int(h[2],16)==0 and int(h[3],16)==0x4000 and int(c[0],16)==0 and int(t.splitlines()[2+int(h[11],16)].split()[1],16)==0x4002)
 fault=next(t for h,c,t in faults if int(h[1],16)==0o72010 and int(h[2],16)==3 and int(h[11],16)==1)
 cases=[
  ('missing-ash','cp20c',choose(1,0),False,None),
  ('ignored-high-count','cp22c',next(t for h,c,t in items if int(h[1],16)==0o72001 and int(h[2],16)==0 and int(h[3],16)==1 and int(h[4],16)==0xffc0),False,('imm=63, b=T0','imm=127, b=T0')),
  ('zero-keeps-carry','cp22c',choose(1,0,1),False,('imm=63, b=T0, dst=RF, flags=NZVC','imm=63, b=T0, dst=RF, flags=NZV')),
  ('lost-count-sign','cp22c',choose(0x8000,32),False,('imm=63, b=T0','imm=31, b=T0')),
  ('lost-sticky-overflow','cp22c',choose(0x8000,2),False,('alu OR, a=T6, pair=AD, d=PSW, b=T6, dst=RF','alu PASSA, pair=DA, d=PSW, b=T6, dst=RF')),
  ('logical-right','cp22c',choose(0x8000,63),False,('alu ASR, a=T4, b=T4, dst=RF, flags=NZVC','alu LSR, a=T4, b=T4, dst=RF, flags=NZVC')),
  ('early-register-capture','cp22c',alias,False,('ASH:\n    CALL, target=DESTINATION_EA, prefetch=0\n    alu PASSA, a=RS, b=T4, dst=RF','ASH:\n    alu PASSA, a=RS, b=T4, dst=RF\n    CALL, target=DESTINATION_EA, prefetch=0')),
  ('flags-before-read','cp22c',fault,True,('alu PASSA, a=RS, b=T4, dst=RF','alu PASSA, a=RS, b=T4, dst=RF, flags=NZV'))]
 for name,archive,fixture,is_fault,mutation in cases:
  with tempfile.TemporaryDirectory(prefix='uj11-ash-negative-') as temp:
   root=Path(temp)
   with tarfile.open(ROOT/'synth/reports'/archive/'source.tgz') as a:a.extractall(root,filter='data')
   (root/'tb').mkdir(exist_ok=True);(root/'build').mkdir(exist_ok=True)
   for rel in ['tb/tb_trace_bit.v','tb/tb_bus_fault.v','tb/uj11_ram.v','rtl/uj11_rom.v','reference/lsi11/spi_fram_model.v']:
    (root/rel).parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,root/rel)
   if mutation:
    f=root/'microcode/m0.uasm';before,part=f.read_text().split('; CP22 fixes ASH',1);part,after=part.split('; CP22 ASHC:',1)
    assert part.count(mutation[0])==1,(name,part.count(mutation[0]));f.write_text(before+'; CP22 fixes ASH'+part.replace(*mutation)+'; CP22 ASHC:'+after)
   subprocess.run(['python3','microasm/uj11asm.py','microcode/m0.uasm','-o','microcode/generated/m0.mem'],cwd=root,check=True,stdout=subprocess.DEVNULL)
   stem='eis-ash-fault' if is_fault else 'eis_ash';top='tb_bus_fault' if is_fault else 'tb_trace_bit'
   (root/f'build/{stem}-vectors.txt').write_text(fixture)
   sources=[str(f.relative_to(root)) for f in sorted((root/'rtl').glob('*.v'))]
   subprocess.run(['iverilog','-g2012','-s',top,f'-P{top}.EIS_ASH=1','-o','build/negative',f'tb/{top}.v','tb/uj11_ram.v']+sources+['reference/lsi11/spi_fram_model.v','reference/lsi11/spi_fram_guest_ram.v'],cwd=root,check=True)
   result=subprocess.run(['vvp','build/negative'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
   (ROOT/f'build/cp22-ash-negative-{name}.log').write_text(f'Archive: {archive}; mutation: {mutation!r}\n'+result.stdout)
   marker='fault case' if is_fault else 'beat1 got wr0' if archive=='cp20c' else 'R0 got' if name=='early-register-capture' else 'trace case'
   if not result.returncode or 'FATAL' not in result.stdout or marker not in result.stdout:raise SystemExit(name+': expected failure missing: '+result.stdout)
   print(f'PASS ASH negative: {name}; archived {archive}')
if __name__=='__main__':main()
