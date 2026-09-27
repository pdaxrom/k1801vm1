#!/usr/bin/env python3
"""RT-11XM disk -> extended-memory VM: -> binary comparison, using guest tools."""
import argparse
import json
import subprocess
from pathlib import Path
from board_common import ROOT
from build_mmu_board import build,CORE,BOARD,sha

def run(out,image):
    out.mkdir(parents=True,exist_ok=True)
    record=build();digest=sha(image)
    template=ROOT/'tests/mmu/tb_mmu_boot.v'
    text=template.read_text()
    before='''        check(dma_words>1000 && concurrent_fetches>100,"autonomous disk IO under OS");'''
    assert text.count(before)==1
    text=text.replace(before,'''        phase=5;shell("INITIALIZE/NOQUERY VM:");
        phase=6;shell("COPY RT11XM.SYS VM:MMUTST.SYS");
        phase=7;shell("DIFFERENCES/BINARY RT11XM.SYS VM:MMUTST.SYS");
        contains("No differences found");
        check(high_writes>27000 && high_reads>27000,"VM accesses extended physical RAM");
'''+before)
    text=text.replace('    reg clk=0,', '''    integer high_writes=0,high_reads=0;
    always @(posedge clk)if(phase>=5 && dut.request && dut.ready &&
        dut.address>=22'h40000 && dut.address<22'h200000)begin
        if(dut.writing)high_writes++;else high_reads++;
    end
    reg clk=0,''')
    tb=out/'tb.v';tb.write_text(text)
    inventory=CORE+BOARD+[str(tb.relative_to(ROOT)),
        'tests/models/async_sram_model.v','tests/models/spi_sd_model.v']
    hashes={p:sha(ROOT/p) for p in inventory+['tests/mmu/tb_mmu_boot.v','tools/test_mmu_vm.py']}
    command=['verilator','--binary','--timing','-Wno-WIDTH','-Wno-TIMESCALEMOD',
        '--top-module','tb_mmu_boot','-j','4','--Mdir',str(out/'obj')]+inventory
    with (out/'build.log').open('w') as log:subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:
        rc=subprocess.run([str(out/'obj/Vtb_mmu_boot'),'+MONITOR=xm',
            f'+SD_IMAGE={image}',f'+UART_LOG={out}/uart.txt'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT).returncode
    print((out/'simulation.log').read_text()[-4000:])
    if rc:raise SystemExit(rc)
    assert 'PASS MMU' in (out/'simulation.log').read_text()
    assert digest==sha(image)
    assert all(sha(ROOT/p)==h for p,h in hashes.items())
    (out/'result.json').write_text(json.dumps(dict(passed=True,hardware=record,files=hashes,
        sd_image=str(image),sd_image_sha256=digest),indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=ROOT/'build/test-mmu-vm')
    p.add_argument('--image',type=Path,default=ROOT/'../lsi11/disks/rt11v5.3/system.dsk')
    a=p.parse_args();run(a.out.resolve(),a.image.resolve())
