#!/usr/bin/env python3
"""Unrestricted sequential equivalence of CP53 cursor mappings to CP52."""
import hashlib
import json
import os
import subprocess
from board_common import ROOT
from build_cursor_cp53 import OUT, VARIANTS, build
from build_fram_cp52 import replace_once


def main():
    build()
    out=ROOT/'build/cp53-proof';out.mkdir(parents=True,exist_ok=True)
    inputs=['tools/check_cursor_cp53.py','build/cp53-cursor/inputs.json']
    runs=[]
    for variant in VARIANTS:
        for divisor in (1,3):
            for negative in ((False,True) if divisor==1 else (False,)):
                tag=f'{variant}-{divisor}-'+('bad-carry' if negative else 'ok')
                folder=out/tag;folder.mkdir(exist_ok=True)
                for name,source in [('gold',OUT/'baseline/uj11_board_fram.v'),('gate',OUT/variant/'uj11_board_fram.v')]:
                    inputs.append(str(source.relative_to(ROOT)))
                    text=replace_once(source.read_text(),'module uj11_board_fram','module '+name)
                    text=replace_once(text,'CLK_DIV=1','CLK_DIV='+str(divisor))
                    if name=='gate' and negative:
                        # Deliberately omit the carry into the upper half.
                        if variant=='compare':text=replace_once(text,"next_word<=address[15:1]+1'b1;","next_word<={address[15:9],address[8:1]+8'd1};")
                        else:text=replace_once(text,'(&address[increment_bit:1])',"(increment_bit==8 ? 1'b0 : (&address[increment_bit:1]))")
                    (folder/(name+'.v')).write_text(text)
                # Same private state representation: equiv_make additionally
                # proves every matched state bit, including next_word. No
                # assumptions on input address/stability/MISO or request gaps.
                script='''read_verilog -sv gold.v gate.v
proc
opt_clean
equiv_make gold gate equiv
hierarchy -top equiv
equiv_simple
equiv_induct -seq 4
equiv_status -assert
'''
                (folder/'proof.ys').write_text(script)
                result=subprocess.run([str(ROOT/'build/formal/bin/yowasp-yosys'),'-s','proof.ys'],cwd=folder,
                    stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,
                    env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')))
                (folder/'proof.log').write_text(result.stdout)
                if negative:assert result.returncode!=0 and 'unproven' in result.stdout,result.stdout[-1500:]
                else:assert result.returncode==0 and 'Equivalence successfully proven!' in result.stdout,result.stdout[-1500:]
                runs.append(dict(variant=variant,divider=divisor,negative=negative,returncode=result.returncode,
                    log=str((folder/'proof.log').relative_to(ROOT)),log_sha256=hashlib.sha256(result.stdout.encode()).hexdigest()))
                print('PASS CP53 '+tag,flush=True)
    inputs += [str(p.relative_to(ROOT)) for p in out.rglob('*') if p.suffix in ('.v','.ys')]
    (out/'result.json').write_text(json.dumps(dict(method='Yosys unrestricted sequential equivalence, matched full state and outputs',
        assumptions='None on inputs, state equality invariant; no req/address stability restriction',runs=runs,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(inputs))}),indent=2)+'\n')


if __name__=='__main__':main()
