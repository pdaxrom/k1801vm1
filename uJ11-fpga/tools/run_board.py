#!/usr/bin/env python3
"""Run actual cold uJ11 board + FRAM + SD bootstrap against a read-only RT-11 image."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from board_common import ROOT, CORE, BOARD


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image',type=Path,default=ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    args=p.parse_args()
    assert args.image.exists()
    sources=CORE+BOARD+['rtl/uj11_rom.v','reference/lsi11/spi_fram_model.v','reference/lsi11/spi_sd_model_cp28.v']
    source_hashes={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in
                   sources+['tb/tb_board_rt11.v','tools/run_board.py','tools/board_common.py','microcode/generated/m0.mem','microcode/generated/firmware.mem','microcode/generated/decode.mem']}
    image_hash=hashlib.sha256(args.image.read_bytes()).hexdigest()
    manifest=dict(files=source_hashes,image_sha256=image_hash,image_bytes=args.image.stat().st_size,
                  mode='portable Verilator; cold CPU reset; actual UART wire scoreboard; SD read-only backing + RAM overlay')
    (ROOT/'build/cp28-board-inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
    # Freeze the imported models; scope their established implicit-width
    # conventions to those files only. New board/core RTL keeps fatal warnings.
    copies=ROOT/'build/board-reference'; copies.mkdir(exist_ok=True)
    for i, name in enumerate(sources):
        if name.startswith('reference/'):
            data=(ROOT/name).read_text()
            path=copies/Path(name).name
            path.write_text('/* verilator lint_off WIDTH */\n'+data+'\n/* verilator lint_on WIDTH */\n')
            sources[i]=str(path)
    command=['verilator','--binary','--timing','--top-module','tb_board_rt11','-j','4',
             '--Mdir','build/obj-board-rt11','tb/tb_board_rt11.v']+sources
    with (ROOT/'build/cp28-board-build.log').open('w') as log:
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (ROOT/'build/cp28-board-rt11.log').open('w') as log:
        subprocess.run(['build/obj-board-rt11/Vtb_board_rt11',f'+SD_IMAGE={args.image.resolve()}','+TRACE_RK'],
                       cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    assert hashlib.sha256(args.image.read_bytes()).hexdigest()==image_hash, 'backing SD image modified'
    for n,h in source_hashes.items():assert hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==h,n
    print((ROOT/'build/cp28-board-rt11.log').read_text())


if __name__=='__main__':main()
