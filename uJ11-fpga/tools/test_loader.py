#!/usr/bin/env python3
"""Execute native UJMOD range/directory/readback routines on the actual core."""
import json
import re
import struct
import subprocess
from pathlib import Path
from board_common import ROOT
from build_hardware import adapt
from build_software import loader, OUT as SW, sha
from pdp11_program import Program
from module_image import pack


def run():
    out=ROOT/'build/test-loader';out.mkdir(parents=True,exist_ok=True)
    adapt()
    record=loader();asm=ROOT/record['assembly']
    listing=(asm/'UJMOD.LST').read_text(errors='replace')
    sym={n:int(v,8)+0o1000 for n,v in re.findall(r'\b([A-Z][A-Z0-9]{0,5})\s+([0-7]{6})R',listing)}
    binary=(SW/'loader/UJMOD.SAV').read_bytes()
    resident=ROOT/'build/hardware/resid.bin';helper=ROOT/'build/hardware/helper.bin'
    assert binary[sym['HELPER']:sym['HELPER']+len(helper.read_bytes())]==helper.read_bytes()
    lines=[];count=0;mark=0o120000
    def put(a,v,bank=0):lines.append(f"memory[{(bank*65536+a)//2}]=16'h{v&65535:04x};")
    def blob(a,data,bank=0):
        for i in range(0,len(data),2):put(a+i,int.from_bytes(data[i:i+2],'little'),bank)
    def case(name,args=None,patch=None,upper=None,drop=False):
        nonlocal count
        count+=1
        lines.append(f'@(negedge clk);reset=1;drop_write={int(drop)};repeat(4)@(negedge clk);')
        lines.append('for(integer j=0;j<65536;j=j+1)memory[j]=0;')
        blob(0,binary);blob(0o200,resident.read_bytes(),1);blob(0o1000,helper.read_bytes(),1)
        for a,v in ((0,0o2000),(2,0o340),(4,0o312),(6,0o340),(0o170,0o200),(0o172,0o340)):put(a,v,1)
        boot=Program(0o2000).mov(0,0o100000).store(0,0o100).mov(0,0).store(0,0o102).mov(6,0o140000).emit(0o10)
        for i,w in enumerate(boot.words):put(boot.base+2*i,w,1)
        call=Program(0o100000)
        for r,v in (args or {}).items():call.mov(r,v)
        call.emit(0o4737,sym[name],0o106704).store(0,mark+2).store(2,mark+4).store(4,mark+6).mov(0,1).store(0,mark).emit(0o777)
        for i,w in enumerate(call.words):put(call.base+2*i,w)
        for a,v in (patch or {}).items():put(a,v)
        for a,v in (upper or {}).items():put(a,v,1)
        lines.append(f'reset=0;wait(memory[{mark//2}]==1);@(negedge clk);')
    def eq(a,v,why,bank=0):lines.append(f'eq(memory[{(bank*65536+a)//2}],16\'h{v:04x},"case{count} {why}");')
    def carry(value):lines.append(f'eq(memory[{(mark+6)//2}]&1,16\'d{value},"case{count} carry status");')
    for base,length,valid in ((0o6000,256,True),(0o10000,26624,True),(0o5776,1,False),(0o6001,1,False),
                              (0o6776,2,False),(0o7000,1,False),(0o7776,2,False),(0o10000,0,False),
                              (0o10000,32768,False),(0o157776,2,False),(0o160000,1,False)):
        case('RANGE',{0:base,1:length});carry(int(not valid))
        if valid:eq(mark+4,base+2*length,'exclusive end')
    image=pack(0o10000,struct.pack('<5H',0o12700,0,0o207,0,0))
    for change in (None,(0,0),(1,0),(2,1),(3,2),(4,0o10001),(4,0o7000),(5,0),(5,32768),(8,1),'sum','padding','short','extra'):
        h=list(struct.unpack('<256H',image[:512]));flen=len(image)//512
        if isinstance(change,tuple):h[change[0]]=change[1];h[7]=0;h[7]=-sum(h[:16])&65535
        if change=='sum':h[7]^=1
        if change=='padding':h[255]=1
        if change=='short':flen-=1
        if change=='extra':flen+=1
        patch={sym['HEADER']+2*i:w for i,w in enumerate(h)};patch[sym['FLEN']]=flen
        case('VALID',patch=patch);carry(int(change is not None))
    entries=[[],[(0,0o10000,5,0xc101)],[(2,0o10000,0,0xc103)],[(0,0o10002,1,0xc103)],
             [(0,0o40000,0,0xc103)],[(0,0o10000,5,0xc103),(1,0o10000,5,0xc103)],
             [(i,0o40000+i*0o1000,1,0xc103) for i in range(8)],
             [(i,0o40000+i*0o1000 if i!=7 else 0o10000,1,0xc103) for i in range(8)],
             [(0,0o10002,1,0xc102)]]
    for records,want in zip(entries,(0o7000,0o7000,0o7020,None,None,None,None,0o7070,0o7000)):
        upper={}
        for slot,base,length,status in records:
            for i,w in enumerate((base,length,0,status)):upper[0o7000+8*slot+2*i]=w
        case('SELECT',patch={sym['HEADER']+8:0o10000,sym['HEADER']+10:5,sym['NEWEND']:0o10012},upper=upper)
        carry(int(want is None))
        if want is not None:eq(sym['SLOT'],want,'selected slot')
    for addr,drop,success in ((0o10000,False,True),(0o10000,True,False),(0o10001,False,False),(0o20,False,False)):
        case('PUTCHK',{0:0x4321,5:addr},upper={0o10000:0x1234},drop=drop);carry(int(not success))
        eq(0o10000,0x4321 if success else 0x1234,'native helper write and readback',1)
    (out/'odt_cases.vh').write_text('\n'.join(lines)+'\n')
    tb=(ROOT/'tests/tb_odt.v').read_text().replace('(drop_write && !bank && address==16\'o31000)',"(drop_write && bank && address==16'o10000)")
    tb=tb.replace('$display("PASS CP64 monitor: %0d checks %0d commands %0d clocks",checks,prompts,cycles);',f'$display("PASS CP67 native loader: {count} cases, %0d checks, %0d clocks",checks,cycles);')
    (out/'tb.v').write_text(tb)
    core,_=adapt();sources=[str((out/'tb.v').relative_to(ROOT))]+core+['rtl/uj11_rom.v']
    cmd=['verilator','--binary','--timing','--top-module','tb_odt','-j','4','--Mdir',str(out/'obj'),'-I'+str(out)]+sources
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:r=subprocess.run([str(out/'obj/Vtb_odt'),f'+UART_LOG={out}/uart.txt'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    text=(out/'simulation.log').read_text();print(text[-2200:]);r.check_returncode()
    m=re.search(r'PASS CP67 native loader: (\d+) cases, (\d+) checks, (\d+) clocks',text);assert m
    paths=[ROOT/p for p in sources]+[out/'odt_cases.vh',SW/'loader/UJMOD.SAV',ROOT/'tests/tb_odt.v',resident,helper,Path(__file__)]
    (out/'result.json').write_text(json.dumps(dict(passed=True,cases=int(m[1]),checks=int(m[2]),clocks=int(m[3]),
        files={str(p.relative_to(ROOT)):sha(p) for p in paths},loader=record),indent=2)+'\n')


if __name__=='__main__':run()
