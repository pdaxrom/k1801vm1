#!/usr/bin/env python3
"""CP43 unit, complete board CSR/DMA decode and actual CPU regressions."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from board_common import ROOT, CORE, BOARD
from build_mmr3_cp43 import adapt
from build_mmu_entry import change


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('suite',choices=('unit','bus','cpu','oracle','apr'))
    parser.add_argument('--vendor',action='store_true')
    args=parser.parse_args()
    assert not (args.vendor and args.suite in ('unit','oracle'))
    out=ROOT/'build/cp43-tests';out.mkdir(exist_ok=True)
    core,board=adapt(CORE,BOARD)
    dependencies=['tools/check_mmr3_cp43.py','tools/build_mmr3_cp43.py','build/cp43-mmr3/inputs.json',
                  'build/cp39-csr/inputs.json','build/cp39-csr/lookup.mem',
                  'microcode/generated/firmware.mem','microcode/generated/decode.mem']
    if args.suite=='oracle':
        oracle=['tb/reference_mmr3_cp43.c','../core/core.c','../core/core.h','../core/pdp11_fp.c','../core/hardware.c','../core/hardware.h']
        with (ROOT/'build/cp43-oracle-cc.log').open('w') as log:
            subprocess.run(['cc','-O2','-Wall','-DENABLE_MMU=1','-I..',oracle[0],'../core/hardware.c','-lm','-o','build/cp43-oracle'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (ROOT/'build/cp43-mmr3-oracle.txt').open('w') as corpus:
            subprocess.run(['build/cp43-oracle'],cwd=ROOT,stdout=corpus,check=True)
        dependencies+=oracle+['build/cp43-mmr3-oracle.txt']
        tb='tb_mmr3_oracle_cp43'
        sources=['tb/tb_mmr3_oracle_cp43.v','rtl/experimental/uj11_mmr3.v']
    elif args.suite=='unit':
        tb='tb_mmr3_cp43'
        sources=['tb/tb_mmr3_cp43.v','rtl/experimental/uj11_mmr3.v']
    elif args.suite=='bus':
        tb='tb_board_bus'
        source=(ROOT/'tb/tb_board_bus.v').read_text()
        source=change(source, '#(CHECK_SELECTORS!=0 ? 20000000 : 1000000);', '#20000000;')
        source=change(source,'    uj11_board_bus #', '''    wire apr_request,apr_pdr,apr_ack;
    wire [5:0] apr_entry;
    wire [15:0] apr_data;
    uj11_mmu_apr_shared apr(.clk(clk),.reset(reset),.request(apr_request),
        .entry(apr_entry),.pdr_select(apr_pdr),.writing(writing),.mark_written(1'b0),
        .byte_enable(lanes),.write_data(data),.read_data(apr_data),.ready(apr_ack),.busy(),
        .lookup_request(1'b0),.lookup_address(7'b0),.lookup_grant());
    uj11_board_bus #''')
        source=change(source,'.rdata(value),', '.apr_request(apr_request),.apr_pdr(apr_pdr),.apr_entry(apr_entry),.apr_data(apr_data),.apr_ack(apr_ack),.rdata(value),')
        source=change(source,'        beat(0,3,16\'o177750,0,0);if(answer!=16\'o31)$fatal(1,"MAINT layout");', '''        beat(0,3,16'o172516,0,0);if(answer!==0)$fatal(1,"MMR3 reset value");
        // Exercise every 16-bit write value through the actual board mux.
        for(i=0;i<65536;i=i+1)begin
            beat(1,3,16'o172516,i[15:0],0);
            beat(0,3,16'o172516,0,0);if(answer!=={10'b0,i[5:0]})$fatal(1,"MMR3 reserved bits");
        end
        beat(1,2,16'o172517,16'hff00,0);
        beat(1,0,16'o172516,0,0);
        beat(0,3,16'o172516,0,0);if(answer!==16'o77)$fatal(1,"MMR3 byte mask");
        beat(1,1,16'o172516,16'hff95,0);
        beat(0,3,16'o172516,0,0);if(answer!==16'o25)$fatal(1,"MMR3 low lane");
        @(negedge clk);peripheral_reset=1;@(negedge clk);peripheral_reset=0;
        beat(0,3,16'o172516,0,0);if(answer!==0)$fatal(1,"MMR3 peripheral RESET");
        beat(1,3,16'o172516,16'o53,0);
        beat(1,3,16'o172340,16'h5678,0);
        beat(0,3,16'o172516,0,0);if(answer!==16'o53)$fatal(1,"APR changed MMR3");
        beat(0,3,16'o177750,0,0);if(answer!=16'o31)$fatal(1,"MAINT layout");''')
        source=change(source,"        beat(0,3,16'o160476,0,1);", '''        beat(1,1,16'o172516,16'h00ab,0);
        if(memory.memory[16'o172516]!=8'hab)$fatal(1,"DMA intercepted by MMR3 CSR");
        beat(0,3,16'o160476,0,1);''')
        source=change(source,'        $display("PASS board bus:', '''        beat(0,3,16'o172516,0,0);if(answer!==16'o53)$fatal(1,"DMA modified MMR3");
        beat(0,3,16'o172340,0,0);if(answer!==16'h5678)$fatal(1,"MMR3 modified APR");
        // Both bytes, every VA, service/direction/fetch combinations. This
        // independently excludes 177516 and physical RK aliases.
        @(negedge clk);run_clock=0;request=1;
        for(scenario=0;scenario<32;scenario=scenario+1)begin
            dut.rk_service_active=scenario[0];dut.rk_service_movb=scenario[1];
            dut.rk_write_command=scenario[2];writing=scenario[3];fetch=scenario[4];
            for(probe=0;probe<65536;probe=probe+1)begin
                address=probe[15:0];#1;
                if(dut.mmr3_selected !==
                   ((probe==16'o172516 || probe==16'o172517) &&
                    !(scenario[0] && scenario[1] &&
                      ((!scenario[2] && scenario[3]) || (scenario[2] && !scenario[3] && !scenario[4])))))
                    $fatal(1,"MMR3 decode/DMA state%0d addr%o",scenario,probe);
                if(dut.mmr3_selected && (apr_request || dut.fram_request || dut.uart_strobe ||
                   dut.sd_strobe || dut.firmware_selected || dut.panel_selected || dut.ltc_selected))
                    $fatal(1,"MMR3 overlapping decode");
                if(dut.mmr3_selected && value!==16'o53)$fatal(1,"MMR3 read mux");
                selector_checks=selector_checks+1;
            end
        end
        $display("PASS MMR3 bus: %0d decode/DMA combinations; 65536 data values, canonical CSR, lanes, RESET, APR isolation",selector_checks);
        $display("PASS board bus:''')
        path=out/'tb_board_bus.v';path.write_text(source)
        sources=[str(path.relative_to(ROOT))]+board+['build/cp39-csr/uj11_mmu_apr_shared.v',
            'rtl/uj11_mmu_apr_ram.v','rtl/uj11_mmu_apr_decode.v','reference/lsi11/spi_fram_model.v']
        dependencies+=['tb/tb_board_bus.v']
    else:
        tb='tb_mmu_apr_csr_cpu' if args.suite=='apr' else 'tb_mmr3_cpu_cp43'
        sources=['tb/'+tb+'.v']+core+board+['reference/lsi11/spi_fram_model.v']
        sources+=['build/cp39-csr/uj11_m0_ebr.v' if args.vendor else 'rtl/uj11_rom.v']
    if args.vendor:sources+=['build/vendor/'+name+'.v' for name in ('DP8KC','GSR','PUR')]
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(sources+dependencies))}
    tag='cp43-'+args.suite+('-vendor' if args.vendor else '-portable')
    with (ROOT/f'build/{tag}-build.log').open('w') as log:
        subprocess.run(['iverilog','-g2012','-Wall','-s',tb,'-o','build/'+tag]+
                       (['-DUJ11_VENDOR_ROM'] if args.vendor else [])+sources,
                       cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (ROOT/f'build/{tag}.log').open('w') as log:
        subprocess.run(['vvp','build/'+tag],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    if args.suite=='unit':
        with (ROOT/'build/cp43-unit-lint.log').open('w') as log:
            subprocess.run(['verilator','--lint-only','--Wall','--top-module','uj11_mmr3',
                            'rtl/experimental/uj11_mmr3.v'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in hashes}
    lines=[s for s in (ROOT/f'build/{tag}.log').read_text().splitlines() if s.startswith('PASS')]
    assert len(lines)==(1 if args.suite in ('unit','oracle') else 2),lines
    (ROOT/f'build/{tag}.json').write_text(json.dumps(dict(suite=args.suite,vendor=args.vendor,
        inputs_sha256=hashes,pass_lines=lines),indent=2)+'\n')
    print('\n'.join(lines),flush=True)


if __name__=='__main__':main()
