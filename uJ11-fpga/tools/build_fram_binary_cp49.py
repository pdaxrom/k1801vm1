#!/usr/bin/env python3
"""CP49: original FSM encoding and binary byte-skip logic on CP47c FRAM."""
import hashlib
import json
from pathlib import Path
from board_common import ROOT
from build_mmu_entry import change
from build_fram_cp47 import adapt as prior_adapt

VARIANTS=('baseline','original','split-low','equations')
OUT=ROOT/'build/cp49-binary'
STATES=('IDLE','WREN','GAP','CMD','BANK','HIGH','LOW','DATA_LO','DATA_HI','DONE')


def adapt(core,board,variant):
    core,board=prior_adapt(core,board,'shared-rx')
    return core,[f'build/cp49-binary/{variant}/uj11_board_fram.v'
                 if Path(p).name=='uj11_board_fram.v' else p for p in board]


def main():
    source='build/cp47-fram/shared-rx/uj11_board_fram.v'
    table='build/cp48-state/successors/uj11_board_fram.v'
    for path,gate in ((source,'cp47c'),(table,'cp48b')):
        frozen=json.loads((ROOT/f'synth/reports/{gate}/inputs.json').read_text())['files']
        assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==frozen[path]
    outputs={}
    for variant in VARIANTS:
        text=(ROOT/(table if variant=='original' else source)).read_text()
        if variant=='original':
            # Synplify Pro for Lattice Attribute Reference, September 2024,
            # syn_encoding, pp. 67-69: original retains the specified codes.
            text=change(text,'reg [3:0] state;', 'reg [3:0] state /* synthesis syn_encoding="original" */;')
        if variant in ('split-low','equations'):
            if variant=='split-low':
                wires="""    wire [3:0] successor=state+4'd1;
    // DATA_LO=7 normally advances to DATA_HI=8; a byte ends at DONE=9.
    // Only bit zero differs, so the byte override need not mux all four bits.
    wire [3:0] serial_next={successor[3:1],successor[0] | (state==DATA_LO && byte_access)};
"""
            else:
                wires="""    // Binary increment with a byte-only DATA_LO -> DONE LSB override.
    // All sixteen input codes match the original increment/skip expression.
    wire [3:0] serial_next={state[3] ^ (&state[2:0]),
                            state[2] ^ (&state[1:0]),
                            state[1] ^ state[0],
                            ~state[0] | (state==DATA_LO && byte_access)};
"""
            text=change(text,'    assign busy=state!=IDLE;',wires+'    assign busy=state!=IDLE;')
            text=change(text,"state<=(state==DATA_LO && byte_access)?DONE:state+1'b1;",'state<=serial_next;')
        folder=OUT/variant;folder.mkdir(parents=True,exist_ok=True)
        dest=folder/'uj11_board_fram.v';dest.write_text(text)
        outputs[str(dest.relative_to(ROOT))]=hashlib.sha256(text.encode()).hexdigest()
    inputs=[source,table,'tools/build_fram_binary_cp49.py','tools/build_fram_cp47.py',
            'tools/build_mmu_entry.py','build/cp47-fram/inputs.json',
            'synth/reports/cp47c/inputs.json','synth/reports/cp48b/inputs.json']
    (OUT/'inputs.json').write_text(json.dumps(dict(variants=VARIANTS,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        outputs_sha256=outputs),indent=2)+'\n')
    print('PASS CP49 candidates:',', '.join(VARIANTS))


if __name__=='__main__':main()
