#!/usr/bin/env python3
"""Run software reboot trampoline on the actual core, with relocated modules.

Uses generated production ROM inputs from make hardware. No FPGA source edits.
The separately uploaded USER utility uses the same resident copy ABI as UJMOD.
"""
import json
import subprocess
from board_common import ROOT, CORE
from build_software import reboot, native, sha, OUT
from module_image import decode, placed_image, entry


def run():
    out=ROOT/'build/modules-v4/reboot';out.mkdir(parents=True,exist_ok=True)
    rec=reboot();blob,sy,_,_=native(OUT/'reboot/REBOOT.MAC')
    tramp=blob[0o1000:sy['IMEND']]
    lines=[]
    def put(a,v,bank=1):lines.append(f"memory[{(bank*65536+a)//2}]=16'h{v:04x};")
    def load(a,data,bank=1):
        for i in range(0,len(data),2):put(a+i,int.from_bytes(data[i:i+2],'little'),bank)
    def eq(a,v,why,bank=1):lines.append(f'eq(memory[{(bank*65536+a)//2}],16\'h{v:04x},"{why}");')
    load(0o1000,tramp);put(0,0o1000);put(2,0o340)
    modules=[]
    for name,path,base in [('ODT',OUT/'odt',0o24000),('SDBOOT',OUT/'sdboot',0o6000),
                           ('FP11',ROOT/'build/fpp/software',0o10000)]:
        data=(path/(name+'.BIN')).read_bytes();h=decode(data);placed=placed_image(data,base)
        symbol=json.loads((path/'result.json').read_text())['symbols']
        delta=base-h['base'];modules.append((name,base,placed,symbol,delta))
        load(base,placed);load(0o7000+8*(len(modules)-1),entry(base,placed))
        for address in range(base+len(placed),base+h['memory_bytes'],2):put(address,0xa5a5)
    lines+=['repeat(4)@(negedge clk);reset=0;wait(!dut.engine.service_mode);@(negedge clk);',
            'eq(dut.engine.dp.rf.words[7],16\'o4000,"software bootstrap selects USER 4000");',
            'eq(dut.engine.dp.rf.words[6],16\'o1000,"software bootstrap installs HALT stack");',
            'eq({14\'b0,dut.engine.service_ready},3,"software init enables ODT/FPP");',
            'eq({15\'b0,dut.engine.debug_enabled},1,"software init enables debugger");']
    for i,(name,base,placed,symbol,delta) in enumerate(modules):
        eq(0o7000+8*i+6,0xc107,name+' cold init at relocated address')
        for address in ('ENTER','FPS','RADIX'):
            if address in symbol:
                if address=='ENTER':eq(0o110 if name=='ODT' else 0o10,symbol[address]+delta,name+' relocated vector')
                else:eq(symbol[address]+delta,8 if address=='RADIX' else 0,name+' initialized BSS')
    sd=(ROOT/'build/hardware/sdbase.bin').read_bytes()
    for i in range(0,len(sd),2):eq(0o4000+i,int.from_bytes(sd[i:i+2],'little'),'exact USER bootstrap',0)
    # A failed restore must never execute USER code or a module initializer.
    lines += ['reset=1;repeat(4)@(negedge clk);for(integer j=0;j<65536;j++)memory[j]=0;',
              'drop_write=1;']
    load(0o1000,tramp);put(0,0o1000);put(2,0o340)
    lines += [f"reset=0;wait(dut.engine.dp.rf.words[7]==16'o{sy['FAILED']:o});repeat(200)@(negedge clk);",
              'eq({15\'b0,dut.engine.service_mode},1,"restore failure stays in HALT");']
    eq(0o100,0,'failed restore never publishes USER entry')
    (out/'odt_cases.vh').write_text('\n'.join(lines)+'\n')
    tb=(ROOT/'tests/tb_odt.v').read_text()
    tb=tb.replace("(drop_write && !bank && address==16'o31000)","(drop_write && bank && address==16'o4000)")
    tb=tb.replace('cycles>10000000','cycles>100000000')
    (out/'tb.v').write_text(tb)
    sources=[str((out/'tb.v').relative_to(ROOT))]+CORE+['rtl/uj11_rom.v']
    cmd=['verilator','--binary','--timing','--top-module','tb_odt','-j','4','--Mdir',str(out/'obj'),'-I'+str(out)]+sources
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:r=subprocess.run([str(out/'obj/Vtb_odt'),f'+UART_LOG={out}/uart.txt'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-1500:]);r.check_returncode()
    record=dict(passed=True,reboot=rec,files={str(p.relative_to(ROOT)):sha(p) for p in
        [ROOT/p for p in sources]+[out/'odt_cases.vh',out/'simulation.log',ROOT/'tools/test_reboot.py']})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')


if __name__=='__main__':run()
