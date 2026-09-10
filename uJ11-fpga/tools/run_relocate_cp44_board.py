#!/usr/bin/env python3
"""Cold FB regression for the CP44 direct relocation candidate; no XM claim."""
import hashlib
import json
import sys
import types
import run_board
from board_common import ROOT
from build_direct_relocate_cp44 import adapt


def main():
    tb=(ROOT/'tb/tb_board_rt11.v').read_text()
    tb=tb.replace("dut.address>=16'o001000", "dut.address>=22'o001000").replace("dut.address<16'o001056", "dut.address<22'o001056")
    tb=tb.replace("dut.address==16'o177440", "dut.bus.cpu_io_page && dut.address==22'o17777440")
    tb=tb.replace("dut.address==16'o177566", "dut.bus.cpu_io_page && dut.address==22'o17777566")
    tb=tb.replace('    integer clocks=0,', '    integer mapped_beats=0, high_ram_beats=0, mmr0_writes=0, mapped_dma_beats=0, mapped_rom_beats=0;\n    integer clocks=0,')
    tb=tb.replace('        if(dut.acknowledge)begin', '''        if(dut.acknowledge)begin
            if(dut.relocate.phase==2)mapped_beats<=mapped_beats+1;
            if(dut.address>=22'h10000 && dut.address<22'h20000)high_ram_beats<=high_ram_beats+1;
            if(dut.writing && dut.bus.mmr0_selected)mmr0_writes<=mmr0_writes+1;
            if(dut.mmu_enabled && dut.bus.private_copy)mapped_dma_beats<=mapped_dma_beats+1;
            if(dut.mmu_enabled && dut.mmu_bypass && !dut.bus.private_copy)mapped_rom_beats<=mapped_rom_beats+1;''')
    tb=tb.replace('            $finish;', '''            $display("MMU COUNTS mapped%0d highRAM%0d MMR0writes%0d enabledDMA%0d enabledPrivateROM%0d",mapped_beats,high_ram_beats,mmr0_writes,mapped_dma_beats,mapped_rom_beats);
            $finish;''')
    path=ROOT/'build/tb_board_rt11_cp44.v';path.write_text(tb)
    code=(ROOT/'tools/run_board.py').read_text().replace('tb/tb_board_rt11.v','build/tb_board_rt11_cp44.v')
    (ROOT/'build/run_board_cp44.py').write_text(code)
    driver=types.ModuleType('run_board_cp44');exec(compile(code,'build/run_board_cp44.py','exec'),driver.__dict__)
    driver.CORE,driver.BOARD=adapt(run_board.CORE,run_board.BOARD)
    extra=['tools/run_relocate_cp44_board.py','tools/build_direct_relocate_cp44.py','build/cp44-direct/inputs.json','tb/tb_board_rt11.v','build/tb_board_rt11_cp44.v','build/run_board_cp44.py']
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in extra}
    sys.argv=[sys.argv[0],'--tag','cp44'];driver.main()
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in hashes}
    report=json.loads((ROOT/'build/cp44-board-inputs.json').read_text());report['files'].update(hashes)
    report.update(kernel_relocation=True,pdr_protection=False,rt11_xm=False)
    (ROOT/'build/cp44-board-verified.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
