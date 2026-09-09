#!/usr/bin/env python3
"""Reject concrete HALT/RESET regressions using archived synthesis sources."""
import subprocess,tarfile,tempfile,shutil
from pathlib import Path
from check_psw_transfer_negative import records
ROOT=Path(__file__).resolve().parents[1]
def main():
 items=list(records('system_control'))
 def choose(op,psw=0,odd=False):
  return next(t for h,c,t in items if int(h[1],16)==op and int(h[2],16)==psw and (odd or int(c[0],16)==0) and (not odd or any(l.startswith('0004 ') and int(l.split()[1],16)&1 for l in t.splitlines())))
 cases=[
  ('missing-reset','cp19a',choose(5),None),
  ('unregistered-reset','cp20a',choose(5),None),
  ('reset-no-pulse','cp20c',choose(5),('target=RESET_SETTLE, init=1','target=RESET_SETTLE, init=0')),
  ('halt-common-trap','cp20c',choose(0),('TRAP, target=HALT_ENTRY','TRAP, target=BUS_FAULT_ENTRY')),
  ('halt-odd-pc','cp20c',choose(0,0,True),('alu BIC, a=T3, pair=AD, d=ONE, b=R7, dst=RF, seq=FETCH','alu PASSA, a=T3, b=R7, dst=RF, seq=FETCH')),
  ('halt-wrong-psw','cp20c',choose(0),('d=IMM, imm=$e0, flags=LOAD','d=IMM, imm=$00, flags=LOAD'))]
 for name,archive,fixture,mutation in cases:
  with tempfile.TemporaryDirectory(prefix='uj11-control-negative-') as temp:
   root=Path(temp)
   with tarfile.open(ROOT/'synth/reports'/archive/'source.tgz') as a:a.extractall(root,filter='data')
   (root/'tb').mkdir(exist_ok=True);(root/'build').mkdir(exist_ok=True)
   for rel in ['tb/tb_trace_bit.v','tb/uj11_ram.v','rtl/uj11_rom.v','reference/lsi11/spi_fram_model.v']:
    (root/rel).parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,root/rel)
   if archive=='cp19a':
    p=root/'tb/tb_trace_bit.v';s=p.read_text().replace('.peripheral_reset(peripheral_reset),','');s=s.replace('wire irq_ack,waiting,peripheral_reset;',"wire irq_ack,waiting,peripheral_reset; assign peripheral_reset=1'b0;");p.write_text(s)
   if mutation:
    f=root/'microcode/m0.uasm';s=f.read_text();assert s.count(mutation[0])==1,(name,s.count(mutation[0]));f.write_text(s.replace(*mutation))
   subprocess.run(['python3','microasm/uj11asm.py','microcode/m0.uasm','-o','microcode/generated/m0.mem'],cwd=root,check=True,stdout=subprocess.DEVNULL)
   (root/'build/system_control-vectors.txt').write_text(fixture)
   sources=[str(f.relative_to(root)) for f in sorted((root/'rtl').glob('*.v'))]
   subprocess.run(['iverilog','-g2012','-s','tb_trace_bit','-Ptb_trace_bit.SYSTEM_CONTROL=1','-o','build/negative','tb/tb_trace_bit.v','tb/uj11_ram.v']+sources+['reference/lsi11/spi_fram_model.v','reference/lsi11/spi_fram_guest_ram.v'],cwd=root,check=True)
   result=subprocess.run(['vvp','build/negative'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
   (ROOT/f'build/cp20-negative-{name}.log').write_text(f'Archive: {archive}; mutation: {mutation!r}\n'+result.stdout)
   marker={'unregistered-reset':'unexpected peripheral reset side effect','missing-reset':'beat1 got wr0','reset-no-pulse':'trace case','halt-common-trap':'beat1 got wr0','halt-odd-pc':'R7 got','halt-wrong-psw':'trace case'}[name]
   if not result.returncode or 'FATAL' not in result.stdout or marker not in result.stdout:raise SystemExit(name+': expected failure missing: '+result.stdout)
   print(f'PASS system control negative: {name}; archived {archive}')
if __name__=='__main__':main()
