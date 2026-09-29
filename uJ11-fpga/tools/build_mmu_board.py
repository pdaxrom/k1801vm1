#!/usr/bin/env python3
"""Build an isolated HC7000 MMU board image for simulation/qualification."""
import hashlib
import json
import os
from board_common import ROOT,sources
from build_mmu import build as cpu_build,CORE,OUT
from build_iop_mmu import build as iop_build, profile as iop_profile
from build_mmu import fpp_mode,clock_mhz
from build_software import native
from iop_ebr import block

BOARD=['boards/hc7000/mmu/'+n+'.v' for n in (
    'uj11_mmu_board','uj11_mmu_board_bus','uj11_mmu_disk','uj11_mmu_rk611',
    'uj11_mmu_sector_engine','uj11_mmu_sram_arbiter','uj11_mmu_iop_bus')]
BOARD+=['boards/hc7000/uj11_sram.v','boards/hc7000/uj11_hg_inputs.v','boards/hc7000/uj11_diagnostics.v',
    'boards/hc1200/uj11_tick.v','boards/hc1200/uj11_button.v','boards/hc1200/uj11_panel.v',
    'rtl/peripherals/spi_byte_service.v','rtl/peripherals/wbc_uart_xo2.v',
    'build/hc7000-mmu-hardware/uj11_mmu_rom.v','build/hc7000-mmu-hardware/uj11_mmu_boot_rom.v',
    'build/hc7000-mmu-iop/uj11_mmu_iop_ram.v','build/hc7000-mmu-iop/uj11_sector_ram.v']
BOARD+=[p for p in sources('hc7000-lcd-sram')[1] if p.startswith('vendor/serv/')]
TOP_TEMPLATE='boards/hc7000/mmu/uj11_mmu_microcomp.v'
TOP='build/hc7000-mmu-hardware/uj11_mmu_microcomp.v'

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def diagnostics_enabled():
    value=os.environ.get('UJ11_HC7000_DIAGNOSTICS','0')
    if value not in ('0','1'):
        raise ValueError('UJ11_HC7000_DIAGNOSTICS must be 0 or 1')
    return value=='1'

def build():
    diagnostics=diagnostics_enabled()
    if iop_profile()=='storage' and fpp_mode()!='off':
        raise ValueError('IOP=storage requires FPP=off: storage firmware requires the compact no-FPP microstore')
    cpu=cpu_build();iop=iop_build()
    top=(ROOT/TOP_TEMPLATE).read_text()
    assert top.count('parameter integer CLOCK_HZ=24000000')==1
    assert top.count('parameter integer DIAGNOSTICS_ENABLE=0')==1
    top=top.replace('parameter integer DIAGNOSTICS_ENABLE=0',
                    f'parameter integer DIAGNOSTICS_ENABLE={int(diagnostics)}')
    (ROOT/TOP).write_text(top.replace('parameter integer CLOCK_HZ=24000000',
                                    f'parameter integer CLOCK_HZ={clock_mhz()*1000000}'))
    boot_source='firmware/boot/SDIOP.MAC' if iop_profile()=='storage' else 'firmware/boot/SDBASE.MAC'
    raw,symbols,assembly,directory=native(ROOT/boot_source)
    blob=raw[0o4000:symbols['IMEND']]
    assert 0<len(blob)<=512
    if iop_profile()=='legacy':assert len(blob)==426
    (OUT/'bootstrap.bin').write_bytes(blob)
    blob=blob.ljust(1024,b'\0')
    words=[int.from_bytes(blob[i:i+2],'little') for i in range(0,1024,2)]
    (OUT/'bootstrap.mem').write_text(''.join(f'{w:04x}\n' for w in words))
    source=['// Generated: ordinary SD bootstrap at 004000, no VM2 resident.',
        '`timescale 1ns/1ps',
        'module uj11_mmu_boot_rom(input wire clk,enable,input wire [8:0] address,output wire [15:0] data);',
        "wire [15:0] wd=0;wire [1:0] we=0;",
        '`ifdef SYNTHESIS','`define UJ11_BOOT_EBR','`elsif UJ11_VENDOR_ROM','`define UJ11_BOOT_EBR','`endif',
        '`ifdef UJ11_BOOT_EBR',block('boot','address','wd','we','data',words),
        '`else','reg [15:0] words[0:511];reg [15:0] value;',
        'initial $readmemh("build/hc7000-mmu-hardware/bootstrap.mem",words);',
        'always @(posedge clk)if(enable)value<=words[address];assign data=value;',
        '`endif','`undef UJ11_BOOT_EBR','endmodule','']
    (OUT/'uj11_mmu_boot_rom.v').write_text('\n'.join(source))
    record=dict(cpu=cpu,iop=iop,bootstrap=dict(assembly=assembly,directory=str(directory.relative_to(ROOT))),
        files={p:sha(ROOT/p) for p in CORE+BOARD+[TOP,TOP_TEMPLATE,'tools/build_mmu_board.py',boot_source]},
        scope='MMU board; physical-board qualification pending',fpp=cpu['fpp'],clock_mhz=clock_mhz(),
        diagnostics=diagnostics,
        fp_arithmetic=cpu['fpp']=='microcode',boot_pc_octal='004000',sram_bytes=2097152,dma_address_bits=22 if iop_profile()=='storage' else 18)
    (OUT/'board-inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    return record

if __name__=='__main__':build()
