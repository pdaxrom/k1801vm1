#!/usr/bin/env python3
"""Four-state simulation of CP41 control sequencing and context override."""
import hashlib
import json
import subprocess
from board_common import ROOT


def main():
    out=ROOT/'build/cp41-sim';out.mkdir(parents=True,exist_ok=True)
    inputs=['tb/tb_seq_mux.v','tools/check_seq_mux_sim.py','build/cp41-seq/inputs.json'];tests=[]
    for kind in ('flat','merged','encoded','folded'):
        for apr in (False,True):
            name='uj11_microseq_apr.v' if apr else 'uj11_microseq.v'
            base=f'build/cp41-seq/baseline/{name}';path=f'build/cp41-seq/{kind}/{name}';inputs.extend([base,path])
            (out/'gold.v').write_text((ROOT/base).read_text().replace('module uj11_microseq (','module uj11_microseq_reference ('))
            tag=kind+('-apr' if apr else '-production')
            lint_dir=out/tag;lint_dir.mkdir(exist_ok=True)
            lint_source=lint_dir/'uj11_microseq.v';lint_source.write_bytes((ROOT/path).read_bytes())
            lint=subprocess.run(['verilator','--lint-only','--Wall','--top-module','uj11_microseq',str(lint_source)],
                cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            (out/(tag+'-lint.log')).write_text(lint.stdout);assert lint.returncode==0,lint.stdout
            cmd=['iverilog','-g2012','-Wall','-s','tb_seq_mux','-o',str(out/'sim')]+(['-DAPR'] if apr else [])+[str(ROOT/'tb/tb_seq_mux.v'),str(out/'gold.v'),str(ROOT/path)]
            build=subprocess.run(cmd,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            (out/(tag+'-build.log')).write_text(build.stdout);assert build.returncode==0,build.stdout
            run=subprocess.run(['vvp',str(out/'sim')],cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            (out/(tag+'.log')).write_text(run.stdout)
            lines=[line for line in run.stdout.splitlines() if line.startswith('PASS CP41')]
            assert run.returncode==0 and len(lines)==1,run.stdout
            tests.append(dict(tag=tag,pass_line=lines[0],log_sha256=hashlib.sha256(run.stdout.encode()).hexdigest(),lint_returncode=lint.returncode))
            print(lines[0],tag,flush=True)
    (ROOT/'build/cp41-sim.json').write_text(json.dumps(dict(tests=tests,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(inputs))}),indent=2)+'\n')


if __name__=='__main__':main()
