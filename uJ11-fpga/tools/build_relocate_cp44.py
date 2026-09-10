#!/usr/bin/env python3
"""CP44 measured kernel relocation candidate, not a complete J11 MMU.

Build copies only: MMR0 software controls, common-ALU PAR addition, PA22/NXM,
128 KiB FRAM and existing private RK ROM/physical low-64K DMA bypass.
No PDR check/W/abort/restart, modes or split I/D in this candidate.
"""
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from board_common import ROOT
from build_mmu_entry import change
from build_mmr3_cp43 import adapt as old_adapt
sys.path.insert(0,str(ROOT/'microasm'))
from uj11asm import assemble as native_assemble
from uj11relocasm import assemble
from make_ebr import generate
OUT=ROOT/'build/cp44-relocate'


def adapt(core,board):
    core,board=old_adapt(core,board)
    names={'uj11_core.v','uj11_engine.v','uj11_mmu_entry.v','uj11_board.v','uj11_board_bus.v'}
    def path(p):
        name=Path(p).name
        if name=='uj11_mmu_entry.v':return 'rtl/experimental/uj11_mmu_entry_cp44.v'
        return str(OUT.relative_to(ROOT)/name) if name in names else p
    return [path(p) for p in core],[path(p) for p in board]+['rtl/experimental/uj11_mmr0_control.v']


def main():
    subprocess.run([sys.executable,str(ROOT/'tools/build_mmr3_cp43.py')],cwd=ROOT,check=True)
    OUT.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'microcode/generated/full.uasm').read_text()
    helper=(ROOT/'microcode/mmu_relocate_cp44.uasm').read_text()
    old,listing,old_labels,_=native_assemble(source)
    image,new_listing,labels,stats=assemble(source+'\n'+helper)
    used={int(line.split()[0],16) for line in listing.splitlines()}
    assert all(image[a]==old[a] for a in used)
    assert all(labels[k]==v for k,v in old_labels.items())
    assert stats['used_words']==970
    assert labels['MMU_ENTRY']==0x1cd
    for name,data in [('relocate.uasm',source+'\n'+helper),('relocate.lst',new_listing),
                      ('relocate.mem',''.join(f'{w:09x}\n' for w in image)),
                      ('relocate.labels.json',json.dumps(labels,indent=2)+'\n'),
                      ('relocate.stats.json',json.dumps(stats,indent=2)+'\n'),
                      ('uj11_m0_ebr.v',generate(image))]:
        (OUT/name).write_text(data)
    for name in ('uj11_core.v','uj11_engine.v'):
        text=(ROOT/'build/cp39-csr'/name).read_text()
        text=change(text,'mmu_enabled, mmu_hold,','mmu_enabled, mmu_hold, map22,')
        text=change(text,'output wire [15:0] mem_addr,','output wire [21:0] mem_addr,\n    output wire [15:0] mem_va,')
        if name=='uj11_core.v':
            text=change(text,'.mmu_enabled(mmu_enabled),','.map22(map22),.mmu_enabled(mmu_enabled),')
            text=change(text,'.mem_addr(mem_addr),','.mem_va(mem_va),.mem_addr(mem_addr),')
        else:
            text=change(text,'    reg [2:0] mmu_page;', '    reg [3:0] mmu_memory_a;\n    reg [15:0] mmu_block_address;\n    wire mmu_resumed;')
            text=change(text,"assign apr_address={3'b000,mmu_page,uword[6]};","assign apr_address={3'b000,read_a[15:13],uword[6]};")
            text=change(text,'if (mmu_redirect && !mmu_active) mmu_page <= read_a[15:13];', '''if (mmu_redirect && !mmu_active) mmu_memory_a <= a;
        if (mmu_redirect && mmu_active)
            mmu_block_address <= {(map22 ? read_a[15:12] : {4{&read_a[11:7]}}),read_a[11:0]};''')
            text=change(text,'uj11_mmu_entry entry(', 'uj11_mmu_entry_cp44 entry(')
            text=change(text,'.active(mmu_active),', '.active(mmu_active),.resumed(mmu_resumed),')
            text=change(text,'wire [3:0] a = select_register(a_select,{ir[8:6],ir[2:0]});',
                        'wire [3:0] a = a_select[4] && a_select[3] ? mmu_memory_a :\n                   select_register(a_select,{ir[8:6],ir[2:0]});')
            text=change(text,'    uj11_mem memory(', '''    assign mem_addr=mmu_resumed ? {mmu_block_address,mem_va[5:0]} :
                        {{6{&mem_va[15:13]}},mem_va};
    uj11_mem memory(''')
            text=change(text,'.addr(mem_addr),','.addr(mem_va),')
            text=change(text,'build/cp39-csr/lookup.mem','build/cp44-relocate/relocate.mem')
        (OUT/name).write_text(text)
    board=(ROOT/'build/cp39-csr/uj11_board.v').read_text()
    board=change(board,'// CP39 experimental APR CSR + lookup. No address translation or MMRs.',
                 '// CP44 kernel relocation cost gate; no PDR protection/abort/restart.')
    board=change(board,'wire [15:0] address, data, rdata, lane_data, lane_rdata;',
                 'wire [21:0] address;\n    wire [15:0] virtual_address, data, rdata, lane_data, lane_rdata;\n    wire mmu_enabled, map22, mmu_bypass;')
    board=change(board,".mmu_enabled(1'b1)",'.mmu_enabled(mmu_enabled && !mmu_bypass),.map22(map22),.mem_va(virtual_address)')
    board=change(board,'.byte_select(lanes),.address(address),', '''.mmu_enabled(mmu_enabled),.map22(map22),.mmu_bypass(mmu_bypass),
        .virtual_address(virtual_address),.memory_writing(uword[35] && uword[34:31]==4'd12),
        .byte_select(lanes),.address(address),''')
    (OUT/'uj11_board.v').write_text(board)
    bus=(ROOT/'build/cp43-mmr3/uj11_board_bus.v').read_text()
    bus=change(bus,'input  wire [15:0] address,', '''input  wire [21:0] address,
    input wire [15:0] virtual_address,
    input wire memory_writing,
    output wire mmu_enabled, map22, mmu_bypass,''')
    bus=change(bus,'BOOT_ROM_ENABLE && boot_overlay_active && !write &&',
               'BOOT_ROM_ENABLE && address[21:16]==0 && boot_overlay_active && !write &&')
    bus=change(bus,'BOOT_ROM_ENABLE && SD_BOOT_ENABLE &&\n\t\tboot_overlay_active && !write &&',
               'BOOT_ROM_ENABLE && SD_BOOT_ENABLE && address[21:16]==0 &&\n\t\tboot_overlay_active && !write &&')
    bus=change(bus,'wire io_page = &address[15:13];','wire io_page = &address[21:13];')
    bus=change(bus,".physical_address({6'b111111,address})",'.physical_address(address)')
    bus=change(bus,'wire service_program_selected = RK_SERVICE_ENABLE && rk_service_active &&',
               'wire service_program_selected = RK_SERVICE_ENABLE && rk_service_active && io_page &&')
    bus=change(bus,'boot_release_armed && request && !write && word_address == 0',
               'boot_release_armed && request && !write && address == 0')
    bus=change(bus,'wire guest_fram_selected = (!io_page && !boot_selected) ||',
               "wire guest_fram_selected = (address[21:17]==0 && !boot_selected) ||")
    bus=change(bus,".byte_access(fram_byte_access), .bank(1'b0),", ".byte_access(fram_byte_access), .bank(service_dma_selected ? 1'b0 : address[16]),")
    bus=change(bus,'    wire [5:0] mmr3_value;', '''    // Bypass only private ROM reads and the physical MOVB copy operand.
    // Kernel vector/frame/stack traffic otherwise remains translated.
    wire private_copy=RK_SERVICE_ENABLE && rk_service_active && rk_service_movb &&
        ((!rk_write_command && memory_writing) ||
         (rk_write_command && !memory_writing && !instruction_fetch));
    assign mmu_bypass=private_copy || (RK_SERVICE_ENABLE && rk_service_active &&
        !memory_writing && virtual_address>=SERVICE_BASE && virtual_address<=SERVICE_RTI+1);
    wire mmr0_selected=cpu_io_page && word_address[12:0]==13'o17572;
    wire [15:0] mmr0_value;
    wire mmr0_ready;
    uj11_mmr0_control mmr0(.clk(clk),.reset(rst || peripheral_reset),
        .request(request && mmr0_selected),.writing(write),.byte_enable(byte_select),
        .write_data(wdata),.value(mmr0_value),.enabled(mmu_enabled),.ready(mmr0_ready));
    assign map22=mmr3_value[4];
    wire [5:0] mmr3_value;''')
    bus=change(bus,'\twire [15:0] small_rdata =','\twire [15:0] small_rdata = (mmr0_value & {16{mmr0_selected}}) |')
    bus=change(bus,'\tassign acknowledge =','\tassign acknowledge = mmr0_ready ||')
    (OUT/'uj11_board_bus.v').write_text(bus)
    inputs=['tools/build_relocate_cp44.py','microcode/mmu_relocate_cp44.uasm','microasm/uj11relocasm.py',
            'rtl/experimental/uj11_mmu_entry_cp44.v','rtl/experimental/uj11_mmr0_control.v',
            'build/cp43-mmr3/inputs.json','build/cp43-mmr3/uj11_board_bus.v',
            'build/cp39-csr/uj11_board.v','build/cp39-csr/uj11_core.v','build/cp39-csr/uj11_engine.v',
            'microcode/generated/full.uasm']
    record=dict(scope='Kernel-unified relocation cost gate; MMR0 software controls; no PDR check/W/abort/restart',
                microcode_words=970,helper_words=16,retained_address='RF selector4 + normalized physical block16',
                inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
                outputs_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in sorted(OUT.iterdir()) if p.is_file() and p.name!='inputs.json'})
    (OUT/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    print('PASS CP44 build: 954 guest words intact; 16 helper words; PA22 and FRAM17, kernel unified only')


if __name__=='__main__':main()
