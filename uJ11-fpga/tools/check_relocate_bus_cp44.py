#!/usr/bin/env python3
"""Full PA22 bus decode and private-ROM/DMA bypass checks for CP44 direct."""
import hashlib
import json
import subprocess
from pathlib import Path
from board_common import ROOT,CORE,BOARD
from build_direct_relocate_cp44 import adapt
from build_mmu_entry import change


def main():
    out=ROOT/'build/cp44-bus';out.mkdir(exist_ok=True)
    _,board=adapt(CORE,BOARD)
    source=(ROOT/'tb/tb_board_bus.v').read_text()
    source=source.replace("movb_address=16'o160000","movb_address=32'o160000").replace("memory.memory[16'o177566]","memory.memory[17'o177566]")
    source=change(source,'reg [1:0] lanes=3;reg [15:0] address=0,data=0;',
                  'reg [1:0] lanes=3;reg [21:0] address=0;reg [15:0] data=0;')
    a=source.index('    task beat(');b=source.index('    task accept_irq;',a)
    task=source[a:b]
    physical=task.replace('task beat(', 'task physical_beat(').replace('input [15:0] addr,datum,','input [21:0] addr,input [15:0] datum,')
    source=source[:a]+task.replace('address=addr;', "address={{6{&addr[15:13]}},addr};")+physical+source[b:]
    source=change(source,'    uj11_board_bus #', '''    wire apr_request,apr_pdr,apr_ack,mmu_enabled,map22,mmu_bypass;
    wire [5:0] apr_entry;wire [15:0] apr_data;
    uj11_mmu_apr_shared apr(.clk(clk),.reset(reset),.request(apr_request),
        .entry(apr_entry),.pdr_select(apr_pdr),.writing(writing),.mark_written(1'b0),
        .byte_enable(lanes),.write_data(data),.read_data(apr_data),.ready(apr_ack),.busy(),
        .lookup_request(1'b0),.lookup_address(7'b0),.lookup_grant());
    uj11_board_bus #''')
    source=change(source,'.rdata(value),', '''.virtual_address(address[15:0]),.memory_writing(writing),
        .mmu_enabled(mmu_enabled),.map22(map22),.mmu_bypass(mmu_bypass),
        .apr_request(apr_request),.apr_pdr(apr_pdr),.apr_entry(apr_entry),.apr_data(apr_data),.apr_ack(apr_ack),.rdata(value),''')
    source=change(source,"        beat(1,3,16'o177440,16'o101,0);", '''        physical_beat(1,3,22'h1fffe,16'hcafe,0);
        physical_beat(0,3,22'h1fffe,0,0);if(answer!==16'hcafe)$fatal(1,"FRAM17 final word");
        if(memory.memory[65534]!=0 || memory.memory[65535]!=0)$fatal(1,"high FRAM alias");
        beat(1,3,16'o177440,16'o101,0);''')
    a=source.index('        if(CHECK_SELECTORS!=0)begin');b=source.index('        $finish;',a)
    source=source[:a]+'''        @(negedge clk);run_clock=0;request=1;writing=0;fetch=0;
        dut.rk_service_active=0;
        for(probe=0;probe<4194304;probe=probe+1)begin
            address=probe[21:0];#1;
            if(dut.io_page!==(probe>=22'h3fe000))$fatal(1,"PA22 I/O classification");
            if(dut.guest_fram_selected!==((probe<22'h20000) && !dut.boot_selected))
                $fatal(1,"PA22 FRAM classification %o",probe);
            if(dut.mmr0_selected!==((probe & ~1)==32'o17777572) ||
               dut.mmr3_selected!==((probe & ~1)==32'o17772516))$fatal(1,"MMR physical alias");
            if(probe>=22'h20000 && probe<22'h3fe000 && (ack || dut.fram_request || dut.firmware_selected))
                $fatal(1,"NXM dispatched to a device %o",probe);
            selector_checks=selector_checks+1;
        end
        for(scenario=0;scenario<32;scenario=scenario+1)begin
            dut.rk_service_active=scenario[0];dut.rk_service_movb=scenario[1];
            dut.rk_write_command=scenario[2];writing=scenario[3];fetch=scenario[4];
            for(probe=0;probe<65536;probe=probe+1)begin
                address={{6{&probe[15:13]}},probe[15:0]};#1;
                if(mmu_bypass!==
                   (scenario[0] && ((scenario[1] &&
                      ((!scenario[2] && scenario[3]) || (scenario[2] && !scenario[3] && !scenario[4]))) ||
                    (!scenario[3] && probe>=16'o160000 && probe<=16'o160477))))
                    $fatal(1,"MMU private bypass scenario%0d VA%o",scenario,probe);
                if(dut.service_dma_selected && (dut.mmr0_selected || dut.mmr3_selected || apr_request || !dut.fram_request))
                    $fatal(1,"physical DMA intercepted by CSR");
                selector_checks=selector_checks+1;
            end
        end
        $display("PASS CP44 PA/bypass: %0d combinations; full PA22/NXM/CSR decode and 32 private service states over every VA",selector_checks);
''' + source[b:]
    source=source.replace('#(CHECK_SELECTORS!=0 ? 20000000 : 1000000);','#7000000;')
    path=out/'tb_board_bus.v';path.write_text(source)
    sources=[str(path.relative_to(ROOT))]+board+['build/cp39-csr/uj11_mmu_apr_shared.v',
             'rtl/uj11_mmu_apr_ram.v','rtl/uj11_mmu_apr_decode.v','reference/lsi11/spi_fram_model.v']
    dependencies=['tools/check_relocate_bus_cp44.py','tb/tb_board_bus.v','tools/build_direct_relocate_cp44.py',
                  'build/cp44-direct/inputs.json','microcode/generated/firmware.mem']
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources+dependencies}
    compiled=sources.copy()
    for i,p in enumerate(compiled):
        if p.startswith('reference/'):
            copy=out/Path(p).name;copy.write_text('/* verilator lint_off WIDTH */\n'+(ROOT/p).read_text()+'\n/* verilator lint_on WIDTH */\n');compiled[i]=str(copy)
    with (ROOT/'build/cp44-bus-build.log').open('w') as log:
        subprocess.run(['verilator','--binary','--timing','--top-module','tb_board_bus','-j','4',
                        '--Mdir','build/obj-cp44-bus']+compiled,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (ROOT/'build/cp44-bus.log').open('w') as log:
        subprocess.run(['build/obj-cp44-bus/Vtb_board_bus'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in hashes}
    lines=[s for s in (ROOT/'build/cp44-bus.log').read_text().splitlines() if s.startswith('PASS')]
    assert len(lines)==2,lines
    (ROOT/'build/cp44-bus.json').write_text(json.dumps(dict(inputs_sha256=hashes,pass_lines=lines),indent=2)+'\n')
    print('\n'.join(lines))


if __name__=='__main__':main()
