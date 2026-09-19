#!/usr/bin/env python3
"""Qualify the exact 22-instruction hardware probe on the frozen CP67 core."""
import hashlib
import json
import subprocess
from pathlib import Path
from board_common import ROOT, CORE
from build_modules_cp67 import OUT as HW
from build_fp11_cp80 import OUT
from fp_psw_hardware_cp80 import plan


def run():
    out=OUT/'hardware-program';out.mkdir(exist_ok=True)
    template=OUT/'psw-sync/tb.v'
    p=plan();source=template.read_text().split('    // A CPU-local PSW operand')[0]
    source+='''    always @(posedge clk) if(!reset && request && !bank && address==16'o177776)
        $fatal(1,"PSW operand escaped to bus");
    initial begin
        for(integer n=0;n<65536;n++)memory[n]=0;
        $readmemh("build/cp80-fp11/psw-sync/image.mem",memory);
        for(integer n=0;n<23;n++)values[n]=0;
        values[0]=16'o170127;values[2]=16'o340;values[9]=16'o30000;
        guest_start=16'o6000;prepare();
'''
    for i,w in enumerate(p['words']):source+=f'        memory[{(p["base"]+2*i)//2}]=16\'o{w:o};\n'
    for i,s in enumerate(p['steps'],1):
        source+=f'        scenario={i};finish_fp();\n'
        source+=f'        eq(psw,16\'o{s["psw"]:o},"program PSW");\n'
        source+=f'        eq(memory[32768+FP_FPS/2],16\'o{s["fps"]:o},"program FPS");\n'
        source+=f'        eq(dut.engine.dp.rf.words[7],16\'o{s["pc"]:o},"program PC");\n'
        for r,v in s['registers'].items():source+=f'        eq(dut.engine.dp.rf.words[{r}],16\'o{v:o},"program register");\n'
    source+='''        $display("PASS CP80 hardware program: %0d instructions / %0d checks",scenario,checks);
        $finish;
    end
endmodule
'''
    (out/'tb.v').write_text(source);(out/'plan.json').write_text(json.dumps(p,indent=2)+'\n')
    sources=[str((out/'tb.v').relative_to(ROOT))]+[str((HW/'src'/s).relative_to(ROOT)) for s in CORE]+['rtl/uj11_rom.v']
    paths=[ROOT/s for s in sources]+[template,OUT/'psw-sync/image.mem',OUT/'psw-sync/fp80_symbols.vh',
        ROOT/'tools/fp_psw_hardware_cp80.py',Path(__file__),ROOT/'firmware/fp11/FP11.MAC']
    inputs={str(s.relative_to(ROOT)):hashlib.sha256(s.read_bytes()).hexdigest() for s in paths}
    cmd=['verilator','--binary','--timing','-Wno-WIDTH','-j','4','--top-module','tb_fp_psw_cp80','--Mdir',str(out/'obj'),'-I'+str(OUT/'psw-sync')]+sources
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:
        r=subprocess.run([str(out/'obj/Vtb_fp_psw_cp80')],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text());r.check_returncode()
    for n,h in inputs.items():assert hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==h,n
    (out/'result.json').write_text(json.dumps(dict(passed=True,instructions=len(p['steps']),inputs=inputs),indent=2)+'\n')


if __name__=='__main__':run()
