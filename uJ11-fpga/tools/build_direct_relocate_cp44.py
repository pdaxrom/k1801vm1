#!/usr/bin/env python3
"""Compare a small synchronous PAR bridge with CP44 microcoded relocation.

The CPU, ALU and production microcode remain unchanged. The address adder
belongs only to MMU; addressing modes and PDP11 instructions stay microcoded.
"""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from board_common import ROOT,CORE,BOARD
from build_mmu_entry import change
from build_relocate_cp44 import main as build_microcoded
OUT=ROOT/'build/cp44-direct'


def adapt(core,board):
    extras=['rtl/experimental/uj11_mmu_relocate.v','rtl/uj11_mmu_apr_ram.v',
            'rtl/uj11_mmu_apr_decode.v','build/cp39-csr/uj11_mmu_apr_shared.v']
    return list(core)+extras,[str(OUT.relative_to(ROOT)/Path(p).name) if Path(p).name in {'uj11_board.v','uj11_board_bus.v'} else p
                             for p in board]+['rtl/experimental/uj11_mmr0_control.v','rtl/experimental/uj11_mmr3.v']


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--classified',action='store_true')
    args=parser.parse_args()
    build_microcoded()
    OUT.mkdir(parents=True,exist_ok=True)
    board=(ROOT/'build/cp44-relocate/uj11_board.v').read_text()
    board=change(board,'// CP44 kernel relocation cost gate; no PDR protection/abort/restart.',
                 '// CP44 direct relocation cost gate; original CPU, no PDR/abort/restart.')
    board=change(board,'wire bus_request=request && !turnaround;',
                 'wire translated_request;\n    wire bus_request=translated_request && !turnaround;')
    start=board.index('        .mmu_enabled(')
    end=board.index('.clk(clk),.reset(reset),.irq_valid(irq_valid)',start)
    board=board[:start]+'        '+board[end:]
    board=change(board,'.mem_addr(address),','.mem_addr(virtual_address),')
    board=change(board,'    uj11_mmu_apr_shared apr(', '''    uj11_mmu_relocate relocate(.clk(clk),.reset(reset),.enabled(mmu_enabled && !mmu_bypass),
        .map22(map22),.request(request),.acknowledge(acknowledge),.virtual_address(virtual_address),
        .lookup_request(apr_request),.lookup_address(apr_address),.lookup_grant(apr_grant),.lookup_data(apr_data),
        .translated_request(translated_request),.physical_address(address));
    uj11_mmu_apr_shared apr(''')
    bus=(ROOT/'build/cp44-relocate/uj11_board_bus.v').read_text()
    if args.classified:
        board=change(board,'wire mmu_enabled, map22, mmu_bypass;',
                     'wire mmu_enabled, map22, mmu_bypass, ram_region, io_region;')
        board=change(board,'.translated_request(translated_request),',
                     '.ram_region(ram_region),.io_region(io_region),.translated_request(translated_request),')
        board=change(board,'.byte_select(lanes),.address(address),',
                     '.ram_region(ram_region),.io_region(io_region),.byte_select(lanes),.address(address[16:0]),')
        bus=change(bus,'input  wire [21:0] address,','input wire ram_region, io_region,\n    input wire [16:0] address,')
        bus=bus.replace('address[21:16]==0','ram_region && !address[16]')
        bus=change(bus,'wire io_page = &address[21:13];','wire io_page = io_region;')
        bus=change(bus,'.physical_address(address)',".physical_address({6'b111111,address[15:0]})")
        bus=change(bus,'address[21:17]==0 && !boot_selected','ram_region && !boot_selected')
        bus=change(bus,'boot_release_armed && request && !write && address == 0',
                   'boot_release_armed && request && !write && ram_region && address == 0')
        bus=change(bus,'virtual_address>=SERVICE_BASE && virtual_address<=SERVICE_RTI+1',
                   'virtual_address[15:9]==SERVICE_BASE[15:9]')
    else:
        board=change(board,'.translated_request(translated_request),',
                     '.ram_region(),.io_region(),.translated_request(translated_request),')
    (OUT/'uj11_board.v').write_text(board)
    (OUT/'uj11_board_bus.v').write_text(bus)
    inputs=['tools/build_direct_relocate_cp44.py','rtl/experimental/uj11_mmu_relocate.v',
            'build/cp44-relocate/inputs.json','build/cp44-relocate/uj11_board.v','build/cp44-relocate/uj11_board_bus.v']
    record=dict(scope='Direct kernel-unified PAR bridge, no PDR/W/abort/restart',microcode_words=954,classified=args.classified,
                inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
                outputs_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in sorted(OUT.iterdir()) if p.is_file() and p.name!='inputs.json'})
    (OUT/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    print('PASS CP44 direct build: original 954-word CPU; synchronous PAR bridge and PA22')


if __name__=='__main__':main()
