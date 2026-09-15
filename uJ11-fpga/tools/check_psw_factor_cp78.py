#!/usr/bin/env python3
"""Bitwise exhaustive control/data equivalence, plus a T-protection mutation."""
import hashlib,json,subprocess
from board_common import ROOT

def run():
    out=ROOT/'build/cp78b-equivalence';out.mkdir(exist_ok=True)
    source=ROOT/'rtl/cp78/uj11_psw.v';candidate=ROOT/'rtl/cp78/uj11_psw_factored.v';bench=ROOT/'tb/tb_psw_factor_cp78.v'
    gold=out/'gold.v';gold.write_text(source.read_text().replace('module uj11_psw (','module uj11_psw_gold ('))
    paths=[source,candidate,bench,__import__('pathlib').Path(__file__)]
    inputs={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    results=[]
    for mutation in (False,True):
        name='bad-t' if mutation else 'equivalent';new=out/(name+'.v');text=candidate.read_text()
        if mutation:
            assert text.count('if (load) psw[4] <= value[4];')==1
            text=text.replace('if (load) psw[4] <= value[4];','if (load || low_write) psw[4] <= low_write ? bus_value[4] : value[4];')
        new.write_text(text)
        with (out/(name+'-build.log')).open('w') as log:subprocess.run(['iverilog','-g2012','-s','tb_psw_factor_cp78','-o',str(out/(name+'.sim')),str(bench),str(gold),str(new)],stdout=log,stderr=subprocess.STDOUT,check=True)
        with (out/(name+'.log')).open('w') as log:r=subprocess.run(['vvp',str(out/(name+'.sim'))],stdout=log,stderr=subprocess.STDOUT)
        log=(out/(name+'.log')).read_text();assert (r.returncode!=0)==mutation,log
        assert ('PSW mismatch' if mutation else 'PASS CP78 PSW factor equivalence: 131078 clock comparisons') in log
        print(name+': '+log.splitlines()[0]);results.append(dict(mutation=mutation,exit_code=r.returncode))
    for p,h in inputs.items():assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h,p
    (out/'result.json').write_text(json.dumps(dict(passed=True,inputs=inputs,results=results),indent=2)+'\n')
if __name__=='__main__':run()
