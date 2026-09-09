#!/usr/bin/env python3
"""Exhaustively compare the current decoder with CP26a, except the 32 newly enabled FIS encodings."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tarfile
ROOT=Path(__file__).resolve().parents[1]
def main():
    baseline=ROOT/'synth/reports/cp26a'
    with tarfile.open(baseline/'source.tgz') as archive:
        source=archive.extractfile('rtl/uj11_decode.v').read()
    expected=json.loads((baseline/'inputs.json').read_text())['files']['rtl/uj11_decode.v']
    assert hashlib.sha256(source).hexdigest()==expected,'baseline decoder hash'
    renamed,n=re.subn(r'\bmodule uj11_decode\b','module uj11_decode_baseline',source.decode())
    assert n==1
    build=ROOT/'build';build.mkdir(exist_ok=True)
    (build/'cp27_decode_baseline.v').write_text(renamed)
    (build/'cp27_tb_decode_equivalence.v').write_text('''`timescale 1ns/1ps
module tb_decode_equivalence;
    reg [15:0] ir;wire [9:0] baseline,current;integer i;
    uj11_decode_baseline a(.ir(ir),.entry(baseline));
    uj11_decode b(.ir(ir),.entry(current));
    initial begin
        for(i=0;i<65536;i=i+1)begin
            ir=i[15:0];#1;
            if(current!==((i & 'o177740)=='o075000 ? 10'h011 : baseline))$fatal(1,"decoder changed op%o %h/%h",ir,baseline,current);
        end
        $display("PASS CP27 decoder: all 65536 checked; only FIS differ from archived CP26a");$finish;
    end
endmodule
''')
    subprocess.run(['iverilog','-g2012','-Wall','-s','tb_decode_equivalence','-o','build/cp27_decode_equivalence',
                    'build/cp27_tb_decode_equivalence.v','build/cp27_decode_baseline.v','rtl/uj11_decode.v'],cwd=ROOT,check=True)
    subprocess.run(['vvp','build/cp27_decode_equivalence'],cwd=ROOT,check=True)
if __name__=='__main__':main()
