#!/usr/bin/env python3
"""CP28 portable/vendor ROM, exact timebase, FRAM and peripheral contracts."""
from pathlib import Path
import os
import subprocess
from board_common import ROOT,CORE,BOARD

def run(top,tag,sources,flags=()):
    subprocess.run(['iverilog','-g2012','-s',top,'-o','build/'+tag]+list(flags)+['tb/'+top+'.v']+sources,cwd=ROOT,check=True)
    with (ROOT/'build'/f'{tag}.log').open('w') as log:
        subprocess.run(['vvp','build/'+tag],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    print((ROOT/'build'/f'{tag}.log').read_text(),flush=True)

def main():
    vendor=Path(os.environ.get('LATTICE_SIM_DIR',ROOT/'build/vendor'))
    tests=[('decode_rom',['rtl/uj11_decode_rom.v','microcode/generated/uj11_decode_table.v','reference/uj11/decode_cp27.v']),
           ('firmware_rom',['microcode/generated/uj11_firmware_rom.v'])]
    for name,sources in tests:
        for variant in ('portable','vendor'):
            flags=[] if variant=='portable' else ['-DUJ11_VENDOR_ROM']
            library=[] if variant=='portable' else [str(vendor/(x+'.v')) for x in ('DP8KC','GSR','PUR')]
            run('tb_'+name,f'cp28-{name}-{variant}',sources+library,flags)
    for divisor in (1,3):
        run('tb_board_fram',f'cp28-fram-{divisor}',['boards/hc1200/uj11_board_fram.v','reference/lsi11/spi_fram_model.v'],
            [f'-Ptb_board_fram.CLK_DIV={divisor}'])
    for mode in (0,1):
        run('tb_board_irq',f'cp28-irq-{mode}',CORE+['tb/uj11_ram.v','rtl/uj11_rom.v'],[f'-Ptb_board_irq.ROM_DECODE={mode}'])
    for divisor in (1,2,3,257,591200):
        run('tb_board_tick',f'cp28-tick-{divisor}',['boards/hc1200/uj11_tick.v'],[f'-Ptb_board_tick.DIVISOR={divisor}'])
    state=1
    for i in range(1,1048576):
        state=((state<<1)&0xfffff)^(9 if state&0x80000 else 0)
        assert state!=1 or i==1048575
    assert state==1
    print('PASS LFSR polynomial: exact period 1048575 nonzero states',flush=True)
    run('tb_board_bus','cp28-board-bus',BOARD+['reference/lsi11/spi_fram_model.v'])
if __name__=='__main__':main()
