#!/usr/bin/env python3
"""CP32 MMU + physical SPI model; strict new RTL, frozen imported model widths."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    sources = ['tb/tb_mmu_fram.v', 'rtl/uj11_mmu_translate.v',
               'boards/hc1200/uj11_board_fram.v', 'reference/lsi11/spi_fram_model.v']
    def hashes():
        return {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources}
    before = hashes()
    model = ROOT/'build/cp32-fram-model.v'
    model.write_text('/* verilator lint_off WIDTHEXPAND */\n'
                     '/* verilator lint_off WIDTHTRUNC */\n'+(ROOT/sources[-1]).read_text()+
                     '\n/* verilator lint_on WIDTHEXPAND */\n'
                     '/* verilator lint_on WIDTHTRUNC */\n')
    for variant in ('four-state', 'full'):
        name = 'cp32-mmu-fram-'+variant
        command = (['iverilog', '-g2012', '-Wall', '-s', 'tb_mmu_fram',
                    '-Ptb_mmu_fram.FULL=0', '-o', 'build/'+name]+sources
                   if variant=='four-state' else
                   ['verilator', '--binary', '--timing', '-j', '4', '--top-module', 'tb_mmu_fram',
                    '--Mdir', 'build/obj-'+name]+sources[:-1]+[str(model)])
        with (ROOT/f'build/{name}-build.log').open('w') as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        run = ['vvp', 'build/'+name] if variant=='four-state' else ['build/obj-'+name+'/Vtb_mmu_fram']
        with (ROOT/f'build/{name}.log').open('w') as log:
            subprocess.run(run, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        print((ROOT/f'build/{name}.log').read_text(), flush=True)
    assert before==hashes(), 'sources changed while testing'
    (ROOT/'build/cp32-mmu-fram-inputs.json').write_text(json.dumps(before, indent=2)+'\n')


if __name__=='__main__':
    main()
