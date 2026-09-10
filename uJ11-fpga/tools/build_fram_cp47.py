#!/usr/bin/env python3
"""CP47 isolated SPI FRAM candidates on the retained CP45 relocation board."""
import hashlib
import json
from pathlib import Path
from board_common import ROOT
from build_mmu_entry import change
from build_bus_cp45 import adapt as prior_adapt

VARIANTS = ('baseline','byte-mux','shared-rx','combined')
OUT = ROOT/'build/cp47-fram'


def adapt(core, board, variant):
    core, board = prior_adapt(core, board, 'narrow-rom')
    return core, [f'build/cp47-fram/{variant}/uj11_board_fram.v'
                  if Path(p).name=='uj11_board_fram.v' else p for p in board]


def main():
    path = 'boards/hc1200/uj11_board_fram.v'
    original = (ROOT/path).read_text()
    frozen = json.loads((ROOT/'synth/reports/cp45k/inputs.json').read_text())['files']
    assert hashlib.sha256(original.encode()).hexdigest()==frozen[path]
    outputs = {}
    for variant in VARIANTS:
        text = original
        if variant in ('byte-mux','combined'):
            # These selectors are used only for HIGH/LOW/DATA_LO/DATA_HI.
            # In those states bit 1 selects the byte and XNOR selects the word.
            text = change(text, '    always @*case(state)', '''    wire [15:0] payload_word=(state[0] ~^ state[1]) ? wdata : address;
    wire [7:0] payload_byte=state[1] ? payload_word[7:0] : payload_word[15:8];
    always @*case(state)''')
            text = change(text, '''        HIGH:next_byte=address[15:8];
        LOW:next_byte=address[7:0];
        DATA_LO:next_byte=wdata[7:0];
        DATA_HI:next_byte=wdata[15:8];''', '        HIGH,LOW,DATA_LO,DATA_HI:next_byte=payload_byte;')
        if variant in ('shared-rx','combined'):
            text = change(text, '    reg [7:0] rx;', '''    // rdata is valid at ready and stable while idle. During a transfer,
    // its high byte is the serial receive shift register (no separate rx FFs).
    wire [7:0] rx=rdata[15:8];''')
            text = change(text, 'tx<=0;rx<=0;bit_count<=0;', 'tx<=0;bit_count<=0;')
            text = change(text, 'if(!spi_sck)rx<={rx[6:0],spi_miso};',
                          'if(!spi_sck)rdata[15:8]<={rx[6:0],spi_miso};')
            text = change(text, '                        if(state==DATA_HI)rdata[15:8]<=rx;\n', '')
        folder = OUT/variant; folder.mkdir(parents=True,exist_ok=True)
        dest = folder/'uj11_board_fram.v'; dest.write_text(text)
        outputs[str(dest.relative_to(ROOT))] = hashlib.sha256(text.encode()).hexdigest()
    inputs = [path,'tools/build_fram_cp47.py','tools/build_bus_cp45.py',
              'tools/build_mmu_entry.py','synth/reports/cp45k/inputs.json']
    (OUT/'inputs.json').write_text(json.dumps(dict(variants=VARIANTS,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        outputs_sha256=outputs),indent=2)+'\n')
    print('PASS CP47 candidates:',', '.join(VARIANTS))


if __name__=='__main__': main()
