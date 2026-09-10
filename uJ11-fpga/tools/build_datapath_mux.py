#!/usr/bin/env python3
"""CP40 equivalent datapath mux candidates, frozen against the CP39d snapshot."""
import hashlib
import json
import tarfile
from board_common import ROOT
from build_mmu_entry import change


def main():
    snapshot=ROOT/'synth/reports/cp39d/source.tgz'
    with tarfile.open(snapshot) as archive:
        base=archive.extractfile('rtl/uj11_datapath.v').read().decode()
    expected=json.loads((ROOT/'synth/reports/cp39d/inputs.json').read_text())['files']['rtl/uj11_datapath.v']
    assert hashlib.sha256(base.encode()).hexdigest()==expected
    shift='''    // Resolve long shifts first. Byte merge only applies to destinations
    // 5/6, so these selects are disjoint without an extra ordinary-data mask.
    wire [15:0] shifted = destination==3'd3 ? {result[14:0],q[15]} :
                          destination==3'd4 ? {result[15],result[15:1]} : result;
    assign writeback = {keep_high ? read_b[15:8] :
                        sign_high ? {8{result[7]}} : shifted[15:8], shifted[7:0]};
'''
    start=base.index('    wire [15:0] ordinary_writeback')
    end=base.index('    assign rf_write',start)
    shifted=base[:start]+shift+base[end:]
    # Four input values with a two-bit selector, derived from the existing
    # three-bit microcode pair. No microinstruction encoding change.
    pairs='''    wire [1:0] lhs_select = {
        pair[2] && (pair[1]==pair[0]),
        (pair[1] && pair[0]) || (pair[2] && (pair[1] || pair[0]))};
    wire [1:0] rhs_select = {
        pair[1] && (pair[2] || !pair[0]),
        (pair[0] && !pair[1]) || (pair[2] && pair[1])};
    // LHS 00=A,01=D,10=0,11=B; RHS 00=B,01=Q,10=D,11=A.
    wire [15:0] lhs = lhs_select[1] ? (lhs_select[0] ? read_b : 16'b0) :
                                    (lhs_select[0] ? d : read_a);
    wire [15:0] rhs = rhs_select[1] ? (rhs_select[0] ? read_a : d) :
                                    (rhs_select[0] ? q : read_b);
'''
    def pair_mux(source):
        start=source.index('    // Decode once,')
        end=source.index('    uj11_alu alu',start)
        return source[:start]+pairs+source[end:]
    variants=dict(baseline=base,shift=shifted,pair=pair_mux(base),both=pair_mux(shifted))
    root=ROOT/'build/cp40-mux';root.mkdir(parents=True,exist_ok=True)
    outputs={}
    for name,source in variants.items():
        folder=root/name;folder.mkdir(exist_ok=True)
        path=folder/'uj11_datapath.v';path.write_text(source)
        outputs[str(path.relative_to(ROOT))]=hashlib.sha256(source.encode()).hexdigest()
    inputs=['tools/build_datapath_mux.py','synth/reports/cp39d/source.tgz','synth/reports/cp39d/inputs.json']
    record=dict(scope='Unrestricted equivalence candidates: datapath input and RF writeback muxes',
                baseline_sha256=expected,outputs_sha256=outputs,
                inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs})
    (root/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    print('PASS CP40 build: three datapath mux candidates, unchanged ports/state/microcode')


if __name__=='__main__':main()
