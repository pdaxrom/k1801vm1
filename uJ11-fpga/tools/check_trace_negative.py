#!/usr/bin/env python3
"""Require real failures for missing trace, wrong RTI/RTT policy and IRQ priority."""
from pathlib import Path
import subprocess,tarfile,tempfile,shutil
ROOT=Path(__file__).resolve().parents[1]

def records():
    lines=iter((ROOT/'build/trace_bit-vectors.txt').read_text().splitlines())
    for line in lines:
        h=line.split();ctl=next(lines);patch=[next(lines) for _ in range(int(h[11],16))]
        post=next(lines);bus=[next(lines) for _ in range(int(post.split()[9],16))]
        yield h,ctl.split(),[p.split() for p in patch],'\n'.join([line,ctl,*patch,post,*bus])+'\n'

items=list(records())
selected={
 'rti':next(text for h,c,p,text in items if int(h[1],16)==2 and not int(h[2],16)&16 and int(c[0],16)==0 and any(int(a,16)==0x6002 and int(v,16)&16 for a,v in p)),
 'rtt':next(text for h,c,p,text in items if int(h[1],16)==6 and int(c[0],16)==0 and any(int(a,16)==0x6002 and int(v,16)&16 for a,v in p))}
for name,archive,tb,fixture,mutation in [
 ('missing-trace','cp16f','tb_trace_bit',(ROOT/'build/trace_bit-vectors.txt').read_text(),None),
 ('rti-old-T','cp17a','tb_trace_bit',selected['rti'],('wire return_trace = uword[0] && uword[12:10]!=3\'d6;',"wire return_trace = 1'b0;")),
 ('rtt-traces','cp17a','tb_trace_bit',selected['rtt'],('(psw[4] && !ir[2])','psw[4]')),
 ('trace-irq-ack','cp17a','tb_trace_system',None,('irq_pending && !trace_pending &&','irq_pending &&'))]:
    with tempfile.TemporaryDirectory(prefix='uj11-trace-negative-') as temp:
        p=Path(temp)
        with tarfile.open(ROOT/'synth/reports'/archive/'source.tgz') as a:a.extractall(p,filter='data')
        (p/'tb').mkdir(exist_ok=True);(p/'build').mkdir(exist_ok=True)
        for rel in ['tb/'+tb+'.v','tb/uj11_ram.v','rtl/uj11_rom.v','reference/lsi11/spi_fram_model.v']:
            (p/rel).parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,p/rel)
        subprocess.run(['python3','microasm/uj11asm.py','microcode/m0.uasm','-o','microcode/generated/m0.mem'],cwd=p,check=True,stdout=subprocess.DEVNULL)
        if mutation:
            f=p/'rtl/uj11_engine.v';s=f.read_text();assert s.count(mutation[0])==1;f.write_text(s.replace(*mutation))
        if fixture:(p/'build/trace_bit-vectors.txt').write_text(fixture)
        sources=[str(f.relative_to(p)) for f in sorted((p/'rtl').glob('*.v'))]
        subprocess.run(['iverilog','-g2012','-s',tb,'-o','build/negative','tb/'+tb+'.v','tb/uj11_ram.v']+sources+['reference/lsi11/spi_fram_model.v','reference/lsi11/spi_fram_guest_ram.v'],cwd=p,check=True)
        r=subprocess.run(['vvp','build/negative'],cwd=p,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        marker='wrong return/IRQ trace boundary' if name=='trace-irq-ack' else 'trace case'
        # The missing-RTT-suppression mutation reaches an unexpected vector READ;
        # accept only that bus comparison or the complete architectural mismatch.
        valid_failure=marker in r.stdout or (name=='rtt-traces' and 'beat3 got wr0 000e' in r.stdout)
        (ROOT/'build'/f'cp17-negative-{name}.log').write_text(f'Archived source: {archive}; mutation: {mutation!r}\n'+r.stdout)
        if not r.returncode or not valid_failure:raise SystemExit(name+': expected negative failure missing: '+r.stdout)
        print(f'PASS trace negative: {name}; archived {archive}'+(' with one explicit mutation' if mutation else ''))
