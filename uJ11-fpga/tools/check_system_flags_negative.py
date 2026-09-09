#!/usr/bin/env python3
"""Require architectural/bus failures for archived absent or corrupted CC/MFPT."""
import argparse,subprocess,tarfile,tempfile,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def records():
 lines=iter((ROOT/'build/system_flags-vectors.txt').read_text().splitlines())
 for line in lines:
  h=line.split();ctl=next(lines);patch=[next(lines) for _ in range(int(h[11],16))]
  post=next(lines);bus=[next(lines) for _ in range(int(post.split()[9],16))]
  yield h,ctl.split(),'\n'.join([line,ctl,*patch,post,*bus])+'\n'

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',default='cp18e');args=p.parse_args()
 items=list(records())
 def choose(op,psw):return next(text for h,c,text in items if int(h[1],16)==op and int(h[2],16)==psw and int(c[0],16)==0)
 for name,archive,fixture,mutation in [
  ('missing-cc','cp17a',choose(0o240,0),None),
  ('clear-sets',args.archive,choose(0o241,1),('alu BIC, pair=DB, d=PSW, b=T4, flags=LOAD, seq=FETCH','alu OR, pair=DB, d=PSW, b=T4, flags=LOAD, seq=FETCH')),
  ('set-clears',args.archive,choose(0o261,0),('alu OR, pair=DB, d=PSW, b=T4, flags=LOAD, seq=FETCH','alu BIC, pair=DB, d=PSW, b=T4, flags=LOAD, seq=FETCH')),
  ('wrong-mfpt',args.archive,choose(7,0),('imm=5, b=R0, dst=RF, seq=FETCH','imm=4, b=R0, dst=RF, seq=FETCH'))]:
  with tempfile.TemporaryDirectory(prefix='uj11-cc-negative-') as temp:
   root=Path(temp)
   with tarfile.open(ROOT/'synth/reports'/archive/'source.tgz') as a:a.extractall(root,filter='data')
   (root/'tb').mkdir(exist_ok=True);(root/'build').mkdir(exist_ok=True)
   for rel in ['tb/tb_trace_bit.v','tb/uj11_ram.v','rtl/uj11_rom.v','reference/lsi11/spi_fram_model.v']:
    (root/rel).parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,root/rel)
   if mutation:
    f=root/'microcode/m0.uasm';s=f.read_text();assert s.count(mutation[0])==(1 if name=='wrong-mfpt' else 2);f.write_text(s.replace(*mutation))
   subprocess.run(['python3','microasm/uj11asm.py','microcode/m0.uasm','-o','microcode/generated/m0.mem'],cwd=root,check=True,stdout=subprocess.DEVNULL)
   (root/'build/system_flags-vectors.txt').write_text(fixture)
   sources=[str(f.relative_to(root)) for f in sorted((root/'rtl').glob('*.v'))]
   subprocess.run(['iverilog','-g2012','-s','tb_trace_bit','-Ptb_trace_bit.SYSTEM_FLAGS=1','-o','build/negative','tb/tb_trace_bit.v','tb/uj11_ram.v']+sources+['reference/lsi11/spi_fram_model.v','reference/lsi11/spi_fram_guest_ram.v'],cwd=root,check=True)
   r=subprocess.run(['vvp','build/negative'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
   (ROOT/'build'/f'cp18-negative-{name}.log').write_text(f'Archive: {archive}; mutation: {mutation!r}\n'+r.stdout)
   marker={'missing-cc':'beat1 got wr0','clear-sets':'trace case','set-clears':'trace case','wrong-mfpt':'R0 got'}[name]
   if not r.returncode or 'FATAL' not in r.stdout or marker not in r.stdout:raise SystemExit(name+': expected failure missing: '+r.stdout)
   print(f'PASS system flags negative: {name}; archived {archive}')
if __name__=='__main__':main()
