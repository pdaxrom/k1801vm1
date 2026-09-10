#!/usr/bin/env python3
"""CP42 D-input candidates, derived from immutable CP40 full-board inputs."""
import hashlib
import json
import tarfile
from board_common import ROOT


def d_span(source):
    start=source.index('    always @* begin\n        case (uword[12:10])')
    end=source.index('    uj11_datapath dp(',start)
    return start,end


def main():
    out=ROOT/'build/cp42-input';out.mkdir(parents=True,exist_ok=True)
    snapshot=ROOT/'synth/reports/cp40i/source.tgz'
    expected=json.loads((ROOT/'synth/reports/cp40i/inputs.json').read_text())['files']
    with tarfile.open(snapshot) as archive:
        bases={profile:archive.extractfile(path).read().decode() for profile,path in
               [('production','rtl/uj11_engine.v'),('apr','build/cp39-csr/uj11_engine.v')]}
    for profile,path in [('production','rtl/uj11_engine.v'),('apr','build/cp39-csr/uj11_engine.v')]:
        assert hashlib.sha256(bases[profile].encode()).hexdigest()==expected[path]
    prefix='''    wire [2:0] d_select=uword[12:10];
    wire d_byte_step=byte_instruction && a<4'd6;
    wire [1:0] d_small={d_select[1] && (!d_select[0] || !d_byte_step),
                        d_select[0] && (!d_select[1] || d_byte_step)};
    wire [15:0] d_disp={{7{ir[7] && !ir[14]}},
                       (ir[7:6] & {2{!ir[14]}}),ir[5:0],1'b0};
'''
    tree=prefix+'''    always @* begin
        d=d_select[2] ?
            (d_select[1] ? (d_select[0] ? psw : {8'b0,uword[7:0]}) :
                           (d_select[0] ? d_disp : mdr)) : {14'b0,d_small};
    end
'''
    sliced=prefix+'''    always @* begin
        d[7:0]=d_select[2] ?
            (d_select[1] ? (d_select[0] ? psw[7:0] : uword[7:0]) :
                           (d_select[0] ? d_disp[7:0] : mdr[7:0])) : {6'b0,d_small};
        // Only MDR, PSW and the replicated displacement sign reach the high byte.
        d[15:8]=(mdr[15:8] & {8{d_select==3'd4}}) |
                (psw[15:8] & {8{d_select==3'd7}}) |
                {8{d_select==3'd5 && ir[7] && !ir[14]}};
    end
'''
    # The sliced form needs only the low displacement byte. Its high sign
    # is generated directly, so do not retain unused upper signal bits.
    sliced=sliced.replace("wire [15:0] d_disp={{7{ir[7] && !ir[14]}},\n                       (ir[7:6] & {2{!ir[14]}}),ir[5:0],1'b0};",
                          "wire [7:0] d_disp_low={ir[6] && !ir[14],ir[5:0],1'b0};")
    sliced=sliced.replace('d_disp[7:0]','d_disp_low')
    outputs={}
    for profile,base in bases.items():
        start,end=d_span(base)
        apr_line="    wire [15:0] lookup_d = d | (apr_data & {16{mmu_active && uword[12:10]==0}});\n" if profile=='apr' else ''
        if apr_line:assert base[start:end].endswith(apr_line)
        for kind,block in [('baseline',base[start:end]),('tree',tree+apr_line),('sliced',sliced+apr_line)]:
            folder=out/kind/profile;folder.mkdir(parents=True,exist_ok=True)
            source=base[:start]+block+base[end:]
            engine=folder/'uj11_engine.v';engine.write_text(source)
            cone='''module d_input(input [35:0] uword,input [15:0] ir,mdr,psw,apr_data,
input [3:0] a,input byte_instruction,mmu_active,output reg [15:0] d,output wire [15:0] effective_d);
'''+block+('assign effective_d=lookup_d;\n' if profile=='apr' else 'assign effective_d=d;\n')+'endmodule\n'
            (folder/'d_input.v').write_text(cone)
            for path in folder.glob('*.v'):outputs[str(path.relative_to(ROOT))]=hashlib.sha256(path.read_bytes()).hexdigest()
    inputs=['tools/build_d_input_cp42.py','synth/reports/cp40i/source.tgz','synth/reports/cp40i/inputs.json']
    record=dict(scope='D input only, unchanged APR masked addition and all engine state/control',
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},outputs_sha256=outputs)
    (out/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    print('PASS CP42: frozen baseline, tree and sliced D-input; production/APR engines and exact cones')


if __name__=='__main__':main()
