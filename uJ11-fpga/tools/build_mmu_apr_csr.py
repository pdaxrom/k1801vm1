#!/usr/bin/env python3
"""CP39 build-only CPU APR CSR / scheduled lookup sharing one EBR.

Preserves the CP33 CSR controller and CP37 microcode. No translation/MMRs.
Production sources remain unchanged until this complete-board gate fits.
"""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from build_mmu_entry import ROOT, change

OUT = ROOT/'build/cp39-csr'
EXTRA = ['rtl/experimental/uj11_mmu_entry.v', 'rtl/uj11_mmu_apr_ram.v',
         'rtl/uj11_mmu_apr_decode.v', 'build/cp39-csr/uj11_mmu_apr_shared.v']


def adapt(core, board):
    names = {'uj11_core.v', 'uj11_engine.v', 'uj11_microseq.v', 'uj11_decode_table.v'}
    return ([f'build/cp39-csr/{Path(p).name}' if Path(p).name in names else p for p in core]+EXTRA,
            [f'build/cp39-csr/{Path(p).name}' if Path(p).name in {'uj11_board.v', 'uj11_board_bus.v'} else p for p in board])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--read-mux', choices=('masked', 'outer', 'inner'), default='outer')
    args = parser.parse_args()
    subprocess.run([sys.executable, str(ROOT/'tools/build_mmu_apr_lookup.py'),
                    '--page', '--scheduled', '--masked-d'], cwd=ROOT, check=True)
    OUT.mkdir(parents=True, exist_ok=True)
    ports = '''    output wire apr_request,
    output wire [6:0] apr_address,
    input wire apr_grant,
    input wire [15:0] apr_data,
'''
    for filename in ('uj11_core.v', 'uj11_engine.v', 'uj11_microseq.v', 'uj11_decode_table.v',
                     'uj11_m0_ebr.v', 'lookup.mem', 'lookup.stats.json'):
        source = (ROOT/'build/cp37-lookup'/filename).read_text()
        if filename in ('uj11_core.v', 'uj11_engine.v'):
            source = change(source, '    input wire irq_valid,', ports+'    input wire irq_valid,')
        if filename == 'uj11_core.v':
            source = change(source, '.irq_valid(irq_valid),',
                            '.apr_request(apr_request),.apr_address(apr_address),.apr_grant(apr_grant),.apr_data(apr_data),.irq_valid(irq_valid),')
        if filename == 'uj11_engine.v':
            source = change(source, '    wire apr_request =', '    assign apr_request =')
            start = source.index('    wire [15:0] apr_data;')
            end = source.index('    always @(posedge clk) begin', start)
            source = source[:start]+"    assign apr_address={3'b000,mmu_page,uword[6]};\n"+source[end:]
            source = change(source, '.hold_routine(mmu_hold)', '.hold_routine(mmu_hold || (apr_request && !apr_grant))')
            source = change(source, 'build/cp37-lookup/lookup.mem', 'build/cp39-csr/lookup.mem')
        (OUT/filename).write_text(source)
    shared = (ROOT/'rtl/uj11_mmu_apr.v').read_text()
    shared = change(shared, 'module uj11_mmu_apr(', 'module uj11_mmu_apr_shared(')
    shared = change(shared, '    input wire [5:0] entry,', '''    // CSR owns the port through its release edge. A lookup waits via grant;
    // accepted lookup output is available on the next microinstruction.
    input wire lookup_request, input wire [6:0] lookup_address,
    output wire lookup_grant,
    input wire [5:0] entry,''')
    shared = change(shared, '    uj11_mmu_apr_ram ram(.clk(clk),.enable(memory_enable),.address(address),\n        .write_enable(lanes),', '''    assign lookup_grant=lookup_request && !request && !busy && !reset;
    uj11_mmu_apr_ram ram(.clk(clk),.enable(memory_enable || lookup_grant),
        .address(lookup_grant ? lookup_address : address),
        .write_enable(lookup_grant ? 2'b00 : lanes),''')
    (OUT/'uj11_mmu_apr_shared.v').write_text(shared)
    board = (ROOT/'boards/hc1200/uj11_board.v').read_text()
    board = change(board, '// No prefetch in this initial full-peripheral resource baseline; no MMU.',
                   '// CP39 experimental APR CSR + lookup. No address translation or MMRs.')
    board = change(board, '    wire request, writing, byte_access, acknowledge, error, raw_ack;', '''    wire request, writing, byte_access, acknowledge, error, raw_ack;
    wire apr_request, apr_grant, apr_csr_request, apr_csr_ack;
    wire [6:0] apr_address;
    wire [5:0] apr_entry;
    wire apr_pdr;
    wire [15:0] apr_data;''')
    board = change(board, '    wire rom_enable;', '''    uj11_mmu_apr_shared apr(.clk(clk),.reset(reset),.request(apr_csr_request),
        .entry(apr_entry),.pdr_select(apr_pdr),.writing(writing),.mark_written(1'b0),
        .byte_enable(lanes),.write_data(lane_data),.read_data(apr_data),.ready(apr_csr_ack),.busy(),
        .lookup_request(apr_request),.lookup_address(apr_address),.lookup_grant(apr_grant));
    wire rom_enable;''')
    board = change(board, '.clk(clk),.reset(reset),.irq_valid(irq_valid)',
                   ".mmu_enabled(1'b1),.mmu_hold(1'b0),.apr_request(apr_request),.apr_address(apr_address),.apr_grant(apr_grant),.apr_data(apr_data),.clk(clk),.reset(reset),.irq_valid(irq_valid)")
    board = change(board, '.rdata(lane_rdata),', '''.apr_request(apr_csr_request),.apr_entry(apr_entry),.apr_pdr(apr_pdr),
        .apr_data(apr_data),.apr_ack(apr_csr_ack),.rdata(lane_rdata),''')
    (OUT/'uj11_board.v').write_text(board)
    bus = (ROOT/'boards/hc1200/uj11_board_bus.v').read_text()
    bus = change(bus, '\toutput wire [15:0] rdata,', '''    output wire apr_request, apr_pdr,
    output wire [5:0] apr_entry,
    input wire [15:0] apr_data, input wire apr_ack,
\toutput wire [15:0] rdata,''')
    bus = change(bus, '\twire uart_selected =', '''    wire apr_decoded;
    // This gate retains the 16-bit unmapped bus. Only its CPU I/O page is
    // canonicalized; a physical RK service operand never selects a CPU CSR.
    uj11_mmu_apr_decode apr_decode(.physical_address({6'b111111,address}),
        .selected(apr_decoded),.entry(apr_entry),.pdr_select(apr_pdr));
    wire apr_selected=cpu_io_page && apr_decoded;
    assign apr_request=request && apr_selected;
\twire uart_selected =''')
    bus = change(bus, '\twire [15:0] small_rdata =', '\twire [15:0] small_rdata = (apr_data & {16{apr_selected}}) |')
    bus = change(bus, '\tassign acknowledge =', '\tassign acknowledge = apr_ack ||')
    if args.read_mux != 'masked':
        bus = change(bus, '(apr_data & {16{apr_selected}}) |', '')
        if args.read_mux == 'outer':
            bus = change(bus, 'assign rdata = firmware_selected ?', 'assign rdata = apr_selected ? apr_data : firmware_selected ?')
        else:
            bus = change(bus, 'fram_selected ? fram_rdata : small_rdata;',
                         'fram_selected ? fram_rdata : apr_selected ? apr_data : small_rdata;')
    (OUT/'uj11_board_bus.v').write_text(bus)
    inputs = ['tools/build_mmu_apr_csr.py', 'rtl/uj11_mmu_apr.v', 'rtl/uj11_mmu_apr_decode.v',
              'boards/hc1200/uj11_board.v', 'boards/hc1200/uj11_board_bus.v', 'build/cp37-lookup/inputs.json']
    inputs += [str(p.relative_to(ROOT)) for p in sorted((ROOT/'build/cp37-lookup').glob('*.v'))]
    record = dict(scope='CPU APR CSRs, CSR-priority shared port, kernel unified lookup; no translation/MMRs',
                  microcode_words=963, read_mux=args.read_mux,
                  inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
                  outputs_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in sorted(OUT.iterdir()) if p.is_file() and p.name!='inputs.json'})
    (OUT/'inputs.json').write_text(json.dumps(record, indent=2)+'\n')
    print('PASS CP39 build: shared APR EBR, CPU CSR decode, unchanged 963 microcode words')


if __name__ == '__main__':
    main()
