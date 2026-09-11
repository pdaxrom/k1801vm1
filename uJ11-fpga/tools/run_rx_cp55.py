#!/usr/bin/env python3
"""CP55 FRAM contract, sequential reads, resets, memory and full-board checks."""
import hashlib
import json
import subprocess
from board_common import ROOT
from build_rx_cp55 import OUT, adapt
from build_fram_cp52 import replace_once
import run_fram_cp52 as runner


def main():
    core,board=adapt()
    out=ROOT/'build/cp55-tests';out.mkdir(parents=True,exist_ok=True);runner.OUT=out
    fram='build/cp55-rx/shared/uj11_board_fram.v';model='reference/lsi11/spi_fram_model.v'
    gold=out/'uj11_fram_gold.v'
    gold.write_text(replace_once((OUT/'baseline/uj11_board_fram.v').read_text(),'module uj11_board_fram','module uj11_fram_gold'))
    miter=(ROOT/'tb/tb_fram_miter_cp47.v').read_text().replace('cp47','cp55').replace('CP47','CP55')
    miter=replace_once(miter,'    reg [15:0] address=0,data=0;',
        '    reg keep_read=1,close_read=0;\n    reg [15:0] address=0,data=0;')
    miter=miter.replace('byte_access,bank,\n','byte_access,bank,keep_read,close_read,\n')
    miter=replace_once(miter,'(!QUALIFIED_DATA || gold_ready || !gold_busy)',
        '(!QUALIFIED_DATA || gold_ready || !gold_busy || gold.state==9)')
    miter=replace_once(miter,'        checks=checks+1;', '''        if(gold_data[7:0]!==gate_data[7:0])$fatal(1,"low rdata mismatch");
        if(gold.next_word!==gate.next_word)$fatal(1,"cursor mismatch");
        checks=checks+1;''')
    miter=replace_once(miter,'        // Cancel at every clock offset', '''        // Retained READ with X/Z MISO, then a high-address alias and closure.
        writing=0;byte_access=0;bank=0;keep_read=1;
        for(i=0;i<64;i=i+1)begin address=16'h4000+16'(2*i);beat();end
        address=0;beat();address=16'h8002;beat();
        close_read=1;repeat(4)@(posedge clk);#2;close_read=0;
        address=16'h8004;beat();
        // Cancel at every clock offset''')
    (out/'tb_miter.v').write_text(miter)
    random=replace_once((ROOT/'tb/tb_board_fram.v').read_text(),'bank,address,data,value',"bank,1'b1,1'b0,address,data,value")
    (out/'tb_random.v').write_text(random)
    runs=[]
    for divider in (1,2,3):
        runs.append(runner.simulate('tb_fram_miter_cp55',f'miter-{divider}',
            ['build/cp55-tests/tb_miter.v','build/cp55-tests/uj11_fram_gold.v',fram],
            ['-Wall',f'-Ptb_fram_miter_cp55.CLK_DIV={divider}']))
        runs.append(runner.simulate('tb_board_fram',f'random-{divider}',
            ['build/cp55-tests/tb_random.v',fram,model],['-Wall',f'-Ptb_board_fram.CLK_DIV={divider}']))
        runs.append(runner.simulate('tb_fram_cp52',f'protocol-{divider}',
            ['tb/tb_fram_cp52.v',fram,model],['-Wall',f'-Ptb_fram_cp52.CLK_DIV={divider}']))
    negative=[]
    for defect,old,new,message in [
        ('byte-high','if(byte_access)rdata[15:8]<=0;',"if(byte_access)rdata[15:8]<=8'hff;",'qualified rdata mismatch'),
        ('cursor-alias','address[15:1]==next_word','address[14:1]==next_word[13:0]','SPI/handshake mismatch')]:
        path=out/(defect+'.v');path.write_text(replace_once((ROOT/fram).read_text(),old,new))
        with (out/(defect+'-build.log')).open('w') as log:
            subprocess.run(['iverilog','-g2012','-Wall','-s','tb_fram_miter_cp55','-o',str(out/defect),
                str(out/'tb_miter.v'),str(gold),str(path)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (out/(defect+'.log')).open('w') as log:
            run=subprocess.run(['vvp',str(out/defect)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        assert run.returncode!=0 and message in (out/(defect+'.log')).read_text(),defect
        negative.append(dict(defect=defect,returncode=run.returncode,detected=message))
        print('PASS CP55 executable mutation rejected:',defect,flush=True)
    with (out/'lint.log').open('w') as log:
        subprocess.run(['verilator','--lint-only','--Wall','--top-module','uj11_board_fram',fram],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    bus=replace_once((ROOT/'tb/tb_board_bus.v').read_text(),'        $display("PASS board bus:',
        (ROOT/'tb/bus_fram_cp52_checks.vh').read_text()+'\n        $display("PASS board bus:')
    (out/'tb_bus.v').write_text(bus)
    runs.append(runner.simulate('tb_board_bus','bus',['build/cp55-tests/tb_bus.v']+board+[model]))
    bench=replace_once((ROOT/'tb/tb_board_bench_cp51.v').read_text(),'sck_edges-start_sck!=12288',
        'sck_edges-start_sck!=(workload==7 ? 12288 : 4224)')
    bench=bench.replace('build/cp51-board-bench.json','build/cp55-tests/bench.json').replace('PASS CP51','PASS CP55')
    (out/'tb_bench.v').write_text(bench)
    runs.append(runner.simulate('tb_board_bench_cp51','bench',['build/cp55-tests/tb_bench.v']+core+board+['rtl/uj11_rom.v',model],verilator=True))
    vendor=replace_once(bench,'module tb_board_bench_cp51;',
        "module tb_board_bench_cp51;\n    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));")
    vendor=vendor.replace('build/cp55-tests/bench.json','build/cp55-tests/vendor-bench.json')
    (out/'tb_vendor.v').write_text(vendor)
    runs.append(runner.simulate('tb_board_bench_cp51','vendor',['build/cp55-tests/tb_vendor.v']+core+board+[
        'microcode/generated/uj11_m0_ebr.v',model]+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')],['-DUJ11_VENDOR_ROM']))
    rows=json.loads((out/'bench.json').read_text())
    assert rows==json.loads((out/'vendor-bench.json').read_text())
    reference=json.loads((ROOT/'tb/reports/cp54/cp54-tests/result.json').read_text())['variants'][0]['workloads']
    assert rows==reference,'full-board counters differ from CP54'
    for path in out.glob('*build.log'):assert not any(x in path.read_text() for x in ('Warning','Error','warning','error','sorry')),path
    inputs=['tools/run_rx_cp55.py','tools/run_fram_cp52.py','tools/build_rx_cp55.py','tools/build_fram_cp52.py',
        'tools/board_common.py','tb/tb_fram_miter_cp47.v','tb/tb_fram_cp52.v','tb/tb_board_fram.v','tb/tb_board_bus.v',
        'tb/bus_fram_cp52_checks.vh','tb/tb_board_bench_cp51.v','microcode/generated/m0.mem',
        'microcode/generated/decode.mem','microcode/generated/firmware.mem',
        'tb/reports/cp54/cp54-tests/result.json','build/cp55-rx/inputs.json']
    inputs += [str(p.relative_to(ROOT)) for p in out.glob('*.v')]
    report=dict(runs=runs,negative=negative,workloads=rows,cp54_counters_identical=True,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        logs_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.log')})
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP55 local full-board tests: exact CP54 portable/vendor counters')


if __name__=='__main__':main()
