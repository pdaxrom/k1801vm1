#!/usr/bin/env python3
"""Unrestricted CP41 sequential equivalence, with deliberate priority/link errors."""
import argparse
import hashlib
import json
import os
import subprocess
from board_common import ROOT
from build_mmu_entry import change


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--yosys',default='yosys');args=p.parse_args()
    out=ROOT/'build/cp41-proof';out.mkdir(parents=True,exist_ok=True)
    script='''read_verilog gold.v gate.v
proc
opt_clean
equiv_make gold gate equiv
hierarchy -top equiv
equiv_simple
equiv_induct -seq 4
equiv_status -assert
'''
    (out/'proof.ys').write_text(script)
    tests=[];inputs=['tools/check_seq_mux.py','build/cp41-seq/inputs.json']
    for variant,apr in [('flat',False),('merged',False),('flat',True),('merged',True),('encoded',False),('encoded',True),('folded',False),('folded',True),
                        ('bad-link',False),('bad-repair',False),('bad-context',True)]:
        negative=variant.startswith('bad-');kind='flat' if negative else variant
        filename='uj11_microseq_apr.v' if apr else 'uj11_microseq.v'
        base=f'build/cp41-seq/baseline/{filename}';path=f'build/cp41-seq/{kind}/{filename}'
        inputs.extend([base,path]);source=(ROOT/path).read_text()
        if variant=='bad-link':source=change(source,'link <= sequential;','link <= upc;')
        if variant=='bad-repair':source=change(source,"if (fault_repair) next_address = 10'h015;","if (fault_repair && !fault_redirect) next_address = 10'h015;")
        if variant=='bad-context':source=change(source,'if (context_redirect) next_address = context_address;',
            'if (context_redirect && !fault_redirect) next_address = context_address;')
        (out/'gold.v').write_text((ROOT/base).read_text().replace('module uj11_microseq (','module gold ('))
        (out/'gate.v').write_text(source.replace('module uj11_microseq (','module gate ('))
        result=subprocess.run([args.yosys,'-s','proof.ys'],cwd=out,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
            env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')))
        tag=variant+('-apr' if apr else '-production');(out/(tag+'.log')).write_text(result.stdout)
        assert (result.returncode!=0 and 'unproven' in result.stdout) if negative else (result.returncode==0 and 'Equivalence successfully proven!' in result.stdout),result.stdout[-2000:]
        tests.append(dict(tag=tag,negative=negative,returncode=result.returncode,
            candidate_sha256=hashlib.sha256(source.encode()).hexdigest(),log_sha256=hashlib.sha256(result.stdout.encode()).hexdigest()))
        print('PASS CP41 '+('reject ' if negative else 'equivalence ')+tag,flush=True)
    (ROOT/'build/cp41-proof.json').write_text(json.dumps(dict(method=script,tests=tests,
        assumptions='No constraints on control words, flags, faults, IRQ/trace, dispatch, reset/enable or matched sequencer state',
        inputs_sha256={s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sorted(set(inputs))}),indent=2)+'\n')


if __name__=='__main__':main()
