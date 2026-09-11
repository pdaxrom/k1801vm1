#!/usr/bin/env python3
"""Build a native-only demand sequential READ candidate; keep production frozen."""
import hashlib
import json
import subprocess
from board_common import ROOT, CORE, BOARD

OUT = ROOT/'build/cp52-fram'


def replace_once(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new)


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    inputs = ['boards/hc1200/uj11_board_bus.v', 'boards/hc1200/uj11_board_fram.v',
              'tools/build_fram_cp52.py', 'tools/board_common.py']
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}
    native = {}
    for p in inputs[:2]:
        native[p] = subprocess.check_output(
            ['iverilog', '-g2012', '-E', '-o', '-', p], cwd=ROOT).decode()
    fram = native[inputs[1]]
    fram = replace_once(fram, 'input wire clk,rst,req,write,byte_access,bank,',
        'input wire clk,rst,req,write,byte_access,bank,\n'
        '    input wire keep_read,close_read,')
    fram = replace_once(fram, 'reg active,seen;', '''reg active,seen;
    // Only demand reads: no cached data or clocks before a real request.
    // CS itself records whether the FRAM read cursor is still valid.
    reg [14:0] next_word;
    wire retain=keep_read && !close_read && !write && !byte_access &&
        !bank && !address[0] && !(&address[15:1]);
    wire resume_read=retain && !spi_cs_n && address[15:1]==next_word;''')
    fram = replace_once(fram, 'state<=IDLE;active<=0;seen<=0;',
        'next_word<=0;state<=IDLE;active<=0;seen<=0;')
    fram = replace_once(fram, 'spi_cs_n<=1;spi_sck<=0;\n                    if(req && !seen)begin\n                        seen<=1;',
        '''if(close_read)spi_cs_n<=1;
                    spi_sck<=0;
                    if(req && !seen)begin
                        seen<=1;
                        // A miss raises CS for a complete system clock before
                        // CMD/WREN starts. At 29.56 MHz this exceeds tSHSL.
                        spi_cs_n<=!resume_read;''')
    fram = replace_once(fram, 'else state<=write?WREN:CMD;',
        'else state<=write?WREN:(resume_read?DATA_LO:CMD);')
    fram = replace_once(fram, 'DONE:begin spi_cs_n<=1;ready<=1;state<=IDLE;end',
        '''DONE:begin
                    spi_cs_n<=!retain;ready<=1;state<=IDLE;
                    next_word<=address[15:1]+1'b1;
                end''')
    bus = native[inputs[0]]
    bus = replace_once(bus, '.req(fram_request), .write(write),', '''.req(fram_request), .write(write),
        // Qualify at the real board decode. ROM, CSR and private RK DMA
        // operands cannot retain a READ, including aliases in the I/O page.
        // This also helps consecutive data reads without decoding the IR.
        .keep_read(!io_page && !boot_selected && !service_dma_selected),
        .close_read(request && !fram_selected),''')
    outputs = {}
    for name, text in [('uj11_board_fram.v', fram), ('uj11_board_bus.v', bus)]:
        path = OUT/name
        path.write_text('// CP52 experimental native sequential READ; generated, not production.\n'+text)
        outputs[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = dict(mmu=False, speculative=False, bank_zero_only=True,
                    inputs=hashes, outputs=outputs)
    (OUT/'inputs.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return manifest


def adapt(core=CORE, board=BOARD):
    build()
    return core.copy(), [str((OUT/p.rsplit('/', 1)[-1]).relative_to(ROOT))
        if p in ('boards/hc1200/uj11_board_bus.v', 'boards/hc1200/uj11_board_fram.v')
        else p for p in board]


if __name__ == '__main__':
    print(json.dumps(build(), indent=2))
