#!/usr/bin/env python3
"""Execute assembled production ODT on CP63 CPU, with directed bus faults."""
import argparse
import json
import hashlib
from pathlib import Path
import subprocess
from board_common import ROOT
from build_debug_cp63 import adapt
from run_service_cp59 import Program


def run(odt,execute=True,out=None,mode='sync'):
    assert mode in ('sync','logic','vendor'),mode
    out=out or ROOT/'build/cp64-test';out.mkdir(exist_ok=True)
    (out/'result.json').unlink(missing_ok=True)
    result=json.loads((odt/'result.json').read_text());sym=result['symbols']
    for p,h in result['sources'].items():assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h,p
    payload=(odt/'payload.bin').read_bytes()
    lines=[]
    def put(addr,words,bank=0):
        for i,w in enumerate(words):lines.append(f"memory[{(65536*bank+addr)//2+i}]=16'o{w:o};")
    put(0o10000,[int.from_bytes(payload[i:i+2],'little') for i in range(0,len(payload),2)],1)
    boot=Program(0o2000).mov(0,0o20000).store(0,0o100).mov(0,0o341).store(0,0o102)
    boot.mov(0,sym['ENTER']).store(0,0o110).mov(0,0o340).store(0,0o112).mov(0,5).emit(0o42)
    for r,v in enumerate([0x5555,0x1234,0x102,0x2468,0o125061,0o30000,65535]):boot.mov(r,v)
    boot.emit(0o10);put(0o2000,boot.words,1);put(0,[0o2000,0o340],1)
    put(0o20000,[0o005201,0o005202,0o777])
    lines+=['repeat(4)@(negedge clk);reset=0;wait(dut.engine.debug_enabled);',
        '@(negedge clk);halt_button=1;@(negedge clk);halt_button=0;wait(prompts==1);',
        'contains("R4=125061");contains("R6=177777");contains("INC R1");',
        'command("R 0 177777");contains("OLD=052525 -> 177777");contains("R0=177777");',
        'command("R 0 200000");contains("?SYNTAX");',
        'command("R 0");contains("R0=177777");',
        'command("W 30000 123456");contains("000000 -> 123456");eq(memory[6144],16\'o123456,"word write");',
        'command("V 30001 377");eq(memory[6144],16\'o177456,"odd byte write");',
        'command("B 30001");contains("30001: 000377");',
        'command("M 30001");contains("?SYNTAX");',
        'command("M 177562");contains("?SYNTAX");',
        'command("D 157776 2");contains("EXTENSION UNAVAILABLE");',
        'inject=1;command("M 31000");contains("?MEMORY");inject=0;',
        'command("R 4");contains("R4=125061");',
        'command("S");contains("R1=011065");contains("R7=020002");',
        'command("S");contains("R2=000403");contains("R7=020004");',
        'command("X");contains("R0=FFFF");',
        'command("R 2 80AF");contains("R2=80AF");',
        'command("R 2 12:4");contains("?SYNTAX");',
        'command("O");contains("R2=100257");',
        'command("D 20000 3");contains("005201  INC R1");contains("000777  BR 020004");',
        'command("R 1 7 EXTRA");contains("?SYNTAX");',
        'command("R 1");contains("R1=011065");',
        "eq(16'(io_bad),16'(0),\"automatic reads never touch other CSR\");",
        'baseprompt=prompts;segment="";send({"C",8\'d13});wait(!dut.engine.service_mode);',
        'eq(dut.engine.dp.rf.words[4],16\'o125061,"CONTINUE R4");eq(dut.engine.dp.rf.words[6],65535,"CONTINUE SP");'
    ]
    assignments=len(lines)
    put(0o157776,[0o012700])
    # Every operand mode for every register, including both PC extension slots.
    corpus=[];pc=0o40000
    def operand(mode,reg,extension_pc):
        if mode==0:return f"R{reg}",[]
        if mode==1:return f"(R{reg})",[]
        if mode in (2,3):
            prefix='@' if mode==3 else ''
            if reg==7:return prefix+'#123456',[0o123456]
            return prefix+f"(R{reg})+",[]
        if mode in (4,5):return ('@' if mode==5 else '')+f"-(R{reg})",[]
        prefix='@' if mode==7 else ''
        if reg==7:return prefix+f"{(extension_pc+2-4)&65535:06o}",[0o177774]
        return prefix+f"177774(R{reg})",[0o177774]
    for destination in (False,True):
        for address_mode in range(8):
            for reg in range(8):
                spec=address_mode*8+reg
                op,extra=operand(address_mode,reg,pc+2)
                opcode=0o010000+((2<<6)|spec if destination else (spec<<6)|2)
                put(pc,[opcode]+extra)
                assembly=f"MOV R2,{op}" if destination else f"MOV {op},R2"
                corpus.append(f'command("D {pc:o}");contains("{assembly}");')
                pc+=2+len(extra)*2
    for opcode,assembly in [(0o000600, 'BR'),(0o077301,'SOB R3'),(0o070327,'MUL #123456,R3'),
                            (0o004327,'JSR R3,#123456'),(0o074327,'XOR R3,#123456'),
                            (0o075025,'FMUL R5'),(0o237,'SPL 000007'),(0o177777,'.WORD 177777')]:
        put(pc,[opcode,0o123456]);corpus.append(f'command("D {pc:o}");contains("{assembly}");');pc+=4
    for words,assembly in [([0o600],f"BR {pc+2-256:06o}"),
                           ([0o16767,0o177774,0o12],f"MOV {pc+2:06o},{pc+18:06o}")]:
        put(pc,words);corpus.append(f'command("D {pc:o}");contains("{assembly}");');pc+=2*len(words)
    init=lines[assignments:];lines=init+lines[:assignments]
    end=next(i for i,l in enumerate(lines) if l.startswith('eq(16'))
    corpus += [
        'fail_write=1;command("W 31000 123");contains("?MEMORY");fail_write=0;eq(memory[6400],0,"failed write unchanged");',
        'drop_write=1;command("W 31000 123");contains("?MEMORY");drop_write=0;eq(memory[6400],0,"readback failure");',
        'command("K 20 21 22 23");contains("FUNCTION KEYS SET");',
        'command("K 20 21 22 22");contains("?SYNTAX");',
        'command("G 160000");contains("?SYNTAX");',
        'command("W 30000 7 EXTRA");contains("?SYNTAX");eq(memory[6144],16\'o177456,"trailing junk no write");',
        'command("R 0 1");contains("R0=000001");',
        'baseprompt=prompts;send("R 0 12");@(negedge clk);halt_button=1;@(negedge clk);halt_button=0;wait(prompts>baseprompt);',
        'command("R 0");contains("R0=000001");',
        'baseprompt=prompts;segment="";txready=0;send({"W 30000 7",{80{8\'d32}},8\'d13});txready=1;wait(prompts>baseprompt);contains("?INPUT OVERFLOW");eq(memory[6144],16\'o177456,"overflow cannot execute truncated write");',
        'baseprompt=prompts;segment="";rxerr=1;send("W");rxerr=0;send({" 30000 7",8\'d13});wait(prompts>baseprompt);contains("?INPUT OVERFLOW");eq(memory[6144],16\'o177456,"RX error cannot execute a write");',
        'command("R 0");contains("R0=000001");',
        'command("R 7 32000");command("S");contains(" WAIT");',
        'oldfetches=guest_fetches;command("S");eq(16\'(guest_fetches),16\'(oldfetches),"repeated WAIT step has no fetch");',
        'command("R 7 20004");'
    ]
    # Put WAIT in USER memory (before reset), independently of monitor state.
    lines.insert(0,"memory[6656]=1;")
    end=next(i for i,l in enumerate(lines) if l.startswith('eq(16'))
    lines[end:end]=corpus
    (out/'odt_cases.vh').write_text('\n'.join(lines)+'\n')
    if not execute:return
    core,_=adapt()
    sources=['tb/tb_odt_cp64.v']+core+['rtl/uj11_rom.v']
    defines=[]
    if mode=='vendor':
        defines=['-DUJ11_VENDOR_ROM'];sources.remove('rtl/uj11_rom.v')
        # Icarus 12 crashes on a packed byte select inside string concatenation.
        # Keep the scoreboard and cases intact; only format the appended byte.
        tb=(ROOT/'tb/tb_odt_cp64.v').read_text()
        before='uart={uart,wdata[7:0]};segment={segment,wdata[7:0]};'
        after='uart=$sformatf("%s%c",uart,wdata[7:0]);segment=$sformatf("%s%c",segment,wdata[7:0]);'
        assert tb.count(before)==1
        (out/'tb.v').write_text(tb.replace(before,after))
        sources[0]=str((out/'tb.v').relative_to(ROOT))
        sources+=['build/cp63-debug/uj11_m0_ebr.v']+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')]
        # The unmodified Lattice models use procedural assign/deassign, which
        # Verilator does not support. Use the CP63 Icarus simulation path.
        cmd=['iverilog','-g2012','-s','tb_odt_cp64','-o',str(out/'test.vvp'),'-I'+str(out),'-Ptb_odt_cp64.ROM_DECODE=1']+defines+sources
        simulator=['vvp',str(out/'test.vvp')]
    else:
        cmd=['verilator','--binary','--timing','--top-module','tb_odt_cp64','-j','4','--Mdir',str(out/'obj'),'-I'+str(out),'-GROM_DECODE='+('0' if mode=='logic' else '1')]+sources
        simulator=[str(out/'obj/Vtb_odt_cp64')]
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    simulator.append(f'+UART_LOG={out}/uart.txt')
    with (out/'simulation.log').open('w') as log:r=subprocess.run(simulator,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    log=(out/'simulation.log').read_text();print(log[-4000:]);r.check_returncode()
    assert 'PASS CP64 monitor:' in log
    files=[str((out/'odt_cases.vh').relative_to(ROOT)),str((odt/'result.json').relative_to(ROOT)),str((odt/'payload.bin').relative_to(ROOT)), 'tools/test_odt_cp64.py','tb/tb_odt_cp64.v']+sources
    (out/'result.json').write_text(json.dumps(dict(passed=True,mode=mode,compile_command=cmd,simulation_command=simulator,files={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files}),indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--odt',type=Path,required=True);p.add_argument('--mode',choices=['logic','sync','vendor'],default='sync');p.add_argument('--out',type=Path)
    a=p.parse_args();run(a.odt.resolve(),out=a.out.resolve() if a.out else None,mode=a.mode)
