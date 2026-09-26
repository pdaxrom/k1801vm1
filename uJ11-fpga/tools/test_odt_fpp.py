#!/usr/bin/env python3
"""Execute FP debugger UART commands on the CPU, including all 4096 FP words."""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
from board_common import ROOT
from build_hardware import adapt
from pdp11_program import Program
from build_software import sha


def decode(op, fps=None):
    # Independent DEC opcode matrix; deliberately does not import ODT tables.
    fixed={0o170000:'CFCC',0o170001:'SETF',0o170002:'SETI',0o170011:'SETD',0o170012:'SETL'}
    if op in fixed:return fixed[op],False
    spec=op&63;reg=spec&7;mode=spec>>3;family=op&0o177400;single=op&0o177700
    is_float=False;direction=None
    if single in (0o170100,0o170200,0o170300):
        name={0o170100:'LDFPS',0o170200:'STFPS',0o170300:'STST'}[single]
    elif single in (0o170400,0o170500,0o170600,0o170700):
        name={0o170400:'CLR',0o170500:'TST',0o170600:'ABS',0o170700:'NEG'}[single]+('f' if fps is None else 'D' if fps&128 else 'F');is_float=True
    elif family>=0o171000:
        is_float=family not in (0o175000,0o175400,0o176400,0o177000)
        direction='store' if family in (0o174000,0o175000,0o175400,0o176000) else 'load'
        fd='D' if fps is not None and fps&128 else 'F';il='L' if fps is not None and fps&64 else 'I'
        if family<=0o174400:
            name={0o171000:'MUL',0o171400:'MOD',0o172000:'ADD',0o172400:'LD',0o173000:'SUB',0o173400:'CMP',0o174000:'ST',0o174400:'DIV'}[family]+('f' if fps is None else fd)
        else:
            name={0o175000:'STEXP',0o175400:'STCfi' if fps is None else 'STC'+fd+il,
                  0o176000:'STCff' if fps is None else 'STC'+fd+('F' if fd=='D' else 'D'),
                  0o176400:'LDEXP',0o177000:'LDCif' if fps is None else 'LDC'+il+fd,
                  0o177400:'LDCff' if fps is None else 'LDC'+('F' if fd=='D' else 'D')+fd}[family]
    else:return f'.WORD {op:06o}',False
    if is_float and mode==0 and reg>=6:return f'.WORD {op:06o}',False
    extension=mode>=6 or mode in (2,3) and reg==7
    if mode==0:operand=('AC' if is_float else 'R')+str(reg)
    elif mode==1:operand=f'(R{reg})'
    elif mode in (2,3):operand=('@' if mode==3 else '')+('#123456' if reg==7 else f'(R{reg})+')
    elif mode in (4,5):operand=('@' if mode==5 else '')+f'-(R{reg})'
    else:operand=('@' if mode==7 else '')+(f'{(0o30004+0o123456)&65535:06o}' if reg==7 else f'123456(R{reg})')
    ac=f'AC{(op>>6)&3}'
    tail=(ac+','+operand if direction=='store' else operand+','+ac) if direction else operand
    return name+' '+tail,extension


def run(out):
    out.mkdir(parents=True,exist_ok=True)
    od=ROOT/'build/software/odt';fp=ROOT/'build/fpp/software'
    ore=json.loads((od/'result.json').read_text());fre=json.loads((fp/'result.json').read_text());o=ore['symbols'];f=fre['symbols']
    lines=[]
    def put(a,v,bank=1):lines.append(f"memory[{(bank*65536+a)//2}]=16'o{v:o};")
    def blob(a,data):
        for i in range(0,len(data),2):put(a+i,int.from_bytes(data[i:i+2],'little'))
    blob(0o10000,(od/'payload.bin').read_bytes());blob(f['INIT'],(fp/'image.bin').read_bytes())
    for i,w in enumerate((f['INIT'],fre['format']['words'],0,0xc107)):put(0o7020+2*i,w)
    put(0o10,f['ENTER']);put(0o12,0o340)
    boot=Program(0o2000).mov(0,0o20000).store(0,0o100).mov(0,0o341).store(0,0o102)
    boot.mov(0,o['ENTER']).store(0,0o110).mov(0,0o340).store(0,0o112).mov(0,7).emit(0o42)
    for r,v in enumerate([0x5555,0x1234,0x102,0x2468,0o125061,0o30000,65535]):boot.mov(r,v)
    boot.emit(0o10)
    for i,v in enumerate(boot.words):put(0o2000+i*2,v)
    put(0,0o2000);put(2,0o340);put(0o20000,0o777,0)
    put(f['FPS'],0o300);put(f['FEC'],0o14);put(f['FEA'],0o123456)
    for i in range(24):put(f['ACS']+i*2,0o100+i)
    lines+=['repeat(4)@(negedge clk);reset=0;wait(dut.engine.debug_enabled);',
            '@(negedge clk);halt_button=1;@(negedge clk);halt_button=0;wait(prompts==1);','command("F");']
    for i in range(6):lines.append(f'contains("AC{i}='+ ' '.join(f'{0o100+i*4+j:06o}' for j in range(4))+'");')
    lines+=['contains("FPS=000300 MODE=D,L");contains("FEC=000014");contains("FEA=123456");',
            'command("F 11");contains("?SYNTAX");command("F 0 1");contains("?SYNTAX");',
            'command("X");command("F 8");contains("FEA=A72E");command("O");']
    # Unknown context: every encoding, no operand dereferences, exact lengths.
    for op in range(0o170000,0o200000):
        text,ext=decode(op)
        put(0o30000,op,0);put(0o30002,0o123456,0)
        lines+=[f'command("D 30000");contains("  {text}\\r\\n");',f'eq(memory[{(65536+o["DNEXT"])//2}],16\'o{0o30002+2*ext:o},"FP length");']
    # Live FPS names, every family, all 8 modes, CPU R6/7 versus AC6/7.
    lines+=['command("R 7 30000");']
    for fps in (0,0o100,0o200,0o300):
        put(f['FPS'],fps)
        for base in list(range(0o170400,0o171000,0o100))+list(range(0o171000,0o200000,0o400)):
            for spec in (0,5,6,7,0o10,0o22,0o27,0o37,0o44,0o55,0o62,0o67,0o72,0o77):
                op=base+spec+(0o200 if base>=0o171000 else 0);text,ext=decode(op,fps)
                put(0o30000,op,0)
                lines+=[f'command("D 30000");contains("  {text}\\r\\n");']
    # Faulty/off/incompatible debugger metadata falls back to generic decode.
    put(0o30000,0o172205,0)
    for address,bad in ((0o60004,0),(0o60006,1),(0o60010,0),(0o60012,2),
                        (0o60014,0o177560),(0o60014,0o60023),(0o60016,0o177562),(0o60020,0o177776),(0o10,0o60000)):
        original=int.from_bytes((fp/'image.bin').read_bytes()[address-f['INIT']:address-f['INIT']+2],'little') if address>=f['INIT'] else f['ENTER']
        put(address,bad)
        lines+=['command("F");contains("?FP11 DEBUG STATE UNAVAILABLE");command("D 30000");contains("ADDf AC5,AC2");']
        put(address,original)
    lines+=['fp_fault=1;command("F");contains("?FP11 DEBUG STATE UNAVAILABLE");fp_fault=0;',
            'command("F 6");contains("FPS=000300 MODE=D,L");',
            ]
    # No implicit memory dereference even for @#I/O; D only reads extensions.
    put(0o157776,0o172427,0)
    lines+=['command("D 157776");contains("EXTENSION UNAVAILABLE");']
    for i in range(24):lines.append(f'eq(memory[{(65536+f["ACS"]+i*2)//2}],16\'o{0o100+i:o},"read-only AC");')
    for a,v in ((f['FEC'],0o14),(f['FEA'],0o123456),(o['REGS'],0x5555),(o['REGS']+16,0o341)):
        lines.append(f'eq(memory[{(65536+a)//2}],16\'o{v:o},"read-only state");')
    lines.append('eq(16\'(io_bad),0,"debugger never probes extra CSR");')
    (out/'odt_cases.vh').write_text('\n'.join(lines)+'\n')
    core,_=adapt();tb=(ROOT/'tests/tb_odt.v').read_text()
    tb=tb.replace('cycles>10000000','cycles>2000000000').replace('reg clk=0','reg fp_fault=0;\n    reg clk=0')
    tb=tb.replace('wire error=','wire error=(fp_fault && bank && address==16\'o60014) || ')
    tb=tb.replace('uart={uart,wdata[7:0]};','') # bounded per-command text avoids quadratic accumulated UART string
    (out/'tb.v').write_text(tb)
    sources=[str((out/'tb.v').relative_to(ROOT))]+core+['rtl/uj11_rom.v']
    cmd=['verilator','--binary','--timing','--top-module','tb_odt','-j','4','--Mdir',str(out/'obj'),'-I'+str(out)]+sources
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:p=subprocess.run([str(out/'obj/Vtb_odt'),f'+UART_LOG={out}/uart.txt'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    text=(out/'simulation.log').read_text();print(text[-2500:]);p.check_returncode()
    count=re.search(r'PASS CP64 monitor: (\d+) checks (\d+) commands (\d+) clocks',text);assert count
    files=[Path(__file__),ROOT/'tests/tb_odt.v',out/'tb.v',out/'odt_cases.vh',od/'result.json',fp/'result.json']+[ROOT/p for p in core]
    record=dict(passed=True,checks=int(count[1]),commands=int(count[2]),clocks=int(count[3]),opcodes=4096,live_contexts=1008,
                files={str(p.relative_to(ROOT)):sha(p) for p in files})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,default=ROOT/'build/test-odt-fpp');run(p.parse_args().out.resolve())
