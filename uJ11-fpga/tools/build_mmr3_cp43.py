#!/usr/bin/env python3
"""CP43 build-only MMR3 CSR on the measured CP42 APR board; no translation."""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from build_mmu_entry import ROOT, change
from build_mmu_apr_csr import adapt as apr_adapt

OUT = ROOT/'build/cp43-mmr3'


def adapt(core, board):
    core, board = apr_adapt(core, board)
    return core, [str(OUT.relative_to(ROOT)/Path(p).name) if Path(p).name=='uj11_board_bus.v' else p
                  for p in board]+['rtl/experimental/uj11_mmr3.v']


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--read-mux",choices=("masked","shared"),default="masked")
    args=parser.parse_args()
    subprocess.run([sys.executable,str(ROOT/'tools/build_mmu_apr_csr.py')],cwd=ROOT,check=True)
    OUT.mkdir(parents=True,exist_ok=True)
    bus=(ROOT/'build/cp39-csr/uj11_board_bus.v').read_text()
    bus=change(bus,'\twire uart_selected =', '''    // Canonical MMR3 at 17772516 (unmapped VA 172516). RK physical
    // service operands must reach FRAM even at this numeric address.
    wire mmr3_selected=cpu_io_page && word_address[12:0]==13'o12516;
    wire [5:0] mmr3_value;
    wire mmr3_ready;
    uj11_mmr3 mmr3(.clk(clk),.reset(rst || peripheral_reset),
        .request(request && mmr3_selected),.writing(write),
        .low_byte_enable(byte_select[0]),.write_data(wdata[5:0]),
        .value(mmr3_value),.ready(mmr3_ready));
\twire uart_selected =''')
    bus=change(bus,'\twire [15:0] small_rdata =',
               "\twire [15:0] small_rdata = {10'b0,mmr3_value & {6{mmr3_selected}}} |")
    bus=change(bus,'\tassign acknowledge =','\tassign acknowledge = mmr3_ready ||')
    if args.read_mux=='shared':
        bus=change(bus,"{10'b0,mmr3_value & {6{mmr3_selected}}} |",'')
        bus=change(bus,"(16'o000031 & {16{maint_selected}})",
                   "({10'b0,(mmr3_selected ? mmr3_value : 6'o31)} & {16{maint_selected || mmr3_selected}})")
    (OUT/'uj11_board_bus.v').write_text(bus)
    inputs=['tools/build_mmr3_cp43.py','rtl/experimental/uj11_mmr3.v',
            'tools/build_mmu_apr_csr.py','build/cp39-csr/inputs.json','build/cp39-csr/uj11_board_bus.v']
    record=dict(scope='MMR3 six-bit CSR/reset only; APR lookup remains kernel unified; no CPU translation',
                microcode_words=963, read_mux=args.read_mux,
                inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
                outputs_sha256={'build/cp43-mmr3/uj11_board_bus.v':hashlib.sha256(bus.encode()).hexdigest()})
    (OUT/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    print('PASS CP43 build: MMR3 CSR; unchanged CPU/microcode/APR')


if __name__=='__main__':main()
