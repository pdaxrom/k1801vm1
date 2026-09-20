#!/usr/bin/env python3
"""Verify the FIS .SFPA adapter after a real CP67 cold boot with retained modules."""
import argparse,hashlib,json,re,shutil,subprocess
from pathlib import Path
from board_common import ROOT


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def run(out,adapter):
    out.mkdir(parents=True,exist_ok=True);assert not (out/'test.dsk').exists()
    baseline=ROOT/'build/cp81-basic/rtl-a';retained=ROOT/'build/cp81-basic/rtl-error/fram-before.mem'
    shutil.copyfile(baseline/'test.dsk',out/'test.dsk')
    sav=adapter/'B81FIJ.SAV'
    subprocess.run([str(ROOT/'../lsi11/rt11tool'),'add',str(out/'test.dsk'),str(sav),'B81FIJ.SAV'],check=True,capture_output=True)
    t=(baseline/'tb.v').read_text();t=t[:t.index('    initial begin\n        #1;')]
    t+='''    initial begin
        #1;$readmemh("RETAINED",fram.memory);
        repeat(15)@(negedge clk);reset=0;
        wait(prompts==3);wait(serial_chars==bus_chars);repeat(2000000)@(negedge clk);
        phase=1;shell("SET SL OFF");
        check(dut.cpu.engine.service_ready==3 && dut.cpu.engine.debug_enabled,"retained modules cold initialized");
        phase=2;test_basic("B81FIJ",0,1);
        phase=3;segment="";send_line("RUN B81FIJ");wait(option_count>1);
        wait(serial_chars==bus_chars);send_line("A");wait(ready_count>2);
        wait(serial_chars==bus_chars);repeat(100000)@(negedge clk);
'''.replace('RETAINED',str(retained.relative_to(ROOT)))
    errors=[('PRINT 1/0','?DIVISION BY ZERO'),('PRINT SQR(-1)','?NEGATIVE SQUARE ROOT'),
            ('PRINT 1E30*1E30','?FLOATING OVERFLOW'),('PRINT 2+2',' 4 '),
            ('PRINT 1E-30*1E-30','?FLOATING UNDERFLOW'),('PRINT 1/0','?DIVISION BY ZERO'),('PRINT 2+2',' 4 ')]
    for c,e in errors:t+=f'        basic_command("{c}");contains("{e}");\n'
    t+='''        shell("BYE");check(rx_overruns==0,"no UART overruns");
        $display("PASS CP81 FIS ABI: %0d checks, %0d clocks, %0d UART bytes",checks,clocks,serial_chars);
        $fclose(uart_file);$finish;
    end
endmodule
'''
    (out/'tb.v').write_text(t)
    cmd=json.loads((ROOT/'tb/reports/cp81-basic/rtl/replay.json').read_text())['compile']
    cmd=[str(out/'tb.v') if a=='build/cp81-basic/rtl-a/tb.v' else a for a in cmd]
    cmd[cmd.index('--Mdir')+1]=str(out/'obj')
    runargs=[str(out/'obj/Vtb_odt_rt11'),'+SD_IMAGE='+str(out/'test.dsk'),'+UART_LOG='+str(out/'uart.txt')]
    old=json.loads((baseline/'inputs.json').read_text())
    inputs={p:sha(ROOT/p) for p in old['files']}
    inputs.update({str(p.relative_to(ROOT)):sha(p) for p in [out/'tb.v',retained,sav,Path(__file__)]})
    manifest=dict(files=inputs,image_sha256=sha(out/'test.dsk'),compile=cmd,run=runargs,errors=errors,
                  initialization='Retained SPI FRAM bytes only; no CPU state forcing; production cold walker')
    (out/'inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:subprocess.run(runargs,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    for p,h in inputs.items():assert sha(ROOT/p)==h,p
    assert sha(out/'test.dsk')==manifest['image_sha256']
    log=(out/'simulation.log').read_text();m=re.search(r'PASS CP81 FIS ABI: (\d+) checks, (\d+) clocks, (\d+) UART bytes',log);assert m
    result=dict(passed=True,checks=int(m[1]),clocks=int(m[2]),uart_bytes=int(m[3]),files={p:sha(out/p) for p in ['inputs.json','simulation.log','uart.txt','build.log']})
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(log[-2000:])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--adapter',type=Path,required=True)
    a=p.parse_args();run(a.out.resolve(),a.adapter.resolve())
