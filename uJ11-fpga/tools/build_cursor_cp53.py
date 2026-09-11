#!/usr/bin/env python3
"""CP53: isolate increment/equality LUT mappings of the measured CP52 cursor."""
import hashlib
import json
import tarfile
from board_common import ROOT, CORE, BOARD
from build_fram_cp52 import replace_once

VARIANTS=('increment','compare','both')
OUT=ROOT/'build/cp53-cursor'


def build():
    OUT.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((ROOT/'synth/reports/cp52b/inputs.json').read_text())
    frozen={}
    with tarfile.open(ROOT/'synth/reports/cp52b/source.tgz') as archive:
        for module in ('uj11_board_fram.v','uj11_board_bus.v'):
            name='build/cp52-fram/'+module
            data=archive.extractfile(name).read()
            assert hashlib.sha256(data).hexdigest()==manifest['files'][name],name
            frozen[module]=data.decode()
    baseline=OUT/'baseline';baseline.mkdir(exist_ok=True)
    for name,text in frozen.items():(baseline/name).write_text(text)
    outputs={}
    for variant in VARIANTS:
        text=frozen['uj11_board_fram.v']
        if variant in ('increment','both'):
            # No changed representation or extra state. Each bit is the same
            # binary +1, expressed as XOR with the lower-bit AND prefix.
            declarations='''
    wire [14:0] advanced_word;
    assign advanced_word[0]=!address[1];
    genvar increment_bit;
    generate for(increment_bit=1;increment_bit<15;increment_bit=increment_bit+1)begin:increment_bits
        assign advanced_word[increment_bit]=address[increment_bit+1] ^ (&address[increment_bit:1]);
    end endgenerate
'''
            text=replace_once(text,'    reg [14:0] next_word;', '    reg [14:0] next_word;'+declarations)
            text=replace_once(text,"next_word<=address[15:1]+1'b1;",'next_word<=advanced_word;')
        if variant in ('compare','both'):
            # syn_keep preserves each small equality's net boundary. Lattice
            # Synplify Attribute Reference (Sep 2024), syn_keep, pp.125-128.
            text=replace_once(text,'    wire resume_read=retain && !spi_cs_n && address[15:1]==next_word;', '''
    wire [4:0] cursor_equal /* synthesis syn_keep=1 */;
    genvar equal_group;
    generate for(equal_group=0;equal_group<5;equal_group=equal_group+1)begin:equal_groups
        assign cursor_equal[equal_group]=address[equal_group*3+3:equal_group*3+1]==next_word[equal_group*3+2:equal_group*3];
    end endgenerate
    wire resume_read=retain && !spi_cs_n && (&cursor_equal);''')
        folder=OUT/variant;folder.mkdir(exist_ok=True)
        path=folder/'uj11_board_fram.v';path.write_text('// CP53 '+variant+' cursor mapping candidate.\n'+text)
        outputs[str(path.relative_to(ROOT))]=hashlib.sha256(path.read_bytes()).hexdigest()
    inputs=['tools/build_cursor_cp53.py','tools/build_fram_cp52.py','tools/board_common.py',
            'synth/reports/cp52b/inputs.json','synth/reports/cp52b/source.tgz']
    record=dict(reference='cp52b',variants=list(VARIANTS),mmu=False,extra_state_bits=0,
        inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},outputs=outputs)
    (OUT/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def adapt(variant):
    assert variant in VARIANTS
    build()
    return CORE.copy(),[str((OUT/('baseline' if p.endswith('uj11_board_bus.v') else variant)/p.rsplit('/',1)[-1]).relative_to(ROOT))
        if p in ('boards/hc1200/uj11_board_bus.v','boards/hc1200/uj11_board_fram.v') else p for p in BOARD]


if __name__=='__main__':print(json.dumps(build(),indent=2))
