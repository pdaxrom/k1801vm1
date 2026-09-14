#!/usr/bin/env python3
"""Run an unmodified extracted DEC FP11-A image on the CP67 CPU/FP firmware.
Diagnostic bench: zero-wait RAM, KL11 TX-ready/RX-empty, switches=0. All other
external I/O addresses fault. No FPGA hardware or guest opcode is patched.
"""
import argparse,json,re,subprocess
from pathlib import Path
from board_common import ROOT
from build_modules_cp67 import adapt
from build_software_cp67 import sha
from run_service_cp59 import Program

def run(name,psw_read_adapter=False):
    assert name in ('FFPAA1.BIN','FFPBA0.BIN','FFPCB0.BIC')
    out=ROOT/'build/cp77-diagnostics'/(name[:6]+('-psw' if psw_read_adapter else ''));out.mkdir(exist_ok=True)
    fp=ROOT/'build/cp77-fp11/software';f=json.loads((fp/'result.json').read_text())['symbols']
    image=bytearray(131072);image[:65536]=(out.parent/(name+'.ram')).read_bytes()
    code=(fp/'image.bin').read_bytes();image[65536+f['INIT']:65536+f['INIT']+len(code)]=code
    boot=Program(0o2000).mov(6,0o3700).mov(0,0).emit(0o42)
    boot.mov(0,0o7000).store(0,0o122).emit(0o4737,f['INIT'])
    boot.mov(0,0o200).store(0,0o100).mov(0,0).store(0,0o102).mov(6,0o40000).emit(0o10)
    for a,w in [(0,0o2000),(2,0o340)]+[(boot.base+i*2,w) for i,w in enumerate(boot.words)]:
        image[65536+a:65536+a+2]=w.to_bytes(2,'little')
    (out/'memory.mem').write_text(''.join(f'{int.from_bytes(image[i:i+2],"little"):04x}\n' for i in range(0,len(image),2)))
    tb=(ROOT/'tb/tb_odt_cp64.v').read_text()
    a=tb.index('    wire error=');b=tb.index('    wire ack=',a)
    tb=tb[:a]+"    wire error=io && !(address>=16'o177560 && address<=16'o177566) && address!=16'o177570;\n"+tb[b:]
    if psw_read_adapter:
        # Explicit missing-platform-register adapter, not a production change.
        tb=tb.replace("address!=16'o177570;", "address!=16'o177570 && !(address==16'o177776 && !writing);")
        tb=tb.replace("wire [15:0] word_data=io ? (", "wire [15:0] word_data=io && address==16'o177776 ? dut.psw : io ? (")
    tb=tb.replace('cycles>10000000','cycles>500000000').replace('uart={uart,wdata[7:0]};segment={segment,wdata[7:0]};','segment={segment,wdata[7:0]};')
    tb=tb.replace('    always #5 clk=~clk;','''    function automatic has(input string text,part);
        has=0;for(integer k=0;k+part.len()<=text.len();k++)if(text.substr(k,k+part.len()-1)==part)has=1;
    endfunction
    always #5 clk=~clk;''')
    tb=tb.replace('$fwrite(logfile,"%c",wdata[7:0]);$fflush(logfile);','''$fwrite(logfile,"%c",wdata[7:0]);$fflush(logfile);
                    if(wdata[7:0]==13 && has(segment,"END PASS #"))begin
                        if(!has(segment,"TOTAL ERRORS SINCE LAST REPORT      0"))$fatal(1,"DEC diagnostic errors: %s",segment);
                        $display("PASS DEC diagnostic: %0d core clocks",cycles);$finish;
                    end
                    if(segment.len()>4096)segment=segment.substr(2048,segment.len()-1);''')
    a=tb.index('    initial begin\n');tb=tb[:a]+f'''    initial begin
        logfile=$fopen("{out}/uart.txt","w");
        $readmemh("{out}/memory.mem",memory);
        repeat(4)@(negedge clk);reset=0;
    end
endmodule
'''
    (out/'tb.v').write_text(tb);core,_=adapt();sources=[str((out/'tb.v').relative_to(ROOT))]+core+['rtl/uj11_rom.v']
    cmd=['verilator','--binary','--timing','--top-module','tb_odt_cp64','-j','4','--Mdir',str(out/'obj')]+sources
    files=[Path(__file__),out/'tb.v',out/'memory.mem',out.parent/'extraction.json',fp/'result.json']+[ROOT/p for p in core]
    inputs={str(p.relative_to(ROOT)):sha(p) for p in files};(out/'inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:p=subprocess.run([str(out/'obj/Vtb_odt_cp64')],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    text=(out/'simulation.log').read_text();print(text[-1200:]);m=re.search(r'PASS DEC diagnostic: (\d+) core clocks',text)
    for path,h in inputs.items():assert sha(ROOT/path)==h,path
    (out/'result.json').write_text(json.dumps(dict(passed=p.returncode==0 and m is not None,name=name,psw_read_adapter=psw_read_adapter,clocks=int(m[1]) if m else None,
        inputs=inputs,uart_sha256=sha(out/'uart.txt')),indent=2)+'\n')
    p.check_returncode()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('name');p.add_argument('--psw-read-adapter',action='store_true');a=p.parse_args();run(a.name,a.psw_read_adapter)
