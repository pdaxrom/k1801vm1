#!/usr/bin/env python3
"""CP58 executable firmware tests and portable/vendor ROMs."""
import hashlib
import json
import subprocess
from board_common import ROOT
from build_service_cp58 import adapt, OUT

class Program:
    def __init__(self,base=0o20000): self.base=base;self.words=[]
    @property
    def pc(self):return self.base+2*len(self.words)
    def emit(self,*words):self.words.extend(words);return self
    def mov(self,reg,value):return self.emit(0o012700+reg,value)
    def store(self,reg,address):return self.emit(0o010037+(reg<<6),address)
    def load(self,reg,address):return self.emit(0o013700+reg,address)
    def upper(self,address,value):return self.mov(5,address).mov(0,value).emit(0o41)
    def install(self,handler,vector=8):
        self.upper(vector,handler.base).upper(vector+2,0o340)
        for i,w in enumerate(handler.words):self.upper(handler.base+2*i,w)
        return self


def cases():
    lines=[];case_id=0
    def case(p,expect,extra='',patches=None,waits=0):
        nonlocal case_id
        case_id+=1
        lines.extend([f'scenario={case_id}; fresh(); delay_cycles={waits};',extra])
        lines.extend([f"memory[0]=16'o000137; memory[1]=16'h{p.base:04x};"])
        for i,w in enumerate(p.words):lines.append(f"memory[16'h{(p.base//2+i):04x}]=16'h{w:04x};")
        for a,w in (patches or {}).items():lines.append(f"memory[16'h{a//2:04x}]=16'h{w:04x};")
        lines.extend(['go(); finish_case();']+expect)
    def eq(got,value,what):return f'expect16({got},16\'h{value:04x},"{what}");'
    h=Program(0o400).store(0,0o200).store(5,0o212).emit(0o22).store(0,0o202)
    h.emit(0o062700,2,0o32,0o24).store(0,0o204).emit(0o20).store(0,0o206)
    h.mov(5,0o10000).emit(0o21).store(0,0o210).mov(0,0x9876).emit(0o31)
    h.load(5,0o212).load(0,0o200).emit(0o10)
    for delay in (0,1,7):
        p=Program().install(h).mov(6,0xffff).mov(1,0xbeef).mov(0,2).emit(0o42,0o261)
        fp=p.pc;p.emit(0xf001,0o005001,1)
        case(p,[eq("memory[16'h8040]",2,'saved R0'),eq("memory[16'h8041]",fp+2,'CPC before extension update'),
            eq("memory[16'h8042]",0o341,'16-bit CPSW'),eq("memory[16'h8043]",0,'RSEL installed FP'),
            eq("memory[16'h8044]",0x7654,'MFUS guest read'),eq("memory[16'h0800]",0x9876,'MTUS guest write'),
            eq('dut.engine.dp.rf.words[1]',0xbeef,'WCPC skipped CLR'),eq('dut.engine.dp.rf.words[6]',0xffff,'no guest stack'),
            eq('dut.engine.dp.rf.words[0]',2,'restored R0'),eq('psw',0o341,'START flags'),
            eq('dut.engine.service_mode',0,'START guest bank'),eq("memory[16'h7fff]",0xa55a,'odd SP untouched')],
            patches={0o10000:0x7654},waits=delay)
    p=Program()
    for index,(address,value) in enumerate([(0,0x1234),(8,0xabcd),(0o166000,0x1357),(0xfffe,0x2468)]):
        p.upper(address,value).mov(0,0).emit(0o40).store(0,0x3000+2*index)
    p.mov(0,3).emit(0o42,5,0o43,1)
    case(p,[eq("memory[16'h8000]",0x1234,'raw upper zero'),eq("memory[16'hf600]",0x1357,'raw upper I/O backing'),
        eq("memory[16'hffff]",0x2468,'last physical word'),eq('dut.engine.dp.rf.words[0]',3,'RESET keeps ready'),
        eq('dut.engine.service_mode',0,'installer stays guest')]+[eq(f"memory[16'h{0x1800+i:04x}]",v,'installer readback') for i,v in enumerate([0x1234,0xabcd,0x1357,0x2468])])
    for opcode in [0xf001,0xf000,0xffff]+list(range(0o10,0o30))+list(range(0o31,0o40))+[0o44,0o45]:
        p=Program().mov(6,0o4000).mov(0,0x3344);op_pc=p.pc;p.emit(opcode)
        case(p,[eq('dut.engine.dp.rf.words[6]',0o3774,'reserved trap frame'),
            eq("memory[16'h03fe]",op_pc+2,'reserved saved PC'),eq('dut.engine.service_mode',0,'reserved guest bank'),
            eq('dut.engine.service_ready',0,'cold ready clear')],patches={8:0o2000,10:0o340,0o2000:1})
    p=Program().mov(6,0o4000);halt_pc=p.pc;p.emit(0)
    case(p,[eq('dut.engine.dp.rf.words[6]',0o3774,'legacy HALT frame'),eq("memory[16'h03fe]",halt_pc+2,'legacy HALT PC')],
        patches={4:0o2000,0o2000:1})
    h=Program(0o600).store(0,0o200).emit(0o23).store(0,0o202).load(0,0o200).emit(0o13)
    p=Program().install(h,0o170).mov(6,0xffff).mov(0,1).emit(0o42);halt_pc=p.pc;p.emit(0,1)
    case(p,[eq("memory[16'h8041]",halt_pc+2,'ODT CPC'),eq('dut.engine.dp.rf.words[6]',0xffff,'ODT no guest stack'),
        eq('dut.engine.service_mode',0,'START alias')])
    h=Program(0o400).emit(0o10)
    p=Program().install(h).mov(6,0o4000).mov(0,2).emit(0o42);fp=p.pc;p.emit(0xf001,1)
    case(p,[eq('irq_count',1,'one deferred private IRQ'),eq("memory[16'h03fe]",fp+2,'IRQ guest PC'),
        eq('dut.engine.service_mode',0,'IRQ returned bank')],extra='inject_irq=1;',
        patches={0o160000:0o2000,0o160002:0o340,0o2000:1},waits=3)
    h=Program(0o400).mov(0,0o140021).emit(0o37,0o27).store(0,0o204).emit(0o12)
    p=Program().install(h).mov(6,0o4000).mov(0,2).emit(0o42);fp=p.pc;p.emit(0xf001,1)
    case(p,[eq("memory[16'h8042]",0o140021,'WCPS/RCPS full 16 bits'),eq("memory[16'h03ff]",0o140021,'trace saved CPSW'),
        eq("memory[16'h03fe]",fp+2,'trace return PC'),eq('dut.engine.service_mode',0,'trace in guest')],
        patches={0o14:0o2000,0o16:0o340,0o2000:1})
    for op in [0xf001,0]:
        h=Program(0o400).emit(op)
        p=Program().install(h).mov(6,0xffff).mov(0,3).emit(0o42);fp=p.pc;p.emit(0xf001)
        case(p,[eq('stopped',1,'nested entry terminal'),eq("memory[16'h8020]",fp+2,'nested preserves CPC'),
            eq('dut.engine.dp.rf.words[6]',0xffff,'nested no guest stack')])
    h=Program(0o400).load(0,0x1234)
    p=Program().install(Program(0o1000).emit(1),4).install(h).mov(6,0xffff).mov(0,2).emit(0o42);fp=p.pc;p.emit(0xf001)
    case(p,[eq('waiting',1,'SEL004 firmware'),eq('fault',0,'recovered bus fault'),eq('dut.engine.dp.rf.words[7]',0o1002,'SEL004 PC'),eq("memory[16'h8020]",fp+2,'fault CPC')],extra='inject_error=1;')
    # Partial replacement remains disabled; publishing one flag preserves the other.
    h=Program(0o400).emit(1)
    p=Program().install(h).mov(6,0o4000).mov(0,3).emit(0o42).mov(0,1).emit(0o42)
    p.upper(0o400,0o240).emit(0o43).store(0,0o3000);fp=p.pc;p.emit(0xf001)
    case(p,[eq('dut.engine.service_mode',0,'disabled FP uses guest trap'),
        eq('dut.engine.service_ready',1,'ODT ready preserved'),eq("memory[16'h0300]",0o201,'status after partial update'),
        eq("memory[16'h03fe]",fp+2,'partial update trap PC')],patches={8:0o2000,10:0o340,0o2000:1})
    # Odd installer word faults in the guest and cannot alter upper RAM.
    p=Program().mov(6,0o4000).mov(5,0xfffd).mov(0,0x1234);op_pc=p.pc;p.emit(0o41)
    case(p,[eq('dut.engine.service_mode',0,'odd loader remains guest'),eq("memory[16'hfffe]",0xa55a,'odd write did not reach FRAM'),
        eq("memory[16'h03fe]",op_pc+2,'odd loader PC')],patches={4:0o2000,6:0o340,0o2000:1})
    # FIS may execute inside service firmware, using all its normal temporaries.
    h=Program(0o400)
    for a,v in [(0o2000,0x4080),(0o2002,0),(0o2004,0x4100),(0o2006,0)]:h.mov(0,v).store(0,a)
    h.mov(5,0o2000).emit(0o075005,0o10)
    p=Program().install(h).mov(6,0xffff).mov(0,2).emit(0o42);fp=p.pc;p.emit(0xf001,1)
    case(p,[eq("memory[16'h8202]",0x4140,'service FIS 1+2=3 high'),eq("memory[16'h8203]",0,'service FIS low'),
        eq('dut.engine.dp.rf.words[5]',0o2004,'service FIS stack pointer'),eq("memory[16'h8020]",fp+2,'FIS preserves CPC')])
    h=Program(0o400).mov(5,0x2001).mov(0,0xaaaa).emit(0o21)
    p=Program().install(Program(0o1000).emit(1),4).install(h).mov(0,2).emit(0o42,0xf001)
    case(p,[eq('fault',0,'odd MFUS SEL004'),eq('dut.engine.dp.rf.words[7]',0o1002,'odd MFUS handler PC'),eq('dut.engine.dp.rf.words[5]',0x2003,'failed MFUS incremented'),
        eq('dut.engine.dp.rf.words[0]',0xaaaa,'failed MFUS preserves R0')])
    # A failed initial CPC store never launches firmware or touches guest SP.
    h=Program(0o400).emit(1)
    p=Program().install(h).mov(6,0xffff).mov(0,2).emit(0o42);fp=p.pc;p.emit(0xf001)
    case(p,[eq('fault',2,'context write failure terminal'),eq('dut.engine.dp.rf.words[7]',fp+2,'context fault preserves live PC'),
        eq('dut.engine.dp.rf.words[6]',0xffff,'context fault guest SP'),eq("memory[16'h8020]",0xa55a,'failed copy invalid')],
        extra="inject_error=1;error_address=16'h0040;")
    # Both USER accesses fault as HALT operations. Failed reads still increment
    # R5 exactly once, failed writes retain their predecrement; R0 stays intact.
    for opcode in (0o21,0o31):
        for odd in (False,True):
            for delay in (0,1,7):
                target=0x1234+int(odd)
                initial=target+(2 if opcode==0o31 else 0)
                h=Program(0o400).mov(5,initial).mov(0,0xaaaa).emit(opcode)
                p=Program().install(Program(0o1000).emit(1),4).install(h).mov(6,0xffff).mov(0,2).emit(0o42,0o261)
                fp=p.pc;p.emit(0xf001)
                case(p,[eq('fault',0,'USER access recovered'),eq('dut.engine.service_mode',1,'SEL004 bank'),
                    eq('dut.engine.dp.rf.words[7]',0o1002,'SEL004 handler'),
                    eq('dut.engine.dp.rf.words[5]',target+(2 if opcode==0o21 else 0),'fault address delta'),
                    eq('dut.engine.dp.rf.words[0]',0xaaaa,'fault preserves R0'),
                    eq('dut.engine.dp.rf.words[6]',0xffff,'fault avoids guest stack'),
                    eq("memory[16'h8020]",fp+2,'fault frozen CPC'),eq("memory[16'h8021]",0o341,'fault frozen CPSW'),
                    eq("memory[16'h091a]",0xa55a,'failed store untouched'),
                    eq('error_beats',0 if odd else 1,'one failed word transaction')],
                    extra='inject_error=1;error_bank=0;',waits=delay)
    # First opcode and incomplete entry/vector failures remain terminal. A later
    # opcode-fetch error, after one successful opcode, is recoverable through 004.
    for address,second,terminal in [(0o400,0xffff,True),(0o402,0xffff,False),
                                  (0o102,0xffff,True),(0o10,0xffff,True),(0o12,0xffff,True),
                                  (0x1234,4,True),(0x1234,6,True),(0x1234,0o1000,True)]:
        h=Program(0o400).emit(0o240).load(0,0x1234)
        p=Program().install(Program(0o1000).emit(1),4).install(h).mov(6,0xffff).mov(0,2).emit(0o42)
        fp=p.pc;p.emit(0xf001)
        case(p,[eq('stopped',int(terminal),'entry/fault guard'),eq('fault',2 if terminal else 0,'guard status'),
                eq("memory[16'h8020]",fp+2,'guard frozen CPC'),eq('dut.engine.dp.rf.words[6]',0xffff,'guard SP')],
             extra=f"inject_error=1;error_address=16'h{address:04x};error_address2=16'h{second:04x};",waits=3)
    # A successful handler opcode re-arms SEL004 for later errors, preserving
    # the original guest context across both entries.
    h=Program(0o400).load(0,0x1234)
    eh=Program(0o1000).emit(0o005202,0o022702,2,0o001403).load(0,0x1234).emit(0,1)
    p=Program().install(eh,4).install(h).mov(2,0).mov(0,2).emit(0o42);fp=p.pc;p.emit(0xf001)
    case(p,[eq('fault',0,'second independent SEL004'),eq('dut.engine.dp.rf.words[2]',2,'two handler entries'),
            eq("memory[16'h8020]",fp+2,'second fault frozen CPC')],extra='inject_error=1;')
    # START arbitrates before the first guest opcode; STEP (all aliases) does
    # so only after that instruction, and does not invent a T bit.
    for opcode in range(0o10,0o20):
        for traced in (False,True):
            h=Program(0o400).mov(0,0o140001+(0o20 if traced else 0)).emit(0o37,opcode)
            p=Program().install(h).mov(6,0o4000).mov(1,0).mov(0,2).emit(0o42);fp=p.pc;p.emit(0xf001,0o005201,1)
            stepped=opcode>=0o14
            case(p,[eq('irq_count',0 if traced else 1,'IRQ versus T priority'),
                eq('dut.engine.dp.rf.words[1]',int(stepped),'first guest instruction count'),
                eq("memory[16'h03fe]",fp+2+2*int(stepped),'return boundary PC'),
                eq("memory[16'h03ff]",0o140001+(0o20 if traced else 0),'full PSW and unchanged T'),
                eq('dut.engine.service_mode',0,'return bank')],extra='inject_irq=1;',
                patches={0o160000:0o2000,0o160002:0o340,0o14:0o2000,0o16:0o340,0o2000:1},waits=2)
    # STEP without a pending event continues ordinary execution; first guest
    # instruction faults still use the ordinary guest 004 path.
    h=Program(0o400).emit(0o14)
    p=Program().install(h).mov(1,0).mov(0,2).emit(0o42,0xf001,0o005201,0o005201,1)
    case(p,[eq('dut.engine.dp.rf.words[1]',2,'STEP does not force HALT/T'),eq('psw',0o340,'STEP T remains zero')])
    h=Program(0o400).emit(0o17)
    p=Program().install(h).mov(6,0o4000).mov(0,2).emit(0o42);fp=p.pc;p.emit(0xf001,0o013701,0x1235)
    case(p,[eq('fault',0,'first guest odd fault recovered'),eq('dut.engine.service_mode',0,'first guest fault bank'),
            eq("memory[16'h03fe]",fp+6,'first guest fault PC'),eq('irq_count',0,'fault precedes pending IRQ')],
         extra='inject_irq=1;',patches={4:0o2000,6:0o340,0o2000:1})
    # The service fault redirect also covers ordinary byte/word EA paths and
    # raw physical accesses, not only the two VM2 USER instructions.
    for byte in (False,True):
        for write in (False,True):
            target=0x1234+int(byte)
            opcode=(0o010023 if write else 0o012300)+(0o100000 if byte else 0)
            h=Program(0o400).mov(3,target).mov(0,0xaaaa).emit(opcode)
            p=Program().install(Program(0o1000).emit(1),4).install(h).mov(0,2).emit(0o42)
            fp=p.pc;p.emit(0xf001)
            case(p,[eq('fault',0,'ordinary service EA fault'),eq('dut.engine.dp.rf.words[3]',target+(1 if byte else 2),'EA increment once'),
                    eq('dut.engine.dp.rf.words[0]',0xaaaa,'EA source unchanged'),eq("memory[16'h8020]",fp+2,'EA frozen CPC'),
                    eq("memory[16'h891a]",0xa55a,'failed EA write untouched'),eq('error_beats',1,'EA failed transaction')],
                 extra=f"inject_error=1;error_address=16'h{target:04x};",waits=2)
    for opcode,bank in [(0o40,1),(0o41,1),(0o44,0),(0o45,0)]:
        h=Program(0o400).mov(5,0x1234).mov(0,0xaaaa).emit(opcode)
        p=Program().install(Program(0o1000).emit(1),4).install(h).mov(0,2).emit(0o42)
        fp=p.pc;p.emit(0xf001)
        case(p,[eq('fault',0,'raw service access SEL004'),eq('dut.engine.dp.rf.words[5]',0x1234,'raw address unchanged'),
                eq('dut.engine.dp.rf.words[0]',0xaaaa,'raw R0 unchanged'),eq("memory[16'h8020]",fp+2,'raw frozen CPC'),
                eq("memory[16'h891a]",0xa55a,'upper failed store'),eq("memory[16'h091a]",0xa55a,'lower failed store')],
             extra=f'inject_error=1;error_bank={bank};',waits=1)
    lines += ['@(negedge clk); reset=1; repeat(4)@(negedge clk);',eq('dut.engine.service_ready',0,'cold reset ready'),
              eq('dut.engine.service_mode',0,'cold reset bank'),eq("memory[16'h8004]",0o400,'FRAM survives cold reset')]
    (OUT/'service_cp58_cases.vh').write_text('\n'.join(lines)+'\n')
    return case_id


def main():
    core,_=adapt();count=cases();outputs=[]
    for mode in (0,1,'vendor'):
        cmd=['iverilog','-g2012','-I'+str(OUT),'-s','tb_service_cp58','-o',f'build/cp58-service/test-{mode}.vvp','tb/tb_service_cp58.v']+core
        if mode=='vendor':cmd.insert(1,'-DUJ11_VENDOR_ROM');cmd+=['build/cp58-service/uj11_m0_ebr.v','build/vendor/DP8KC.v','build/vendor/GSR.v','build/vendor/PUR.v']
        else:cmd.insert(1,'-Ptb_service_cp58.ROM_DECODE='+str(mode));cmd+=['rtl/uj11_rom.v']
        subprocess.run(cmd,cwd=ROOT,check=True)
        log=subprocess.run(['vvp',f'build/cp58-service/test-{mode}.vvp'],cwd=ROOT,text=True,capture_output=True)
        path=OUT/f'test-{mode}.log';path.write_text(log.stdout+log.stderr);print(log.stdout[-1800:]);log.check_returncode();outputs.append(path)
    report=dict(scenarios_per_mode=count,modes=['logic decode + portable ROM','sync decode + portable ROM','vendor DP8KC'],
        files={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in outputs+[OUT/'service_cp58_cases.vh',ROOT/'tools/run_service_cp58.py',ROOT/'tb/tb_service_cp58.v']},
        profile=json.loads((OUT/'inputs.json').read_text()))
    (OUT/'test-results.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
