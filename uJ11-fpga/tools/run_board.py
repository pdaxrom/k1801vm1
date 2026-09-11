#!/usr/bin/env python3
"""Run actual cold uJ11 board + FRAM + SD bootstrap against a read-only RT-11 image."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
from board_common import ROOT, CORE, BOARD, MMU, profile_flags


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image',type=Path,default=ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    p.add_argument('--tag',default='cp28')
    p.add_argument('--vendor',action='store_true')
    p.add_argument('--mmu',action='store_true',help='Enable the retained, incomplete CP47c MMU prototype')
    p.add_argument('--fram-cp52',action='store_true',help='Experimental native sequential FRAM READ')
    p.add_argument('--cursor-cp53',choices=['increment','compare','both'],help='Equivalent native FRAM cursor mapping')
    args=p.parse_args()
    assert sum(bool(x) for x in (args.mmu,args.fram_cp52,args.cursor_cp53))<=1, 'Choose one board profile'
    assert re.fullmatch(r'[a-z0-9-]+',args.tag)
    assert args.image.exists()
    sources=CORE+BOARD+['rtl/uj11_rom.v','reference/lsi11/spi_fram_model.v','reference/lsi11/spi_sd_model_cp28.v']
    if args.fram_cp52:
        from build_fram_cp52 import adapt
        core,board=adapt()
        sources=core+board+['rtl/uj11_rom.v','reference/lsi11/spi_fram_model.v','reference/lsi11/spi_sd_model_cp28.v']
    if args.cursor_cp53:
        from build_cursor_cp53 import adapt
        core,board=adapt(args.cursor_cp53)
        sources=core+board+['rtl/uj11_rom.v','reference/lsi11/spi_fram_model.v','reference/lsi11/spi_sd_model_cp28.v']
    if args.mmu:sources+=MMU
    if args.vendor:
        sources.remove('rtl/uj11_rom.v')
        vendor=Path(os.environ.get('LATTICE_SIM_DIR',ROOT/'build/vendor'))
        sources+=['microcode/generated/uj11_m0_ebr.v']+[str(vendor/(n+'.v')) for n in ('DP8KC','GSR','PUR')]
    testbench='tb/tb_board_rt11.v'
    if args.fram_cp52 or args.cursor_cp53:
        from build_fram_cp52 import replace_once
        testbench=f'build/{args.tag}-tb_board_rt11.v'
        (ROOT/testbench).write_text(replace_once((ROOT/'tb/tb_board_rt11.v').read_text(),
            'endmodule',(ROOT/'tb/board_fram_scoreboard.vh').read_text()+'\nendmodule'))
    source_hashes={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in
                   sources+[testbench,'tb/tb_board_rt11.v','tools/run_board.py','tools/board_common.py','microcode/generated/m0.mem','microcode/generated/firmware.mem','microcode/generated/decode.mem']}
    if args.fram_cp52:
        for n in ['tb/board_fram_scoreboard.vh','tools/build_fram_cp52.py','build/cp52-fram/inputs.json']:
            source_hashes[n]=hashlib.sha256((ROOT/n).read_bytes()).hexdigest()
    if args.cursor_cp53:
        for n in ['tb/board_fram_scoreboard.vh','tools/build_fram_cp52.py','tools/build_cursor_cp53.py',
                  'build/cp53-cursor/inputs.json','synth/reports/cp52b/inputs.json','synth/reports/cp52b/source.tgz']:
            source_hashes[n]=hashlib.sha256((ROOT/n).read_bytes()).hexdigest()
    image_hash=hashlib.sha256(args.image.read_bytes()).hexdigest()
    manifest=dict(files=source_hashes,image_sha256=image_hash,image_bytes=args.image.stat().st_size,
                  mmu=args.mmu,fram_cp52=args.fram_cp52,cursor_cp53=args.cursor_cp53,defines=profile_flags(args.mmu),
                  mode=('vendor DP8KC' if args.vendor else 'portable')+' Verilator; cold CPU reset; actual UART wire scoreboard; SD read-only backing + RAM overlay')
    (ROOT/f'build/{args.tag}-board-inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
    # Freeze the imported models; scope their established implicit-width
    # conventions to those files only. New board/core RTL keeps fatal warnings.
    copies=ROOT/f'build/{args.tag}-board-reference'; copies.mkdir(exist_ok=True)
    for i, name in enumerate(sources):
        if name.startswith('reference/'):
            data=(ROOT/name).read_text()
            path=copies/Path(name).name
            path.write_text('/* verilator lint_off WIDTH */\n'+data+'\n/* verilator lint_on WIDTH */\n')
            sources[i]=str(path)
    if args.vendor:
        # The unmodified Lattice model uses procedural assign/deassign that
        # Verilator does not implement. Keep its four-state semantics in Icarus.
        command=['iverilog','-g2012','-DUJ11_VENDOR_ROM','-s','tb_board_rt11',
                 '-o',f'build/{args.tag}-board.vvp',testbench]+sources
        executable=['vvp',f'build/{args.tag}-board.vvp']
        manifest['mode']=manifest['mode'].replace('Verilator','Icarus (unmodified Lattice models)')
        (ROOT/f'build/{args.tag}-board-inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
    else:
        command=['verilator','--binary','--timing','--top-module','tb_board_rt11','-j','4',
                 '--Mdir',f'build/obj-{args.tag}-board-rt11',testbench]+sources
        executable=[f'build/obj-{args.tag}-board-rt11/Vtb_board_rt11']
    with (ROOT/f'build/{args.tag}-board-build.log').open('w') as log:
        command+=profile_flags(args.mmu)
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (ROOT/f'build/{args.tag}-board-rt11.log').open('w') as log:
        subprocess.run(executable+[f'+SD_IMAGE={args.image.resolve()}',
                        f'+UART_LOG=build/{args.tag}-uart.txt','+TRACE_RK'],
                       cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    assert hashlib.sha256(args.image.read_bytes()).hexdigest()==image_hash, 'backing SD image modified'
    for n,h in source_hashes.items():assert hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==h,n
    print((ROOT/f'build/{args.tag}-board-rt11.log').read_text())


if __name__=='__main__':main()
