#!/usr/bin/env python3
"""Fast production monitor + complete electrical key/HDSP frontend, CPU RAM bus."""
import argparse,json,subprocess,hashlib,re
from pathlib import Path
from board_common import ROOT
from build_debug_cp63 import adapt
from test_odt_cp64 import run


def panel(odt,out=None):
    out=out or ROOT/'build/cp64-panel';out.mkdir(parents=True,exist_ok=True)
    sym=json.loads((odt/'result.json').read_text())['symbols']
    # Generate the same architectural boot fixture, then replace command cases.
    run(odt,execute=False,out=out)
    fixture=(out/'odt_cases.vh').read_text()
    # All initial RAM words precede repeat(4).
    fixture=fixture[:fixture.index('repeat(4)')]
    # User's photographed instruction, plus a short following instruction.
    fixture+="memory['o143042/2]=16'o004567;memory['o143044/2]=16'o177324;memory['o143046/2]=16'o000240;\n"
    fixture+='''repeat(4)@(negedge clk);reset=0;wait(dut.engine.debug_enabled);
        @(negedge clk);halt_button=1;@(negedge clk);halt_button=0;wait(prompts==1);
        segment="";keypress(16);keypress(19);contains("[PANEL] R");
        segment="";keypress(18);contains("R1=");
        segment="";keypress(19);keypress(7);keypress(19);contains("[PANEL] R 000001 000007");
        command("D 20000");segment="";keypress(18);eq(upper(O_SCROLL),1,"right scroll");
        keypress(17);eq(upper(O_SCROLL),0,"left scroll");
        if(segment!="")$fatal(1,"scroll printed UART");
        scroll_regression();
        $display("PASS CP64 electrical panel: %0d checks %0d clocks",checks,cycles);$finish;
'''
    (out/'odt_cases.vh').write_text(fixture)
    font_source=(ROOT/'demos/rt11/panel/PNLDRV.MAC').read_text().split('FONT:',1)[1]
    font=[]
    for match in re.finditer(r'\.BYTE\s+([^;\n]+)',font_source):font.extend(int(n.strip(),8) for n in match[1].split(','))
    assert len(font)==320
    (out/'font.hex').write_text(''.join(f'{n:02x}\n' for n in font))
    model=(ROOT/'tb/tb_odt_rt11.v').read_text()
    matrix=model[model.index('    // Same matrix'):model.index('    uj11_board')]
    pins=model[model.index('    always @(posedge sck)'):model.index('    function [15:0] upper')]
    keytask=model[model.index('    task keypress'):model.index('    always @(posedge clk)if(!reset)')]
    keytask=keytask.replace('clocks','cycles')
    tb=(ROOT/'tb/tb_odt_cp64.v').read_text().replace('build/cp64-test/uart.txt','build/cp64-panel/uart.txt')
    tb=tb.replace("address[15:1]==15'o73000 ? 16'h8000", "address[15:1]==15'o73000 ? {pinreg,keys,4'b0}")
    extra='''
    reg [7:0] pinreg=8'h12,shifted=0,outputs=0;
    reg [3:0] keys=0;
    integer keycode=-1,display_count=0,display_frames=0;
    reg [639:0] display_bits=0,last_display=0;
    wire din=pinreg[0],ce=pinreg[1],sck=pinreg[2],rs=pinreg[3],blank=pinreg[4],latch=pinreg[5];
    always @(posedge clk)if(!reset && ack && io && writing && address==16'o166001)pinreg<=wdata[7:0];
    function [15:0] upper(input integer a);return memory[(65536+a)/2];endfunction
'''
    extra+=''.join(f'localparam integer O_{n}={sym[n]};\n' for n in ('KLAST','SCROLL','PNEN','SCREEN','RESULT','REGS','META'))
    extra+=f'initial $readmemh("{out}/font.hex",font);\n'
    tb=tb.replace('    always #5 clk=~clk;', extra+matrix+pins+keytask+'`include "odt_panel_scroll.vh"\n    always #5 clk=~clk;')
    tb=tb.replace('cycles>10000000','cycles>100000000')
    tb=tb.replace('if(stopped || cycles>', 'if(cycles!=0 && cycles%1000000==0)begin $display("PANEL progress%0d key%0d last%o cand%o cnt%o PC%o outputs%h rows%h",cycles,keycode,upper(O_KLAST),upper(O_KLAST+2),upper(O_KLAST+4),dut.engine.dp.rf.words[7],outputs,keys);$fflush();end\n        if(stopped || cycles>')
    src=out/'tb.v';src.write_text(tb)
    core,_=adapt();sources=[str(src)]+core+['rtl/uj11_rom.v']
    cmd=['verilator','--binary','--timing','--top-module','tb_odt_cp64','-j','4','--Mdir',str(out/'obj'),'-I'+str(out),'-I'+str(ROOT/'tb')]+sources
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:r=subprocess.run([str(out/'obj/Vtb_odt_cp64'),f'+UART_LOG={out}/uart.txt'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    log=(out/'simulation.log').read_text();print(log[-4000:]);r.check_returncode()
    assert 'PASS CP64 electrical panel:' in log
    counts=re.search(r'PASS CP64 electrical panel: (\d+) checks (\d+) clocks',log)
    assert counts and 'PASS CP64 scroll regression:' in log
    files=sources+[str((out/'odt_cases.vh').relative_to(ROOT)),str((out/'font.hex').relative_to(ROOT)),str((odt/'result.json').relative_to(ROOT)),str((odt/'payload.bin').relative_to(ROOT)),'tools/test_odt_panel_cp64.py','tools/test_odt_cp64.py','tb/tb_odt_rt11.v','tb/tb_odt_cp64.v','tb/odt_panel_scroll.vh','demos/rt11/panel/PNLDRV.MAC']
    (out/'result.json').write_text(json.dumps(dict(passed=True,checks=int(counts[1]),clocks=int(counts[2]),compile_command=cmd,files={str(Path(p).resolve().relative_to(ROOT)) if Path(p).is_absolute() else p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files}),indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--odt',type=Path,required=True);p.add_argument('--out',type=Path)
    a=p.parse_args();panel(a.odt.resolve(),a.out.resolve() if a.out else None)
