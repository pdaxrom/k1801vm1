#!/usr/bin/env python3
"""CP55 sequential data-contract proof, including the retained READ cursor."""
import hashlib
import json
import os
import subprocess
from board_common import ROOT
from build_rx_cp55 import OUT, build
from build_fram_cp52 import replace_once
from check_fram_cp47 import probe, sat_miter


def main():
    build()
    out=ROOT/'build/cp55-proof';out.mkdir(parents=True,exist_ok=True)
    tests=[]
    for divider in (1,3):
        for defect in (('none','byte-high','cursor-alias') if divider==1 else ('none',)):
            folder=out/(str(divider)+'-'+defect);folder.mkdir(exist_ok=True)
            gold=probe((OUT/'baseline/uj11_board_fram.v').read_text(),'gold',divider)
            gate=probe((OUT/'shared/uj11_board_fram.v').read_text(),'gate',divider)
            if defect=='byte-high':gate=replace_once(gate,'if(byte_access)rdata[15:8]<=0;',"if(byte_access)rdata[15:8]<=8'hff;")
            if defect=='cursor-alias':gate=replace_once(gate,'address[15:1]==next_word','address[14:1]==next_word[13:0]')
            gold,gate,miter=sat_miter(gold,gate)
            # Extend CP47's proven partial-RX invariant to the new native
            # cursor, including arbitrary keep/close inputs on retained reads.
            texts=[gold,gate,miter]
            for i,text in enumerate(texts):
                text=text.replace('[31:0]','[47:0]')
                if i<2:text=replace_once(text,'{state,active,seen,tx,bit_count,divider}',
                    '{state,active,seen,tx,bit_count,divider,next_word}')
                else:
                    text=replace_once(text,'bank,spi_miso,','bank,spi_miso,keep_read,close_read,')
                    text=text.replace('.address(address),','.keep_read(keep_read),.close_read(close_read),.address(address),')
                texts[i]=text
            for name,text in zip(('gold','gate','miter'),texts):(folder/(name+'.v')).write_text(text)
            script='''read_verilog -sv gold.v gate.v miter.v
prep -top miter -flatten
opt
sat -verify -prove ok 1 -set-init-zero -set-at 1 rst 1 -seq 2 -tempinduct -maxsteps 12
'''
            (folder/'proof.ys').write_text(script)
            with (folder/'proof.log').open('w') as log:
                run=subprocess.run([str(ROOT/'build/formal/bin/yowasp-yosys'),'-s','proof.ys'],cwd=folder,
                    stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')))
            text=(folder/'proof.log').read_text()
            if defect=='none':assert run.returncode==0 and 'Induction step proven: SUCCESS!' in text,text[-2000:]
            else:assert run.returncode!=0 and 'proof did fail' in text,text[-2000:]
            tests.append(dict(divider=divider,defect=defect,returncode=run.returncode))
            print('PASS CP55 proof',divider,defect,flush=True)
    inputs=['tools/check_rx_cp55.py','tools/check_fram_cp47.py','tools/build_rx_cp55.py','build/cp55-rx/inputs.json']
    inputs += [str(p.relative_to(ROOT)) for p in OUT.rglob('*.v')]
    inputs += [str(p.relative_to(ROOT)) for p in out.rglob('*') if p.suffix in ('.v','.ys')]
    report=dict(tests=tests,method='Two-state SAT temporal induction with partial RX and full control/cursor invariants',
        assumptions='Zero initial formal state and reset at first step, then all inputs (including reset, keep/close, MISO and request/address) unrestricted',
        qualified_data=True,inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        logs_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*.log')})
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
