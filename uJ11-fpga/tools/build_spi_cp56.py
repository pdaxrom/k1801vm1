#!/usr/bin/env python3
"""CP56: one SPI bit per CPU clock, using the frozen CP54b board."""
import hashlib
import json
import re
import tarfile
from board_common import ROOT, CORE, BOARD
from build_fram_cp52 import replace_once

OUT = ROOT/'build/cp56-spi'


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ROOT/'synth/reports/cp54b/inputs.json').read_text())
    names = {'uj11_board_bus.v': 'build/cp54-ack/dma-ack/uj11_board_bus.v',
             'uj11_board_fram.v': 'build/cp54-ack/baseline/uj11_board_fram.v'}
    frozen = {}
    with tarfile.open(ROOT/'synth/reports/cp54b/source.tgz') as archive:
        for name, path in names.items():
            data = archive.extractfile(path).read()
            assert hashlib.sha256(data).hexdigest() == manifest['files'][path], path
            frozen[name] = data.decode()
    baseline = OUT/'baseline'; baseline.mkdir(exist_ok=True)
    for name, data in frozen.items():
        (baseline/name).write_text(data)
    text = frozen['uj11_board_fram.v']
    # The explicit new profile has no integer divider. Keep the parameter in
    # the interface, and reject anything other than the board's CLK_DIV=1.
    text = replace_once(text, '    output reg spi_cs_n,spi_sck,spi_mosi,',
        '    output reg spi_cs_n,spi_mosi,\n    output wire spi_sck,')
    # Preserve positional interface ordering used by the existing fixtures.
    text = replace_once(text, '    output reg spi_cs_n,spi_mosi,\n    output wire spi_sck,',
        '    output reg spi_cs_n,\n    output wire spi_sck,\n    output reg spi_mosi,')
    text = replace_once(text, '    localparam integer DIV_WIDTH=CLK_DIV>1?$clog2(CLK_DIV):1;\n    localparam integer DIV_LAST=CLK_DIV-1;\n', '')
    text = replace_once(text, '    reg [DIV_WIDTH-1:0] divider;\n', '')
    text = replace_once(text, '    reg [7:0] rx;', '    reg [6:0] rx;')
    text = replace_once(text, '    assign busy=state!=IDLE;', '''    assign busy=state!=IDLE;
    // ODDRXE samples D1 on posedge, emits it on negedge, and drives D0=0
    // on the following posedge. MISO is captured before that falling SCK
    // reaches the FRAM. MOSI changes on posedge, half a cycle before SCK rises.
    // At bit_count=7 the final pulse is already on the wire; stop the NEXT one.
    wire serial_state=state!=IDLE && state!=GAP && state!=DONE;
    wire clock_pulse=!rst && serial_state && (!active || bit_count!=7);
    // Reset is synchronous like CS/control; masking D1 finishes the current
    // high half-period, rather than asynchronously truncating an SCK pulse.
    ODDRXE clock_output(.SCLK(clk),.RST(1'b0),.D0(1'b0),.D1(clock_pulse),.Q(spi_sck));
    // synthesis translate_off
    initial if(CLK_DIV!=1)$fatal(1,"CP56 requires CLK_DIV=1");
    // synthesis translate_on''')
    text = text.replace('spi_sck<=0;', '').replace('divider<=0;', '')
    start = text.index('            if(active)begin')
    end = text.index('            end else case(state)', start)
    text = text[:start] + '''            if(active)begin
                rx<={rx[5:0],spi_miso};
                if(bit_count==7)begin
                    active<=0;
                    if(state==DATA_LO)begin
                        rdata[7:0]<={rx[6:0],spi_miso};
                        if(byte_access)rdata[15:8]<=0;
                    end
                    if(state==DATA_HI)rdata[15:8]<={rx[6:0],spi_miso};
                    state<=(state==DATA_LO && byte_access)?DONE:state+1'b1;
                end else begin
                    bit_count<=bit_count+1'b1;
                    spi_mosi<=tx[6];tx<={tx[5:0],1'b0};
                end
''' + text[end:]
    text = text[text.index('`timescale'):]
    text = re.sub(r'\n(?:[ \t]*\n){2,}', '\n\n', text)
    text = '\n'.join(line.rstrip() for line in text.splitlines())+'\n'
    text = '// CP56 experimental 29.56 MHz SPI; CPU and CP54b cursor unchanged.\n' + text
    target = OUT/'fast'; target.mkdir(exist_ok=True)
    (target/'uj11_board_fram.v').write_text(text)
    inputs = ['tools/build_spi_cp56.py', 'tools/build_fram_cp52.py', 'tools/board_common.py',
              'synth/reports/cp54b/inputs.json', 'synth/reports/cp54b/source.tgz']
    outputs = [str(p.relative_to(ROOT)) for p in [baseline/n for n in names] + [target/'uj11_board_fram.v']]
    record = dict(reference='cp54b', mmu=False, cpu_mhz=29.56, spi_mhz=29.56,
        sampling='MISO on CPU posedge, before forwarded SCK falls; MOSI on CPU posedge; SCK rises on CPU negedge.',
        inputs={p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        outputs={p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in outputs})
    (OUT/'inputs.json').write_text(json.dumps(record, indent=2)+'\n')
    return record


def adapt():
    build()
    return CORE.copy(), [str((OUT/('baseline' if p.endswith('uj11_board_bus.v') else 'fast')/p.rsplit('/',1)[-1]).relative_to(ROOT))
        if p in ('boards/hc1200/uj11_board_bus.v', 'boards/hc1200/uj11_board_fram.v') else p for p in BOARD]


if __name__ == '__main__':
    print(json.dumps(build(), indent=2))
