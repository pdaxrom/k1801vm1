#!/usr/bin/env python3
"""Measure the seven implemented FP controls with actual RTL and zero-wait RAM."""
import json
import re
import subprocess
from board_common import ROOT,CORE

def main():
    names={0o170100:'LDFPS R0',0o170200:'STFPS R0',0o170000:'CFCC',
           0o170001:'SETF',0o170002:'SETI',0o170011:'SETD',0o170012:'SETL'}
    found={}
    for line in (ROOT/'build/fp-control-vectors.mem').read_text().splitlines():
        op=int(line.split()[0],16)
        if op in names and op not in found:found[op]=line
    assert set(found)==set(names)
    (ROOT/'build/fp-control-bench.mem').write_text('\n'.join(found[op] for op in names)+'\n')
    results={}
    for vendor in (False,True):
        tag='cp30-fp-bench-'+('vendor' if vendor else 'portable')
        lib=[str(ROOT/'build/vendor'/f'{n}.v') for n in ('DP8KC','GSR','PUR')] if vendor else []
        command=['iverilog','-g2012','-s','tb_fp_control','-Ptb_fp_control.CASES=7','-Ptb_fp_control.BENCHMARK=1','-o',f'build/{tag}']
        if vendor:command+=['-DUJ11_VENDOR_ROM']
        subprocess.run(command+['tb/tb_fp_control.v']+CORE+['tb/uj11_ram.v','microcode/generated/uj11_m0_ebr.v' if vendor else 'rtl/uj11_rom.v']+lib,cwd=ROOT,check=True)
        result=subprocess.check_output(['vvp',f'build/{tag}','+VECTORS=build/fp-control-bench.mem'],cwd=ROOT,text=True)
        (ROOT/f'build/{tag}.log').write_text(result)
        rows={names[int(op,8)]:dict(microclocks=int(clocks),external_beats=int(beats))
              for op,clocks,beats in re.findall(r'FPBENCH ([0-7]+) (\d+) (\d+)',result)}
        assert len(rows)==7
        results['vendor' if vendor else 'portable']=rows
    assert results['portable']==results['vendor']
    (ROOT/'build/cp30-fp-bench.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(results,indent=2))
if __name__=='__main__':main()
