#!/usr/bin/env python3
"""CP42 exact FIS oracle through CP39 APR port, RAM/FRAM and vendor microstore."""
import hashlib
import json
from board_common import ROOT
from run_fis_tests import compile_test, execute


def main():
    folder = ROOT/'build/cp42-fis'
    folder.mkdir(exist_ok=True)
    paths = ['tb/tb_fis.v','build/cp39-csr/uj11_core.v','build/cp39-csr/uj11_engine.v',
             'build/cp39-csr/uj11_microseq.v','build/cp39-csr/uj11_decode_table.v','build/cp39-csr/lookup.mem',
             'build/cp39-csr/uj11_m0_ebr.v','rtl/experimental/uj11_mmu_entry.v','rtl/uj11_mmu_apr_ram.v',
             'build/cp39-csr/uj11_mmu_apr_shared.v','rtl/uj11_decode_rom.v','tools/check_d_input_cp42_fis.py','tools/run_fis_tests.py',
             'rtl/uj11_decode.v','rtl/uj11_alu.v','rtl/uj11_regfile.v','rtl/uj11_datapath.v',
             'rtl/uj11_psw.v','rtl/uj11_mem.v','rtl/uj11_rom.v','tb/uj11_ram.v',
             'rtl/uj11_stream.v','rtl/uj11_prefetch_control.v','rtl/uj11_prefetch.v',
             'rtl/uj11_fram_transport.v','reference/lsi11/spi_fram_model.v','build/fis-vectors.txt']
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    core = folder/'core.v'
    core.write_text('\n'.join((ROOT/s).read_text() for s in
                    ['build/cp39-csr/uj11_core.v','rtl/uj11_decode_rom.v','build/cp39-csr/uj11_decode_table.v']))
    engine = folder/'engine.v'
    engine.write_text((ROOT/'build/cp39-csr/uj11_engine.v').read_text()+'\n'+
                      (ROOT/'rtl/experimental/uj11_mmu_entry.v').read_text()+'\n'+
                      (ROOT/'rtl/uj11_mmu_apr_ram.v').read_text()+'\n'+
                      (ROOT/'build/cp39-csr/uj11_mmu_apr_shared.v').read_text())
    tb = (ROOT/'tb/tb_fis.v').read_text()
    assert tb.count('ROM_DECODE=0') == 1 and tb.count('dut(.clk(clk),.reset(reset),') == 1
    tb = tb.replace('ROM_DECODE=0','ROM_DECODE=1')
    tb = tb.replace('dut(.clk(clk),.reset(reset),',
                    "dut(.clk(clk),.reset(reset),.mmu_enabled(1'b1),.mmu_hold(entry_hold),.apr_request(apr_request),.apr_address(apr_address),.apr_data(apr_data),.apr_grant(apr_grant),")
    tb = tb.replace('    always #5 clk=~clk;', '''    wire apr_request,apr_grant;
    wire [6:0] apr_address;wire [15:0] apr_data;
    uj11_mmu_apr_shared apr(.clk(clk),.reset(reset),.request(1'b0),.entry(6'b0),
        .pdr_select(1'b0),.writing(1'b0),.mark_written(1'b0),.byte_enable(2'b0),.write_data(16'b0),
        .read_data(apr_data),.ready(),.busy(),.lookup_request(apr_request),
        .lookup_address(apr_address),.lookup_grant(apr_grant));
    reg [3:0] entry_clock=0;
    wire entry_hold=entry_clock[1:0]==2'b11;
    always @(negedge clk)entry_clock<=entry_clock+4'd1;
    always #5 clk=~clk;''')
    assert tb.count("if(upc!=10'h020)entered=1;") == 1
    tb = tb.replace("if(upc!=10'h020)entered=1;", "if(upc!=10'h020 && !dut.engine.mmu_active)entered=1;")
    tb = tb.replace('cycles<4000','cycles<5000').replace('cycles>=4000','cycles>=5000')
    bench = folder/'tb_fis.v'
    bench.write_text(tb)
    replacements = {'rtl/uj11_core.v': str(core),'rtl/uj11_engine.v': str(engine),
                    'rtl/uj11_microseq.v': 'build/cp39-csr/uj11_microseq.v','tb/tb_fis.v': str(bench),
                    'microcode/generated/uj11_m0_ebr.v': 'build/cp39-csr/uj11_m0_ebr.v'}
    fixtures = (ROOT/'build/fis-vectors.txt').read_text().splitlines()
    subset = folder/'vendor-vectors.txt'
    subset.write_text('\n'.join(fixtures[::37])+'\n')
    results = []
    for mode, vendor in [(-1,False),(2,False),(-1,True)]:
        tag = f'cp42-fis-{mode}-'+('vendor' if vendor else 'portable')
        simulator = 'iverilog' if vendor else 'verilator'
        command = compile_test(mode,simulator,vendor,candidate=False,tag=tag,replacements=replacements)
        vectors = str(subset.relative_to(ROOT)) if vendor else 'build/fis-vectors.txt'
        execute(command,tag,vectors)
        log = (ROOT/f'build/{tag}.log').read_text()
        count = len(fixtures[::37]) if vendor else len(fixtures)
        assert f'{count} exact state/PSW/bus/memory cases' in log and 'PASS FIS' in log
        results.append(dict(tag=tag,simulator=simulator,memory_mode=mode,vendor=vendor,cases=count,
                            pass_line=next(s for s in log.splitlines() if s.startswith('PASS'))))
        print(results[-1]['pass_line'],flush=True)
    assert hashes == {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    report = dict(scope='Existing exact FIS oracle; CP42 D-input with CP39 APR context excursion enabled with periodic holds',
                  tests=results,inputs_sha256=hashes,
                  vendor_sha256={p: hashlib.sha256((ROOT/'build/vendor'/p).read_bytes()).hexdigest()
                                 for p in ('DP8KC.v','GSR.v','PUR.v')},
                  vendor_subset_sha256=hashlib.sha256(subset.read_bytes()).hexdigest(),
                  limits=['64 KiB guest bus and RAM/FRAM profile; not MMU translation or high-memory DMA.',
                          'Vendor subset is every 37th original fixture, retaining original case IDs.'])
    (ROOT/'build/cp42-fis.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':
    main()
