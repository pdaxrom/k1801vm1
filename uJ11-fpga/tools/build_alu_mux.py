#!/usr/bin/env python3
"""CP40 ALU result selection candidates; preserve all arithmetic/flag logic."""
import hashlib
import json
import tarfile
from board_common import ROOT
from build_mmu_entry import change


def main():
    with tarfile.open(ROOT/'synth/reports/cp39d/source.tgz') as archive:
        base=archive.extractfile('rtl/uj11_alu.v').read().decode()
    expected=json.loads((ROOT/'synth/reports/cp39d/inputs.json').read_text())['files']['rtl/uj11_alu.v']
    assert hashlib.sha256(base.encode()).hexdigest()==expected
    original='''    assign result=(sum[15:0]&{16{arithmetic}})|(logic_value&{16{logic_op}})|
                  (left_value&{16{left}})|(right_value&{16{right}});'''
    choices=dict(balanced='''    wire [1:0] result_select={left || right,arithmetic || right};
    assign result=result_select[1] ? (result_select[0] ? right_value : left_value) :
                                    (result_select[0] ? sum[15:0] : logic_value);''',
                 priority='''    assign result=arithmetic ? sum[15:0] : left ? left_value :
                  right ? right_value : logic_value;''')
    out=ROOT/'build/cp40-alu';out.mkdir(exist_ok=True)
    outputs={}
    for variant,selection in [('baseline',original)]+list(choices.items()):
        source=base if variant=='baseline' else change(change(base,original,selection),
            '    wire logic_op=!arithmetic && !left && !right;\n','')
        folder=out/variant;folder.mkdir(exist_ok=True)
        path=folder/'uj11_alu.v';path.write_text(source)
        outputs[str(path.relative_to(ROOT))]=hashlib.sha256(source.encode()).hexdigest()
    inputs=['tools/build_alu_mux.py','synth/reports/cp39d/source.tgz','synth/reports/cp39d/inputs.json']
    (out/'inputs.json').write_text(json.dumps(dict(baseline_sha256=expected,outputs_sha256=outputs,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}),indent=2)+'\n')
    print('PASS CP40 ALU build: only result selection changed')


if __name__=='__main__':main()
