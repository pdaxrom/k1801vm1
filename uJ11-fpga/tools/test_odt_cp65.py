#!/usr/bin/env python3
"""Execute CP65 panel/navigation/step-over on production CPU and pin frontend."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
from board_common import ROOT
from build_debug_cp63 import adapt
from test_odt_cp64 import run


def test(odt,out):
    out.mkdir(parents=True,exist_ok=True)
    run(odt,execute=False,out=out)
    fixture=(out/'odt_cases.vh').read_text().split('repeat(4)',1)[0]
    fixture+='`include "odt_cp65_cases.vh"\n'
    (out/'odt_cases.vh').write_text(fixture)
    sym=json.loads((odt/'result.json').read_text())['symbols']
    font_source=(ROOT/'demos/rt11/panel/PNLDRV.MAC').read_text().split('FONT:',1)[1]
    font=[]
    for m in re.finditer(r'\.BYTE\s+([^;\n]+)',font_source):font.extend(int(n.strip(),8) for n in m[1].split(','))
    assert len(font)==320
    (out/'font.hex').write_text(''.join(f'{v:02x}\n' for v in font))
    model=(ROOT/'tb/tb_odt_rt11.v').read_text()
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
    extra+=''.join(f'localparam integer O_{n}={sym[n]};\n' for n in names)
    extra+=f'initial $readmemh("{out}/font.hex",font);\n'
    tb=(ROOT/'tb/tb_odt_cp64.v').read_text()
    tb=tb.replace("address[15:1]==15'o73000 ? 16'h8000", "address[15:1]==15'o73000 ? {pinreg,keys,4'b0}")
    tb=tb.replace('    always #5 clk=~clk;',extra+matrix+pins+keytask+'`include "odt_panel_scroll.vh"\n    always #5 clk=~clk;')
    tb=tb.replace('cycles>10000000','cycles>150000000')
    (out/'tb.v').write_text(tb)
    core,_=adapt();sources=[str((out/'tb.v').relative_to(ROOT))]+core+['rtl/uj11_rom.v']
    cmd=['verilator','--binary','--timing','--top-module','tb_odt_cp64','-j','4','--Mdir',str(out/'obj'),'-I'+str(out),'-I'+str(ROOT/'tb')]+sources
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    sim=[str(out/'obj/Vtb_odt_cp64'),f'+UART_LOG={out}/uart.txt']
    with (out/'simulation.log').open('w') as log:r=subprocess.run(sim,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    log=(out/'simulation.log').read_text();print(log[-3000:]);r.check_returncode()
    counts=re.search(r'PASS CP65: (\d+) checks (\d+) windows (\d+) clocks',log);assert counts
    paths=sources+[str((out/n).relative_to(ROOT)) for n in ('odt_cases.vh','font.hex')]
    paths+=[str((odt/n).relative_to(ROOT)) for n in ('result.json','payload.bin')]
    paths+=['tools/test_odt_cp65.py','tools/test_odt_cp64.py','tb/odt_cp65_cases.vh','tb/odt_panel_scroll.vh','tb/tb_odt_cp64.v','tb/tb_odt_rt11.v','demos/rt11/panel/PNLDRV.MAC']
    record=dict(passed=True,checks=int(counts[1]),windows=int(counts[2]),clocks=int(counts[3]),compile_command=cmd,simulation_command=sim,
                files={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--odt',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();test(a.odt.resolve(),a.out.resolve())
