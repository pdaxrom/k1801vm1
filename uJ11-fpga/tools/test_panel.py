#!/usr/bin/env python3
"""Execute panel/navigation/step-over on the production CPU and pin frontend."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
from board_common import ROOT
from build_hardware import adapt
from test_odt import run


def test(odt,out,breakpoints=True):
    out.mkdir(parents=True,exist_ok=True)
    adapt()
    (out/'result.json').unlink(missing_ok=True)
    run(odt,execute=False,out=out)
    fixture=(out/'odt_cases.vh').read_text().split('repeat(4)',1)[0]
    if breakpoints:
        resident=ROOT/'build/hardware/resid.bin'
        data=resident.read_bytes()
        for i in range(0,len(data),2):fixture+=f"memory[{(65536+0o200+i)//2}]=16'o{int.from_bytes(data[i:i+2],'little'):o};\n"
        sym=json.loads((odt/'result.json').read_text())['symbols']
        for a,v in ((0o320,0o177),(0o322,0o177640),(0o164,sym['ENTER']),(0o170,0o200),(0o172,0o340)):
            fixture+=f"memory[{(65536+a)//2}]=16'o{v:o};\n"
        cases=(ROOT/'tests/panel_cases.vh').read_text().replace('command("T")','command("T 10000")').replace('send({"T",','send({"T 10000",')
        cases=cases[:cases.index('$display("PASS CP65:')]+(ROOT/'tests/panel_breakpoints.vh').read_text()
        (out/'panel_all.vh').write_text(cases)
        fixture+='`include "panel_all.vh"\n'
    else:fixture+='`include "panel_cases.vh"\n'
    (out/'odt_cases.vh').write_text(fixture)
    sym=json.loads((odt/'result.json').read_text())['symbols']
    font_source=(ROOT/'demos/rt11/panel/PNLDRV.MAC').read_text().split('FONT:',1)[1]
    font=[]
    for m in re.finditer(r'\.BYTE\s+([^;\n]+)',font_source):font.extend(int(n.strip(),8) for n in m[1].split(','))
    assert len(font)==320
    (out/'font.hex').write_text(''.join(f'{v:02x}\n' for v in font))
    model=(ROOT/'tests/rt11_harness.vh').read_text()
    matrix=model[model.index('    // Same matrix'):model.index('    uj11_board')]
    pins=model[model.index('    always @(posedge sck)'):model.index('    function [15:0] upper')]
    keytask=model[model.index('    task keypress'):model.index('    always @(posedge clk)if(!reset)')].replace('clocks','cycles')
    extra='''
    reg [7:0] pinreg=8'h12,shifted=0,outputs=0;
    reg [3:0] keys=0;
    integer keycode=-1,display_count=0,display_frames=0;
    reg [639:0] display_bits=0,last_display=0;
    wire din=pinreg[0],ce=pinreg[1],sck=pinreg[2],rs=pinreg[3],blank=pinreg[4],latch=pinreg[5];
    always @(posedge clk)if(!reset && ack && io && writing && address==16'o166001)pinreg<=wdata[7:0];
    function [15:0] upper(input integer a);return memory[(65536+a)/2];endfunction
    integer frames_before,auto_at,auto_start,auto_delta,auto_pause,auto_step,over_prompt;
    string photo_line="143042: 004567 177324  JSR R5,142372";
'''
    names=('KLAST','SCROLL','PNEN','SCREEN','RESULT','REGS','META','SCEND','SCDIR','SCTIME','HCNT','HPOS','DCUR','RSEL','RADIX','PVAL','PANEDI','OVACT','OVLEFT')
    if breakpoints:names+=('MENU','BPTAB','BPTEMP')
    extra+=''.join(f'localparam integer O_{n}={sym[n]};\n' for n in names)
    extra+=f'initial $readmemh("{out}/font.hex",font);\n'
    tb=(ROOT/'tests/tb_odt.v').read_text()
    tb=tb.replace("address[15:1]==15'o73000 ? 16'h8000", "address[15:1]==15'o73000 ? {pinreg,keys,4'b0}")
    tb=tb.replace('    always #5 clk=~clk;',extra+matrix+pins+keytask+'`include "odt_panel_scroll.vh"\n    always #5 clk=~clk;')
    tb=tb.replace('cycles>10000000','cycles>150000000')
    (out/'tb.v').write_text(tb)
    core,_=adapt();sources=[str((out/'tb.v').relative_to(ROOT))]+core+['rtl/uj11_rom.v']
    cmd=['verilator','--binary','--timing','--top-module','tb_odt','-j','4','--Mdir',str(out/'obj'),'-I'+str(out),'-I'+str(ROOT/'tests')]+sources
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    sim=[str(out/'obj/Vtb_odt'),f'+UART_LOG={out}/uart.txt']
    with (out/'simulation.log').open('w') as log:r=subprocess.run(sim,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    log=(out/'simulation.log').read_text();print(log[-3000:]);r.check_returncode()
    counts=re.search(r'PASS CP6[56]: (\d+) checks (\d+) windows (\d+) clocks',log);assert counts
    paths=sources+[str((out/n).relative_to(ROOT)) for n in ('odt_cases.vh','font.hex')]
    paths+=[str((odt/n).relative_to(ROOT)) for n in ('result.json','payload.bin')]
    paths+=['tools/test_panel.py','tools/test_odt.py','tests/panel_cases.vh','tests/odt_panel_scroll.vh','tests/tb_odt.v','tests/rt11_harness.vh','demos/rt11/panel/PNLDRV.MAC']
    if breakpoints:paths+=['tests/panel_breakpoints.vh','build/hardware/resid.bin',str((out/'panel_all.vh').relative_to(ROOT))]
    record=dict(passed=True,checks=int(counts[1]),windows=int(counts[2]),clocks=int(counts[3]),compile_command=cmd,simulation_command=sim,
                files={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--odt',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();test(a.odt.resolve(),a.out.resolve(),breakpoints=True)
