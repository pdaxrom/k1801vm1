#!/usr/bin/env python3
"""Cold ROM + native USER upload + physical button stop/step on SPI FRAM."""
import hashlib
import json
import subprocess
from board_common import ROOT
from build_debug_cp63 import adapt, OUT
from run_service_cp59 import Program
from test_debug_cp63 import monitor_program, CONTEXT, COMMAND


def main():
    core,board=adapt()
    handler,poll=monitor_program()
    installer=Program(0o3000).mov(0,handler.base).store(0,0o110).mov(0,0o340).store(0,0o112)
    installer.emit(0o005037,COMMAND).mov(0,5).emit(0o42,0o10)
    guest=Program(0o4000)
    # Release boot overlay through the real SD-control handshake and USER read0.
    guest.emit(0o012737,7,0o177502,0o005737,0)
    for source,payload in ((0o20000,handler),(0o22000,installer)):
        guest.mov(4,0o125061).mov(5,source).mov(1,payload.base)
        left=len(payload.words)
        while left:
            n=min(64,left);guest.mov(2,n).emit(0);left-=n
    guest.mov(1,installer.base).mov(2,0).emit(0)
    for r,v in enumerate([0x5555,0x1234,0x102,0x2468,0o125061,0o30000,65535]):guest.mov(r,v)
    guest.emit(0o010115) # MOV R1,(R5)
    inc_pc=guest.pc;guest.emit(0o005201,0o776) # INC R1; BR back to INC
    lines=['scenario=1;fresh();']
    resident=(ROOT/'firmware/cp62/resident.bin').read_bytes()
    firmware=[int(w,16) for w in (OUT/'firmware.mem').read_text().splitlines()]
    for i in range(0,len(resident),2):lines.append(f"gold({0o200+i},16'h{int.from_bytes(resident[i:i+2],'little'):04x});")
    for i,w in enumerate(firmware[:213]):lines.append(f"gold({0o4000+2*i},16'h{w:04x});")
    for a,w in ((4,0o312),(6,0o340),(0o170,0o200),(0o172,0o340),(0o100,0o4000),(0o102,0),(0o160,0o422),(0o776,0o402)):
        lines.append(f'gold({a},{w});')
    lines.append('boot();')
    for addr,p in ((guest.base,guest),(0o20000,handler),(0o22000,installer)):
        for i,w in enumerate(p.words):lines.append(f"put({addr+2*i},16'h{w:04x});")
    lines += [f"wait(dut.bus_request && dut.writing && !dut.bank && dut.address==16'o30000);",
              'button_n=0;repeat(300)@(negedge clk);button_n=1;',
              f"wait(dut.bus_request && dut.bank && dut.opcode_fetch && dut.address=={poll});@(negedge clk);"]
    lines += ['eq(peek(16\'o30000),16\'h1234,"memory write before stop committed");',
              'eq(peek(65536+16\'o16010),16\'o125061,"signature R4 preserved");',
              'eq(peek(65536+16\'o16014),65535,"odd SP preserved");',
              'eq(dut.cpu.engine.debug_context,1,"physical button entered debugger");',
              'saved_r1=peek(65536+16\'o16002);saved_pc=peek(65536+16\'o100);',
              f'put(65536+{COMMAND},2);wait(!dut.cpu.engine.service_mode);',
              f'wait(dut.bus_request && dut.bank && dut.opcode_fetch && dut.address=={poll});@(negedge clk);',
              f'if(saved_pc=={inc_pc})begin eq(peek(65536+{CONTEXT+2}),saved_r1+1,"SPI step INC");eq(peek(65536+64),{inc_pc+2},"SPI next PC");end',
              f'else begin eq(saved_pc,{inc_pc+2},"stopped on loop BR");eq(peek(65536+{CONTEXT+2}),saved_r1,"SPI step BR");eq(peek(65536+64),{inc_pc},"SPI branch PC");end',
              f'put(65536+{COMMAND},1);wait(!dut.cpu.engine.service_mode);repeat(1000)@(negedge clk);',
              'eq(dut.cpu.engine.service_mode,0,"SPI continue");',
              'button_n=0;wait(reset);repeat(1000)@(negedge clk);eq(reset,1,"long button holds reset");',
              'eq(dut.cpu.engine.service_ready,0,"hard reset clears readiness");',
              'button_n=1;wait(!reset);wait(!dut.cpu.engine.service_mode);',
              'eq(dut.cpu.engine.debug_enabled,0,"cold boot disables debugger");',
              f'eq(peek(65536+{handler.base}),{handler.words[0]},"ODT slot survives reset");',
              '$display("PASS CP63 board: cold boot, native upload, physical button, SPI STEP/CONTINUE, long reset");$finish;']
    (OUT/'board_cases.vh').write_text('\n'.join(lines)+'\n')
    tb=(ROOT/'tb/tb_halt_boot_cp62.v').read_text()
    tb=tb.replace('reg clk=0,reset=1;','''reg clk=0,power_on=1,button_n=1;
    wire hard_reset,halt_button;
    wire reset=power_on || hard_reset;
    reg [15:0] saved_r1,saved_pc;
    uj11_button #(.SAMPLE_DIVISOR(13),.HOLD_SAMPLES(100)) button(
        .clk(clk),.power_on(power_on),.button_n(button_n),.hard_reset(hard_reset),.halt_pulse(halt_button));''')
    tb=tb.replace('.reset(reset),.uart_rx', '.reset(reset),.halt_button(halt_button),.uart_rx')
    tb=tb.replace('reset=1;in_guest=0;','power_on=1;in_guest=0;')
    tb=tb.replace('reset=0;','power_on=0;')
    tb=tb.replace('            release dut.bus.boot_overlay_active;\n','')
    tb=tb.replace("            force dut.bus.boot_overlay_active=1'b0;\n",'')
    tb=tb.replace('halt_boot_cases.vh','board_cases.vh').replace('clocks>500000','clocks>1500000')
    test=OUT/'tb_debug_board.v';test.write_text(tb)
    records=[]
    for vendor in (False,True):
        mode='vendor' if vendor else 'portable'
        sources=[str(test.relative_to(ROOT))]+core+board+['reference/lsi11/spi_fram_model.v']
        defs=[]
        if vendor:
            defs=['-DUJ11_VENDOR_ROM'];sources+=['build/cp63-debug/uj11_m0_ebr.v']+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR','ODDRXE')]
        else:sources+=['rtl/uj11_rom.v','tb/models/ODDRXE.v']
        binary=OUT/f'board-{mode}.vvp'
        with (OUT/f'board-{mode}-build.log').open('w') as log:
            subprocess.run(['iverilog','-g2012','-I'+str(OUT),'-s','tb_halt_boot_cp62','-o',str(binary)]+defs+sources,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (OUT/f'board-{mode}.log').open('w') as log:
            r=subprocess.run(['vvp',str(binary)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        text=(OUT/f'board-{mode}.log').read_text();print(text,flush=True);r.check_returncode()
        assert 'PASS CP63 board:' in text
        paths=sources+[str((OUT/'board_cases.vh').relative_to(ROOT)),'tools/test_debug_board_cp63.py','tools/test_debug_cp63.py']
        records.append(dict(mode=mode,passed=True,files={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}))
    (OUT/'board-tests.json').write_text(json.dumps(records,indent=2)+'\n')


if __name__=='__main__':main()
