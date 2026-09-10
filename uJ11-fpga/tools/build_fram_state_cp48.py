#!/usr/bin/env python3
"""CP48: explicit SPI byte successors / one-hot state on CP47c shared RX."""
import hashlib
import json
from pathlib import Path
from board_common import ROOT
from build_mmu_entry import change
from build_fram_cp47 import adapt as prior_adapt

VARIANTS = ('baseline', 'successors', 'onehot')
OUT = ROOT/'build/cp48-state'
STATES = ('IDLE','WREN','GAP','CMD','BANK','HIGH','LOW','DATA_LO','DATA_HI','DONE')


def adapt(core, board, variant):
    core, board = prior_adapt(core, board, 'shared-rx')
    return core, [f'build/cp48-state/{variant}/uj11_board_fram.v'
                  if Path(p).name=='uj11_board_fram.v' else p for p in board]


def main():
    source='build/cp47-fram/shared-rx/uj11_board_fram.v'
    original=(ROOT/source).read_text()
    frozen=json.loads((ROOT/'synth/reports/cp47c/inputs.json').read_text())['files']
    assert hashlib.sha256(original.encode()).hexdigest()==frozen[source]
    outputs={}
    for variant in VARIANTS:
        text=original
        if variant=='successors':
            text=change(text,"state<=(state==DATA_LO && byte_access)?DONE:state+1'b1;",'''// Reachable serial states have explicit successors. Invalid states
                        // recover to IDLE; behavior is proved from reset, not illegal state injection.
                        case(state)
                            WREN:state<=GAP;
                            CMD:state<=BANK;
                            BANK:state<=HIGH;
                            HIGH:state<=LOW;
                            LOW:state<=DATA_LO;
                            DATA_LO:state<=byte_access?DONE:DATA_HI;
                            DATA_HI:state<=DONE;
                            default:state<=IDLE;
                        endcase''')
        if variant=='onehot':
            old='    localparam [3:0] IDLE=0,WREN=1,GAP=2,CMD=3,BANK=4,\n                     HIGH=5,LOW=6,DATA_LO=7,DATA_HI=8,DONE=9;'
            text=change(text,old,'    localparam [9:0] '+',\n                     '.join(f'{n}=10\'b{1<<i:010b}' for i,n in enumerate(STATES))+';')
            text=change(text,'    reg [3:0] state;','    reg [9:0] state;')
            text=change(text,"state<=(state==DATA_LO && byte_access)?DONE:state+1'b1;", "state<=(state==DATA_LO && byte_access)?DONE:{state[8:0],1'b0};")
        folder=OUT/variant;folder.mkdir(parents=True,exist_ok=True)
        dest=folder/'uj11_board_fram.v';dest.write_text(text)
        outputs[str(dest.relative_to(ROOT))]=hashlib.sha256(text.encode()).hexdigest()
    inputs=[source,'tools/build_fram_state_cp48.py','tools/build_fram_cp47.py',
            'build/cp47-fram/inputs.json','synth/reports/cp47c/inputs.json']
    (OUT/'inputs.json').write_text(json.dumps(dict(variants=VARIANTS,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        outputs_sha256=outputs),indent=2)+'\n')
    print('PASS CP48 candidates:',', '.join(VARIANTS))


if __name__=='__main__':main()
