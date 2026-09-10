#!/usr/bin/env python3
"""CP39 shared-port, board decode/DMA and actual CPU CSR regressions."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from board_common import ROOT, CORE, BOARD
from build_mmu_apr_csr import adapt
from build_mmu_entry import change


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('suite', choices=('port','bus','cpu'))
    parser.add_argument('--vendor', action='store_true')
    args=parser.parse_args()
    out=ROOT/'build/cp39-tests';out.mkdir(exist_ok=True)
    core,board=adapt(CORE,BOARD)
    dependencies=['tools/check_mmu_apr_csr.py','tools/build_mmu_apr_csr.py','build/cp39-csr/inputs.json',
                  'build/cp39-csr/lookup.mem','microcode/generated/firmware.mem','microcode/generated/decode.mem']
    if args.suite=='port':
        tb='tb_mmu_apr'
        source=(ROOT/'tb/tb_mmu_apr.v').read_text()
        source=change(source,'    uj11_mmu_apr dut(.*);', '''    reg lookup_request=0;
    reg [6:0] lookup_address=0;
    wire lookup_grant;
    integer lookup_checks=0;
    uj11_mmu_apr_shared dut(.*);
    always @(posedge clk)begin
        if((reset || request || busy) && lookup_grant)$fatal(1,"lookup stole CSR/reset port");
    end''')
        source=change(source,'            cycles=0;', '            lookup_request=1;lookup_address={~e,p};\n            cycles=0;')
        source=change(source,'            checks=checks+1;', '''            // Held lookup loses to CSR until release, then reads the same
            // nonzero pair just programmed, with exactly one EBR edge.
            @(negedge clk);lookup_address={e,1'b0};
            if(!lookup_grant)$fatal(1,"lookup not granted after release");
            @(posedge clk);#1;
            if(read_data!==pars[e])$fatal(1,"shared PAR stale/wrong entry");
            @(negedge clk);lookup_address={e,1'b1};
            @(posedge clk);#1;
            if(read_data!==pdrs[e])$fatal(1,"shared PDR stale/wrong entry");
            @(negedge clk);lookup_request=0;
            lookup_checks=lookup_checks+2;
            checks=checks+1;''')
        source=change(source,'        $finish;', '        $display("PASS shared APR: %0d coherent lookup reads, CSR collision/release",lookup_checks);\n        $finish;')
        path=out/'tb_mmu_apr.v';path.write_text(source)
        dependencies+=['tb/tb_mmu_apr.v']
        sources=[str(path.relative_to(ROOT)), 'build/cp39-csr/uj11_mmu_apr_shared.v','rtl/uj11_mmu_apr_ram.v']
    elif args.suite=='bus':
        tb='tb_board_bus'
        source=(ROOT/'tb/tb_board_bus.v').read_text()
        source=change(source, '#(CHECK_SELECTORS!=0 ? 20000000 : 1000000);', '#5000000;')
        source=change(source,'    uj11_board_bus #', '''    wire apr_request,apr_pdr,apr_ack;
    wire [5:0] apr_entry;
    wire [15:0] apr_data;
    uj11_mmu_apr_shared apr(.clk(clk),.reset(reset),.request(apr_request),
        .entry(apr_entry),.pdr_select(apr_pdr),.writing(writing),.mark_written(1'b0),
        .byte_enable(lanes),.write_data(data),.read_data(apr_data),.ready(apr_ack),.busy(),
        .lookup_request(1'b0),.lookup_address(7'b0),.lookup_grant());
    uj11_board_bus #''')
        source=change(source,'.rdata(value),', '.apr_request(apr_request),.apr_pdr(apr_pdr),.apr_entry(apr_entry),.apr_data(apr_data),.apr_ack(apr_ack),.rdata(value),')
        source=change(source, '        beat(1,3,16\'o177440,16\'o2021,0);', '''        beat(1,3,16'o172340,16'h5678,0);
        beat(1,3,16'o177440,16'o2021,0);''')
        source=change(source,"        beat(0,3,16'o160476,0,1);", '''        beat(1,1,16'o172340,16'h00ab,0);
        if(memory.memory[16'o172340]!=8'hab)$fatal(1,"DMA intercepted by APR CSR");
        beat(0,3,16'o160476,0,1);''')
        source=change(source,'        $display("PASS board bus:', '''        beat(0,3,16'o172340,0,0);if(answer!=16'h5678)$fatal(1,"DMA modified APR");
        // Independent physical address map on both lanes. Include every
        // service/direction/fetch combination: DMA must mask the CSR decode.
        @(negedge clk);run_clock=0;request=0;
        for(scenario=0;scenario<32;scenario=scenario+1)begin
            dut.rk_service_active=scenario[0];dut.rk_service_movb=scenario[1];
            dut.rk_write_command=scenario[2];writing=scenario[3];fetch=scenario[4];
            for(probe=0;probe<65536;probe=probe+1)begin
                address=probe[15:0];#1;
                if(dut.apr_selected !==
                   (((probe>=16'o172200 && probe<=16'o172377) ||
                     (probe>=16'o177600 && probe<=16'o177677)) &&
                    !(scenario[0] && scenario[1] &&
                      ((!scenario[2] && scenario[3]) || (scenario[2] && !scenario[3] && !scenario[4])))))
                    $fatal(1,"APR decode/DMA state%0d addr%o",scenario,probe);
                selector_checks=selector_checks+1;
            end
        end
        $display("PASS APR bus: %0d decode/DMA combinations; physical FRAM alias preserves CSR",selector_checks);
        $display("PASS board bus:''')
        path=out/'tb_board_bus.v';path.write_text(source)
        sources=[str(path.relative_to(ROOT))]+board+['build/cp39-csr/uj11_mmu_apr_shared.v',
            'rtl/uj11_mmu_apr_ram.v','rtl/uj11_mmu_apr_decode.v','reference/lsi11/spi_fram_model.v']
        dependencies+=['tb/tb_board_bus.v']
    else:
        tb='tb_mmu_apr_csr_cpu'
        sources=['tb/tb_mmu_apr_csr_cpu.v']+core+board+['reference/lsi11/spi_fram_model.v']
        sources+=['build/cp39-csr/uj11_m0_ebr.v' if args.vendor else 'rtl/uj11_rom.v']
    if args.vendor:
        sources += ['build/vendor/'+name+'.v' for name in ('DP8KC','GSR','PUR')]
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(sources+dependencies))}
    tag='cp39-'+args.suite+('-vendor' if args.vendor else '-portable')
    with (ROOT/f'build/{tag}-build.log').open('w') as log:
        subprocess.run(['iverilog','-g2012','-Wall','-s',tb,'-o','build/'+tag]+
                       (['-DUJ11_VENDOR_ROM'] if args.vendor else [])+sources,
                       cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (ROOT/f'build/{tag}.log').open('w') as log:
        subprocess.run(['vvp','build/'+tag],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in hashes}
    lines=[s for s in (ROOT/f'build/{tag}.log').read_text().splitlines() if s.startswith('PASS')]
    assert len(lines)==2, lines
    (ROOT/f'build/{tag}.json').write_text(json.dumps(dict(suite=args.suite,vendor=args.vendor,
        inputs_sha256=hashes,pass_lines=lines),indent=2)+'\n')
    print('\n'.join(lines),flush=True)


if __name__=='__main__':main()
