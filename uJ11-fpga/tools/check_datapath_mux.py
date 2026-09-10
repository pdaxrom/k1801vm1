#!/usr/bin/env python3
"""Unrestricted sequential SAT equivalence of CP40 datapath candidates."""
import argparse
import hashlib
import json
import os
import subprocess
from board_common import ROOT
from build_mmu_entry import change


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--yosys',default='yosys')
    args=parser.parse_args()
    out=ROOT/'build/cp40-proof';out.mkdir(parents=True,exist_ok=True)
    sources=['tools/check_datapath_mux.py','build/cp40-mux/inputs.json','build/cp40-alu/baseline/uj11_alu.v','rtl/uj11_regfile.v']
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
    (out/'proof.ys').write_text(script)
    (out/'alu.v').write_bytes((ROOT/'build/cp40-alu/baseline/uj11_alu.v').read_bytes())
    (out/'regfile.v').write_bytes((ROOT/'rtl/uj11_regfile.v').read_bytes())
    base='build/cp40-mux/baseline/uj11_datapath.v';sources.append(base)
    (out/'gold.v').write_text((ROOT/base).read_text().replace('module uj11_datapath (','module gold ('))
    tests=[]
    for kind in ('shift','pair','both','bad-byte-merge','bad-q-shift'):
        negative=kind.startswith('bad-')
        variant='both' if negative else kind
        path=f'build/cp40-mux/{variant}/uj11_datapath.v';sources.append(path)
        source=(ROOT/path).read_text()
        if kind=='bad-byte-merge':
            source=change(source,"byte_mode && destination==3'd5","1'b0")
        if kind=='bad-q-shift':
            source=change(source,"q <= {q[14:0],1'b0};","q <= {q[14:0],1'b1};")
        (out/'gate.v').write_text(source.replace('module uj11_datapath (','module gate ('))
        run=subprocess.run([args.yosys,'-s','proof.ys'],cwd=out,
            env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')),
            text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (out/(kind+'.log')).write_text(run.stdout)
        assert (run.returncode!=0 and 'unproven' in run.stdout) if negative else (run.returncode==0 and 'Equivalence successfully proven!' in run.stdout),run.stdout[-2400:]
        tests.append(dict(variant=kind,negative=negative,returncode=run.returncode,
                          candidate_sha256=hashlib.sha256(source.encode()).hexdigest(),log_sha256=hashlib.sha256(run.stdout.encode()).hexdigest()))
        print('PASS CP40 '+('rejected ' if negative else 'unrestricted sequential equivalence: ')+kind,flush=True)
    report=dict(method=script,assumptions='None on datapath controls, operands, reset/enable or RF/Q state',
                tests=tests,inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources})
    (ROOT/'build/cp40-proof.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
