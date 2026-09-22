#!/usr/bin/env python3
"""Run HALT breakpoint regressions on the production CPU and resident."""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
from board_common import ROOT
from build_hardware import adapt
from pdp11_program import Program


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def test(odt,out):
    out.mkdir(parents=True,exist_ok=True)
    adapt()
    (out/'result.json').unlink(missing_ok=True)
    record=json.loads((odt/'result.json').read_text());sym=record['symbols']
    for p,h in record['sources'].items(): assert sha(ROOT/p)==h,p
    resident=ROOT/'build/hardware/resid.bin'
    lines=[]
    def put(addr,words,bank=0):
        for i,w in enumerate(words):lines.append(f"memory[{(bank*65536+addr)//2+i}]=16'o{w:o};")
    def blob(addr,data,bank): put(addr,[int.from_bytes(data[i:i+2],'little') for i in range(0,len(data),2)],bank)
    blob(0o10000,(odt/'payload.bin').read_bytes(),1);blob(0o200,resident.read_bytes(),1)
    put(0o320,[0o177,0o177640],1);put(0o164,[sym['ENTER']],1)
    put(0o170,[0o200,0o340],1);put(4,[0o312,0o340],1)
    boot=Program(0o2000).mov(0,0o20000).store(0,0o100).mov(0,1).store(0,0o102)
    boot.mov(0,sym['ENTER']).store(0,0o110).mov(0,0o340).store(0,0o112).mov(0,5).emit(0o42)
    for r,v in enumerate([0x5555,0x1234,0x102,0x2468,0o125061,0o30000,65535]):boot.mov(r,v)
    boot.emit(0o10);put(0o2000,boot.words,1);put(0,[0o2000,0o340],1)
    put(0o20000,[0o5201,0o5202,0o5203,0o777])
    (out/'odt_cases.vh').write_text('\n'.join(lines)+'\n`include "breakpoint_cases.vh"\n')
    tb=(ROOT/'tests/tb_odt.v').read_text()
    tb=tb.replace(".irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(15'b0),.irq_ack(),.waiting()", ".irq_valid(irq_req),.irq_priority(3'd4),.irq_vector(irq_vec),.irq_ack(irq_ack),.waiting(guest_wait)")
    names=('REGS','META','BPTAB','BPTEMP','BPPOST','BPRUN','BPERR')
    extra=''.join(f'localparam integer O_{n}={sym[n]};\n' for n in names)
    extra+='''reg irq_req=0,drop_vector=0; reg [14:0] irq_vec=15'o40; wire irq_ack,guest_wait;
    integer irq_count=0;
    always @(posedge clk)if(irq_ack)begin irq_req<=0;irq_count<=irq_count+1;end
    function [15:0] upper(input integer a);return memory[(65536+a)/2];endfunction
'''
    tb=tb.replace('    always #5 clk=~clk;',extra+'    always #5 clk=~clk;')
    tb=tb.replace('else if(writing && !error &&', "else if(writing && !error && !(drop_vector && bank && address==16'o170 && wdata==16'o200) &&")
    tb=tb.replace('cycles>10000000','cycles>30000000')
    (out/'tb.v').write_text(tb)
    core,_=adapt();sources=[str((out/'tb.v').relative_to(ROOT))]+core+['rtl/uj11_rom.v']
    cmd=['verilator','--binary','--timing','--top-module','tb_odt','-j','4','--Mdir',str(out/'obj'),'-I'+str(out),'-I'+str(ROOT/'tests')]+sources
    with (out/'build.log').open('w') as log: subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    sim=[str(out/'obj/Vtb_odt'),f'+UART_LOG={out}/uart.txt']
    with (out/'simulation.log').open('w') as log:r=subprocess.run(sim,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    log=(out/'simulation.log').read_text();print(log[-3000:]);r.check_returncode()
    counts=re.search(r'PASS CP66: (\d+) checks (\d+) clocks',log);assert counts
    paths=sources+[str((out/'odt_cases.vh').relative_to(ROOT))]+[str((odt/n).relative_to(ROOT)) for n in ('result.json','payload.bin')]
    paths+=['tools/test_breakpoints.py','tests/breakpoint_cases.vh','tests/tb_odt.v','build/hardware/resid.bin']
    (out/'result.json').write_text(json.dumps(dict(passed=True,checks=int(counts[1]),clocks=int(counts[2]),compile_command=cmd,simulation_command=sim,files={p:sha(ROOT/p) for p in paths}),indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--odt',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();test(a.odt.resolve(),a.out.resolve())
