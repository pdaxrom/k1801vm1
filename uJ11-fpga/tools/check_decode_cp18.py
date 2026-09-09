#!/usr/bin/env python3
"""Exhaustively compare the current decoder with CP17a, except the 33 newly enabled CC/NOP and MFPT encodings."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tarfile
ROOT=Path(__file__).resolve().parents[1]
def main():
    baseline=ROOT/'synth/reports/cp17a'
    with tarfile.open(baseline/'source.tgz') as archive:
        source=archive.extractfile('rtl/uj11_decode.v').read()
    expected=json.loads((baseline/'inputs.json').read_text())['files']['rtl/uj11_decode.v']
    assert hashlib.sha256(source).hexdigest()==expected,'baseline decoder hash'
    renamed,n=re.subn(r'\bmodule uj11_decode\b','module uj11_decode_baseline',source.decode())
    assert n==1
    build=ROOT/'build';build.mkdir(exist_ok=True)
    (build/'decode_baseline.v').write_text(renamed)
    (build/'tb_decode_equivalence.v').write_text('''`timescale 1ns/1ps
module tb_decode_equivalence;
    reg [15:0] ir;wire [9:0] baseline,current;integer i;
    uj11_decode_baseline a(.ir(ir),.entry(baseline));
    uj11_decode b(.ir(ir),.entry(current));
    initial begin
        for(i=0;i<65536;i=i+1)begin
            ir=i[15:0];#1;
            if(current!==(i==7?10'h017:((i & 'o177740)=='o000240 ? 10'h104+10'((i & 15)*8) : baseline)))$fatal(1,"decoder changed op%o %h/%h",ir,baseline,current);
        end
        $display("PASS CP18 decoder: all 65536 checked; only CC/NOP and MFPT differ from archived CP17a");$finish;
    end
endmodule
''')
    subprocess.run(['iverilog','-g2012','-Wall','-s','tb_decode_equivalence','-o','build/decode_equivalence',
                    'build/tb_decode_equivalence.v','build/decode_baseline.v','rtl/uj11_decode.v'],cwd=ROOT,check=True)
    subprocess.run(['vvp','build/decode_equivalence'],cwd=ROOT,check=True)
if __name__=='__main__':main()
