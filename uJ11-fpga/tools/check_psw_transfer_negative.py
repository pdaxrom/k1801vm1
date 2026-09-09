#!/usr/bin/env python3
"""Reject five concrete PSW transfer regressions using archived fit sources."""
import subprocess,tarfile,tempfile,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def records(stem,fault=False):
 lines=iter((ROOT/f'build/{stem}-vectors.txt').read_text().splitlines())
 for line in lines:
  h=line.split();ctl=[] if fault else [next(lines)]
  patch=[next(lines) for _ in range(int(h[12 if fault else 11],16))]
  post=next(lines);bus=[next(lines) for _ in range(int(post.split()[9],16))]
  yield h,([] if fault else ctl[0].split()),'\n'.join([line,*ctl,*patch,post,*bus])+'\n'
def main():
 items=list(records('psw_transfer'));faults=list(records('psw-transfer-fault',True))
 def choose(op,psw,irq=0,r0=None):
  return next(t for h,c,t in items if int(h[1],16)==op and int(h[2],16)==psw and int(c[0],16)==irq and (r0 is None or int(h[3],16)==r0))
 failed_write=next(t for h,c,t in faults if int(h[1],16)==0o106710 and int(h[2],16)==3 and int(h[11],16)==1)
 cases=[
  ('missing-mfps','cp18e',choose(0o106700,0),False,None),
  ('mfps-zero-extends','cp19a',choose(0o106700,0x80),False,('d=PSW, b=RD, dst=MOV, flags=NZV','d=PSW, b=RD, dst=RF, flags=NZV')),
  ('mtps-overwrites-t','cp19a',choose(0o106400,7,r0=0xa510),False,('imm=$ef, b=T4','imm=$ff, b=T4')),
  ('mtps-old-ipl','cp19a',choose(0o106400,0xe7,0xe40,0xa500),False,('alu OR, a=T2, b=T4, flags=LOAD\n','alu OR, a=T2, b=T4, flags=LOAD, seq=FETCH\n')),
  ('mfps-flags-before-write','cp19a',failed_write,True,('alu PASSA, pair=DA, d=PSW, b=T0, dst=RF\n    JUMP, target=MOV_MEMORY','alu PASSA, pair=DA, d=PSW, b=T0, dst=RF, flags=NZV\n    JUMP, target=MOV_MEMORY'))]
 for name,archive,fixture,fault,mutation in cases:
  with tempfile.TemporaryDirectory(prefix='uj11-psw-negative-') as temp:
   root=Path(temp)
   with tarfile.open(ROOT/'synth/reports'/archive/'source.tgz') as a:a.extractall(root,filter='data')
   (root/'tb').mkdir(exist_ok=True);(root/'build').mkdir(exist_ok=True)
   for rel in ['tb/tb_trace_bit.v','tb/tb_bus_fault.v','tb/uj11_ram.v','rtl/uj11_rom.v','reference/lsi11/spi_fram_model.v']:
    (root/rel).parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,root/rel)
   if mutation:
    f=root/'microcode/m0.uasm';s=f.read_text();assert s.count(mutation[0])==1;f.write_text(s.replace(*mutation))
   subprocess.run(['python3','microasm/uj11asm.py','microcode/m0.uasm','-o','microcode/generated/m0.mem'],cwd=root,check=True,stdout=subprocess.DEVNULL)
   stem='psw-transfer-fault' if fault else 'psw_transfer';top='tb_bus_fault' if fault else 'tb_trace_bit'
   (root/f'build/{stem}-vectors.txt').write_text(fixture)
   sources=[str(f.relative_to(root)) for f in sorted((root/'rtl').glob('*.v'))]
   subprocess.run(['iverilog','-g2012','-s',top,f'-P{top}.PSW_TRANSFER=1','-o','build/negative',f'tb/{top}.v','tb/uj11_ram.v']+sources+['reference/lsi11/spi_fram_model.v','reference/lsi11/spi_fram_guest_ram.v'],cwd=root,check=True)
   r=subprocess.run(['vvp','build/negative'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
   (ROOT/f'build/cp19-negative-{name}.log').write_text(f'Archive: {archive}; mutation: {mutation!r}\n'+r.stdout)
   marker={'missing-mfps':'beat1 got wr0','mfps-zero-extends':'R0 got','mtps-overwrites-t':'trace case','mtps-old-ipl':'trace case','mfps-flags-before-write':'fault case'}[name]
   if not r.returncode or 'FATAL' not in r.stdout or marker not in r.stdout:raise SystemExit(name+': expected failure missing: '+r.stdout)
   print(f'PASS PSW transfer negative: {name}; archived {archive}')
if __name__=='__main__':main()
