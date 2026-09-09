#!/usr/bin/env python3
"""Reproduce the early-IRQ bug in the archived CP16c core fit."""
from pathlib import Path
import subprocess
import tarfile
import tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='uj11-cp16-irq-negative-') as temp:
    path=Path(temp)
    with tarfile.open(ROOT/'synth/reports/cp16c/source.tgz') as t:t.extractall(path,filter='data')
    (path/'build').mkdir(exist_ok=True)
    (path/'tb/tb_bus_fault_system.v').write_bytes((ROOT/'tb/tb_bus_fault_system.v').read_bytes())
    sources=[str(p.relative_to(path)) for p in sorted((path/'rtl').glob('*.v'))]
    subprocess.run(['iverilog','-g2012','-s','tb_bus_fault_system','-o','build/negative',
                    'tb/tb_bus_fault_system.v']+sources+['reference/lsi11/spi_fram_model.v',
                    'reference/lsi11/spi_fram_guest_ram.v'],cwd=path,check=True)
    result=subprocess.run(['vvp','build/negative'],cwd=path,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (ROOT/'build/cp16-early-irq-negative.log').write_text(result.stdout)
    if not result.returncode or 'IRQ accepted before the expected handler instruction' not in result.stdout:
        raise SystemExit('Expected archived CP16c IRQ-order failure not reproduced: '+result.stdout)
    print('PASS negative control: archived CP16c accepts IRQ before the first fault handler instruction')
