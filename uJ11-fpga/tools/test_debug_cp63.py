#!/usr/bin/env python3
"""Directed CP63 stop/step contracts with logic, sync and vendor microstores."""
import hashlib
import json
import subprocess
from board_common import ROOT
from build_debug_cp63 import adapt, OUT
from run_service_cp59 import Program

CONTEXT=0o16000
COMMAND=0o16040


def monitor_program():
    p=Program(0o10000)
    for r in range(7):p.store(r,CONTEXT+2*r)
    p.emit(0o43).store(0,CONTEXT+14)
    poll=p.pc;p.load(0,COMMAND).emit(0o1775) # BEQ back three words
    p.emit(0o005037,COMMAND) # CLR command, R0 retains request
    p.emit(0o022700,2) # CMP #2,R0
    branch=len(p.words);p.emit(0)
    def leave(opcode):
        p.emit(0o012737,0o312,4)
        for r in range(7):p.load(r,CONTEXT+2*r)
        p.emit(opcode)
    leave(0o10)
    step=p.pc;leave(0o14)
    p.words[branch]=0o1400+((step-(p.base+2*branch+2))//2)
    return p,poll


def fixtures():
    handler,poll=monitor_program()
    (OUT/'debug_constants.vh').write_text(f"localparam integer MONITOR_POLL={poll}, COMMAND={COMMAND};\n")
    lines=[];case_id=0
    def setup(words,flags=0o341,sp=0xffff,ready=5,delay=0):
        nonlocal case_id
        case_id+=1;lines.extend([f'scenario={case_id};fresh();delay_cycles={delay};'])
        boot=Program(0o2000).mov(0,0o20000).store(0,0o100).mov(0,flags).store(0,0o102)
        boot.mov(0,handler.base).store(0,0o110).mov(0,0o340).store(0,0o112)
        boot.mov(0,ready).emit(0o42)
        for r,v in enumerate([0x5555,0x1234,0x102,0x2468,0o125061,0o30000,sp]):boot.mov(r,v)
        boot.emit(0o10)
        programs=[(True,boot),(True,handler),(False,Program(0o20000).emit(*words,0o777))]
        lines.extend([f"memory[32768]={boot.base};memory[32769]=16'o340;",
                      f'memory[{(65536+COMMAND)//2}]=0;'])
        for bank,p in programs:
            for i,w in enumerate(p.words):lines.append(f"memory[{(65536*bank+p.base)//2+i}]=16'h{w:04x};")
        # Vectors and valid, nonhalting guest handlers.
        for v in (4,8,0o14,0o20,0o100):
            for a,w in ((v,0o3000),(v+2,0o340),(0o3000,0o777)):
                lines.append(f"memory[{a//2}]=16'o{w:o};")
    def eq(expression,value,why):lines.append(f'eq({expression},{value},"{why}");')
    def saved(r):return f'memory[{(65536+CONTEXT+2*r)//2}]'
    cpc=f'memory[{(65536+0o100)//2}]';cpsw=f'memory[{(65536+0o102)//2}]'
    for delay in (0,1,7):
        setup([0o010102,0o060102,0o020102,0o401,0o005001,0o005201],delay=delay)
        lines.append('start_debug();');eq('guest_fetches',0,'initial halt before opcode');eq(cpc,0o20000,'initial CPC')
        eq(saved(4),0o125061,'signature R4 preserved, no loader');eq(saved(6),65535,'odd SP preserved')
        for count,(pc,r1,r2,flags) in enumerate([(0o20002,0x1234,0x1234,0o341),(0o20004,0x1234,0x2468,0o340),
               (0o20006,0x1234,0x2468,0o351),(0o20012,0x1234,0x2468,0o351),
               (0o20014,0x1235,0x2468,0o341)],1):
            lines.append('command(2);');eq(cpc,pc,'STEP CPC');eq(saved(1),r1,'STEP R1');eq(saved(2),r2,'STEP R2')
            eq(cpsw,flags,'STEP NZVC/IPL');eq('guest_fetches',count,'one opcode per step');eq(saved(6),65535,'step does not use guest SP')
        lines.append('command(1);repeat(100)@(negedge clk);');eq('dut.engine.service_mode',0,'CONTINUE runs')
    # WAIT is stopped once; continuing returns to waiting, never to the next opcode.
    setup([1,0o005201],flags=0,sp=0o10000);lines.append('start_debug();command(2);')
    eq(cpc,0o20002,'WAIT CPC');eq('dut.engine.debug_wait',1,'WAIT resume record');eq('guest_fetches',1,'WAIT executed once')
    lines.append('command(2);');eq('guest_fetches',1,'STEP on suspended WAIT does not fetch another instruction')
    lines.append('command(1);repeat(80)@(negedge clk);');eq('waiting',1,'CONTINUE preserves WAIT')
    eq('dut.engine.dp.rf.words[1]',0x1234,'WAIT did not execute INC')
    lines.append('irq_valid=1;wait(irq_ack);wait(request && !bank && address==16\'o3000);')
    eq('guest_fetches',1,'IRQ before next guest opcode')
    # Synchronous BPT, IOT, reserved and operand odd/bus faults stop at handler entry.
    for words,vector,error,pcdelta in [([3],0o14,0,2),([4],0o20,0,2),([0o177777],8,0,2),
          ([0o013701,0o30001],4,0,4),([0o013701,0o30000],4,1,4)]:
        setup(words,sp=0o10000);lines.append(f'start_debug();inject_error={error};command(2);')
        eq(cpc,0o3000,'stop before trap handler');eq(saved(6),0o7774,'trap stack completed');eq('guest_fetches',1,'fault step fetch count')
        eq('memory[2046]',0o20000+pcdelta,'trap saved guest PC');eq('memory[2047]',0o341,'trap saved PSW')
    # Pending IRQ remains pending during stop and step, then enters on CONTINUE.
    setup([0o005201],sp=0o10000,flags=0);lines.append('start_debug();irq_valid=1;command(2);')
    eq('irq_count',0,'STEP bypasses IRQ');eq(saved(1),0x1235,'STEP executed INC')
    lines.append('command(1);wait(irq_count==1);@(negedge clk);');eq('irq_count',1,'CONTINUE admits IRQ')
    # Trace occurrence preserved separately from CPSW: RTT suppresses its own T.
    setup([6,0o005201],sp=0o10000,flags=0)
    lines += [f'memory[{0o10000//2}]={0o20002};memory[{0o10002//2}]=16\'o20;',
              'start_debug();command(2);']
    eq(cpc,0o20002,'RTT restored PC');eq(cpsw,0o20,'RTT restored T');eq('dut.engine.debug_trace',0,'RTT has no immediate trace')
    lines.append('command(1);wait(request && !bank && address==16\'o3000);')
    eq('dut.engine.dp.rf.words[1]',0x1235,'CONTINUE after RTT executes instruction before trace')
    setup([0o005201,0o005202],flags=0o140020,sp=0o10000)
    lines.append('start_debug();command(2);')
    eq('dut.engine.debug_trace',1,'completed trace remains pending');eq(cpsw,0o140020,'full CPSW retained')
    lines.append('command(1);wait(request && !bank && address==16\'o3000);')
    eq('dut.engine.dp.rf.words[2]',0x102,'trace enters before second instruction')
    eq('memory[2047]',0o140020,'trace frame preserves full PSW')
    # Long integer/FIS microcode loops are still a single guest step.
    setup([0o070201]) # MUL R1,R2
    lines.append('start_debug();command(2);')
    eq(saved(2),(0x102*0x1234)>>16,'MUL high');eq(saved(3),(0x102*0x1234)&65535,'MUL low')
    eq('guest_fetches',1,'MUL one opcode');eq(cpc,0o20002,'MUL next PC')
    setup([0o075005]) # FADD (R5): 1 + 2 = 3
    for i,w in enumerate([0x4080,0,0x4100,0]):lines.append(f'memory[{0o30000//2+i}]={w};')
    lines.append('start_debug();command(2);')
    eq('memory[6146]',0x4140,'FIS result high');eq('memory[6147]',0,'FIS result low')
    eq(saved(5),0o30004,'FIS pointer');eq('guest_fetches',1,'FIS one opcode')
    # A guest instruction can enter a HALT firmware service before completing.
    # Its START must not consume the outstanding debugger step request.
    for opcode,vector in ((0xf001,8),(0,0o170)):
        setup([opcode],ready=7)
        for a,w in ((vector,0o3000),(vector+2,0o340),(0o3000,0o005201),(0o3002,0o10)):
            lines.append(f'memory[{(65536+a)//2}]={w};')
        lines.append('start_debug();command(2);')
        eq(cpc,0o20002,'HALT service completes before step stop');eq(saved(1),0x1235,'service executed INC')
        eq('guest_fetches',1,'service firmware is not another guest opcode')
    setup([0o777]);lines.append('start_debug();pulse();repeat(100)@(negedge clk);')
    eq(cpc,0o20000,'button in monitor preserves context');eq('debug_count',1,'no recursive debug entry')
    lines.append('command(1);repeat(100)@(negedge clk);');eq('dut.engine.service_mode',0,'monitor button does not retrigger on CONTINUE')
    # While a private board service is active, coalesce requests and defer entry.
    setup([0o777]);lines+=['@(negedge clk);reset=0;wait(!dut.engine.service_mode);debug_block=1;pulse();',
                          'repeat(100)@(negedge clk);']
    eq('debug_count',0,'private service defers halt');eq('dut.engine.debug_pending',1,'request retained')
    lines+=['debug_block=0;monitor();'];eq(cpc,0o20000,'deferred branch PC')
    # Legacy CP62 configurations cannot enable an uninstalled debugger vector.
    setup([0o777],ready=1);lines+=['@(negedge clk);reset=0;wait(!dut.engine.service_mode);pulse();repeat(100)@(negedge clk);']
    eq('debug_count',0,'legacy configuration ignores button')
    (OUT/'debug_cases.vh').write_text('\n'.join(lines)+'\n')
    return case_id


def main():
    core,board=adapt();count=fixtures();records=[]
    jobs=[(f'button{hold}',['tb/tb_button_cp63.v','boards/hc1200/uj11_button.v','boards/hc1200/uj11_tick.v'],
           [f'-Ptb_button_cp63.HOLD={hold}'],'tb_button_cp63') for hold in (10,100,128)]
    for mode in ('logic','sync','vendor'):
        defs=['-Ptb_debug_cp63.ROM_DECODE='+('0' if mode=='logic' else '1')]
        sources=['tb/tb_debug_cp63.v']+core
        if mode=='vendor':
            defs+=['-DUJ11_VENDOR_ROM'];sources+=['build/cp63-debug/uj11_m0_ebr.v']+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')]
        else:sources+=['rtl/uj11_rom.v']
        jobs.append((mode,sources,defs,'tb_debug_cp63'))
    for name,sources,defs,top in jobs:
        binary=OUT/f'test-{name}.vvp'
        with (OUT/f'test-{name}-build.log').open('w') as log:
            subprocess.run(['iverilog','-g2012','-I'+str(OUT),'-s',top,'-o',str(binary)]+defs+sources,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        run=subprocess.run(['vvp',str(binary)],cwd=ROOT,capture_output=True,text=True)
        output=run.stdout+run.stderr;(OUT/f'test-{name}.log').write_text(output);print(output,flush=True);run.check_returncode()
        assert 'PASS CP63' in output
        records.append(dict(mode=name,passed=True,sources={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources}))
    (OUT/'tests.json').write_text(json.dumps(dict(cases=count,records=records,profile=json.loads((OUT/'inputs.json').read_text())),indent=2)+'\n')


if __name__=='__main__': main()
