#!/usr/bin/env python3
"""Prove D/Q preserves the old datapath for every legacy arithmetic context."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--yosys',default='yosys')
    args=p.parse_args()
    with tarfile.open(ROOT/'synth/reports/cp26a/source.tgz') as a:
        old=a.extractfile('rtl/uj11_datapath.v').read().decode()
    expected=json.loads((ROOT/'synth/reports/cp26a/inputs.json').read_text())['files']['rtl/uj11_datapath.v']
    assert hashlib.sha256(old.encode()).hexdigest()==expected
    new=(ROOT/'rtl/uj11_datapath.v').read_text()
    # Every old ALU pair5 uses ZERO, so constrain only that input context.
    import sys
    sys.path.insert(0,str(ROOT/'microasm'))
    from uj11asm import assemble
    image,listing,_,_=assemble((ROOT/'microcode/m0.uasm').read_text())
    legacy_q=[]
    for line in listing.splitlines():
        address=int(line.split()[0],16);w=image[address]
        if not w>>35 and (w>>18)&7==5:
            assert (w>>10)&7==0
            legacy_q.append(address)
    assert legacy_q
    script='''read_verilog -sv alu.v regfile.v gold.v gate.v
proc
memory
flatten
opt_clean
equiv_make gold gate equiv
hierarchy -top equiv
equiv_simple
equiv_induct -seq 4
equiv_status -assert
'''
    def wrapped(source,name):
        source=source.replace('module uj11_datapath (','module '+name+' (')
        source=source.replace('input wire [15:0] d,','input wire [15:0] external_d,')
        source=source.replace('    // Decode once,',"    wire [15:0] d = pair==5 ? 16'b0 : external_d;\n    // Decode once,")
        return source
    results={}
    with tempfile.TemporaryDirectory(prefix='uj11-cp27-proof-') as directory:
        work=Path(directory)
        (work/'alu.v').write_bytes((ROOT/'rtl/uj11_alu.v').read_bytes())
        (work/'regfile.v').write_bytes((ROOT/'rtl/uj11_regfile.v').read_bytes())
        (work/'gold.v').write_text(wrapped(old,'gold'))
        (work/'proof.ys').write_text(script)
        for negative in (False,True):
            candidate=new.replace('.carry(carry)',".carry(1'b0)") if negative else new
            assert not negative or candidate!=new
            (work/'gate.v').write_text(wrapped(candidate,'gate'))
            r=subprocess.run([args.yosys,'-s','proof.ys'],cwd=work,
                env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')),
                text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            kind='negative' if negative else 'positive'
            log=ROOT/f'build/cp27-datapath-{kind}.log';log.write_text(r.stdout)
            if negative:
                assert r.returncode!=0 and 'unproven' in r.stdout,r.stdout[-2000:]
            else:
                assert r.returncode==0 and 'Equivalence successfully proven!' in r.stdout,r.stdout[-2000:]
            results[kind]={'returncode':r.returncode,'log_sha256':hashlib.sha256(r.stdout.encode()).hexdigest()}
    report={'baseline':'cp26a','baseline_sha256':expected,
            'candidate_sha256':hashlib.sha256(new.encode()).hexdigest(),
            'context':'D=0 if pair=5; all other inputs and register states unconstrained',
            'legacy_pair5_alu_addresses':legacy_q,'results':results}
    (ROOT/'build/cp27-datapath-equivalence.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP27 datapath: legacy-context SAT equivalence; removed carry input rejected')


if __name__=='__main__':main()
