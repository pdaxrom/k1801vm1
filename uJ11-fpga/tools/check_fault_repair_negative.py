#!/usr/bin/env python3
"""Confirm the CP16a no-repair fit fails the current DCJ11 fault-frame fixture."""
from pathlib import Path
import subprocess
import tarfile
import tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='uj11-cp16-negative-') as temp:
    path=Path(temp)
    with tarfile.open(ROOT/'synth/reports/cp16a/source.tgz') as t:t.extractall(path,filter='data')
    (path/'build').mkdir(exist_ok=True)
    (path/'build/bus-fault-vectors.txt').write_bytes((ROOT/'build/bus-fault-vectors.txt').read_bytes())
    # The older engine has no repair FF; this changes only a testbench counter.
    tb=(ROOT/'tb/tb_bus_fault.v').read_text().replace('dut.engine.fault_repair',"1'b0")
    (path/'tb/tb_bus_fault.v').write_text(tb)
    subprocess.run(['iverilog','-g2012','-s','tb_bus_fault','-o','build/negative','tb/tb_bus_fault.v']+
                   [str(p.relative_to(path)) for p in sorted((path/'rtl').glob('*.v'))],cwd=path,check=True)
    r=subprocess.run(['vvp','build/negative'],cwd=path,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (ROOT/'build/cp16-no-repair-negative.log').write_text(r.stdout)
    if r.returncode==0 or ' R0 got2000 expected2002' not in r.stdout:
        raise SystemExit('Expected archived CP16a autoincrement mismatch was not reproduced: '+r.stdout)
    print('PASS negative control: archived CP16a fails R0 2000 vs 2002; no-repair variant is rejected')
