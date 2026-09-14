#!/usr/bin/env python3
"""CP72 retained initialization and instruction costs on the CP67 SPI FRAM bus.

Run test_fp_paths_cp72.py first to generate the frozen CP67 hardware and FP image.
No RTL state is forced; modules/table are preloaded external FRAM fixtures.
"""
import argparse
import hashlib
import json
import subprocess
from board_common import ROOT, CORE, BOARD
from build_fp11_cp72 import OUT
from build_modules_cp67 import OUT as HARDWARE, firmware_rom
from build_software_cp67 import odt, bootstrap, OUT as SOFTWARE


def run(vendor=False):
    out=OUT/('board-vendor' if vendor else 'board-sync');out.mkdir(exist_ok=True)
    fp=json.loads((OUT/'software/result.json').read_text());fs=fp['symbols']
    od=odt();sd=bootstrap();os=od['symbols']
    words=[int(w,16) for w in (HARDWARE/'firmware.mem').read_text().split()]
    bs=json.loads((HARDWARE/'boot-symbols.json').read_text());overrides=[]
    for name in ('RLOOPS','RSPINS'):
        index=(1056+bs[name]-bs['MSTART'])//2
        overrides.append(dict(symbol=name,index=index,original=words[index],test=1));words[index]=1
    (out/'firmware-test.mem').write_text(''.join(f'{w:04x}\n' for w in words))
    (out/'test_fw.v').write_text(firmware_rom(words).replace('build/cp67-modules/firmware.mem',str((out/'firmware-test.mem').relative_to(ROOT))))
    lines=[]
    def put(a,v):lines.append(f"put({a},16'h{v&65535:04x});")
    def check(a,v,why):lines.append(f'eq(peek({a}),16\'h{v&65535:04x},"{why}");')
    def module(record,path,slot,corrupt=False):
        h=record['format'];data=path.read_bytes()
        for i in range(0,len(data),2):put(h['base']+i,int.from_bytes(data[i:i+2],'little'))
        for i,v in enumerate((h['base'],h['words'],h['checksum'] ^ int(corrupt),0xc103)):put(0o7000+slot*8+i*2,v)
    for order in range(2):
        lines.append(f'scenario={order+1};fresh();')
        module(od,SOFTWARE/'odt/image.bin',order)
        module(fp,OUT/'software/image.bin',1-order)
        module(sd,SOFTWARE/'sdboot/image.bin',2)
        for record in (fp,od):
            for a in range(record['symbols']['IMMEND'],record['symbols']['MEMEND'],2):put(a,0xa5a5)
        lines.append('boot();')
        for slot in range(3):check(0o7006+8*slot,0xc107,'initializer accepted')
        check(0o10,fs['ENTER'],'FP vector installed');check(0o110,os['ENTER'],'ODT vector installed')
        lines.append('eq(dut.cpu.engine.service_ready,3,"both services ready in either order");eq(dut.cpu.engine.debug_enabled,1,"ODT enabled");')
        for a in range(fs['IMMEND'],fs['MEMEND'],2):check(a,0,'FP BSS cold cleared')
        # Re-enter the real ROM walker without erasing the retained code/table.
        put(fs['FPS'],0xffff);put(fs['ACS']+46,0xbeef);put(os['RADIX'],16)
        lines.append('@(negedge clk);reset=1;repeat(4)@(negedge clk);clocks=0;boot();')
        check(fs['FPS'],0,'second cold reset clears FPS');check(fs['ACS']+46,0,'second reset clears AC5 low word')
        check(os['RADIX'],8,'ODT also reinitialized')
        for slot in range(3):check(0o7006+8*slot,0xc107,'mutable changes do not break checksum')
    # A bad FP image must not disable the working ODT/SDBOOT modules.
    lines.append('scenario=3;fresh();')
    module(od,SOFTWARE/'odt/image.bin',0);module(fp,OUT/'software/image.bin',1,True);module(sd,SOFTWARE/'sdboot/image.bin',2)
    lines.append('boot();');check(0o7016,0xc10b,'bad FP checksum rejected')
    lines.append('eq(dut.cpu.engine.service_ready,1,"ODT survives rejected FP");')
    # A fixed USER program measures each instruction once, avoiding live code
    # changes underneath the FRAM read stream.
    cold_cases=lines.copy()
    from run_service_cp57 import Program
    p=Program(0o20000).mov(6,0o60000)
    for addr,value in ((0o50000,0o40600),(0o50002,0o12345),(0o50004,0o65432),(0o50006,0o23456)):
        p.mov(0,value).store(0,addr)
    p.emit(0o170011,0o172637,0o50000,0o174205)
    addresses=[]
    operations=[0o170000,0o170001,0o170002,0o170011,0o170012,*range(0o170100,0o170110),*range(0o170200,0o170207),0o170300]
    operations += [base+8*mode+2 for base in (0o170100,0o170200,0o170300) for mode in range(1,8)]
    operations=[(op,0) for op in operations]
    operations += [(base+0o200+spec,fps) for fps in (0,0o200) for base in (0o172400,0o174000)
                   for spec in (5,0o12,0o22,0o32,0o42,0o52,0o62,0o72,0o27)]
    operations += [(base+spec,fps) for fps in (0,0o200) for base in (0o170400,0o170500,0o170600,0o170700,0o173600)
                   for spec in (5,0o12,0o22,0o32,0o42,0o52,0o62,0o72,0o27)]
    operations.append((0o170207,0))
    for opcode,fps_value in operations:
        for addr,value in ((0o51776,0o54000),(0o52000,0o54000),(0o54000,0o56000)):
            p.mov(0,value).store(0,addr)
        # Register contents/FPS are controlled without touching CPU state.
        for r in range(6):p.mov(r,0o52000 if r==2 else 0)
        p.emit(0o170100)  # LDFPS R0: no FID, no flags, no mode bits
        if fps_value:p.mov(0,fps_value).emit(0o170100)
        if opcode==0o170207:
            fps_value=0o46000
            p.mov(0,0o46000).emit(0o170100)  # FPS = representable return address
        address=p.pc;p.emit(opcode)
        if (opcode&0o77)==0o27:p.emit(0o40200)
        if ((opcode&0o77)>>3)>=6:p.emit(0o2000)
        addresses.append((address,opcode,fps_value,0o46000 if opcode==0o170207 else p.pc))
    p.emit(0o777)
    prefix=cold_cases
    prefix+=['scenario=4;fresh();', "fram.memory[16'o46000]=8'hff;fram.memory[16'o46001]=8'h01;"]
    for i,w in enumerate(p.words):
        prefix.append(f"fram.memory[{p.base+2*i}]=8'h{w&255:02x};fram.memory[{p.base+2*i+1}]=8'h{w>>8:02x};")
    lines=[];module(fp,OUT/'software/image.bin',0);prefix+=lines
    # The initializer can select a test program through the ordinary ABI.
    lines=[]
    data=[0o12737,0o20000,0o120,0o5000,0o207]
    for i,w in enumerate(data):put(0o6000+2*i,w)
    for i,w in enumerate((0o6000,len(data),sum(data)&65535,0xc103)):put(0o7010+2*i,w)
    prefix+=lines+["expected_pc=16'o20000;boot();"]
    for address,opcode,fps_value,nextpc in addresses:prefix.append(f"measure(16'o{address:o},16'o{opcode:o},16'o{fps_value:o},16'o{nextpc:o});")
    (out/'module_cases.vh').write_text('\n'.join(prefix)+'\n')
    tb=(ROOT/'tb/tb_modules_cp67.v').read_text().replace('tb_modules_cp67','tb_fp11_board_cp72').replace('CP67','CP72')
    tb=tb.replace('    initial begin\n', '''    integer metrics,start_cycle,start_beat,entry_cycles,exit_start,beats=0;
    always @(posedge clk) if(!reset && dut.acknowledge)beats<=beats+1;
    task measure(input [15:0] pc,opcode,fps_value,nextpc);
        begin
            wait(dut.bus_request && !dut.bank && dut.cpu.engine.fetching && dut.address==pc);
            start_cycle=clocks;start_beat=beats;
            wait(dut.bus_request && dut.bank && dut.cpu.engine.fetching && dut.address==FP_ENTER);
            entry_cycles=clocks-start_cycle;
            wait(dut.bus_request && dut.bank && dut.cpu.engine.fetching && dut.address==FP_START);
            exit_start=clocks;
            wait(!dut.cpu.engine.service_mode);@(negedge clk);
            $fwrite(metrics,"%06o,%06o,%0d,%0d,%0d,%0d\\n",opcode,fps_value,clocks-start_cycle,beats-start_beat,entry_cycles,clocks-exit_start);
            eq(dut.cpu.engine.service_ready,2,"FP remains ready after operation");
            eq(dut.cpu.engine.dp.rf.words[7],nextpc,"FP return PC");
        end
    endtask
    initial begin
        metrics=$fopen("'''+str((out/'metrics.csv').relative_to(ROOT))+'''","w");
        $fwrite(metrics,"opcode,initial_fps,core_clocks,memory_beats,entry_to_handler_clocks,start_fetch_to_return_clocks\\n");
''')
    tb=tb.replace('    task measure(', f'    localparam FP_ENTER={fs["ENTER"]},FP_START={fs["FATAL"]-2};\n    task measure(')
    (out/'tb.v').write_text(tb)
    sources=[str((out/'tb.v').relative_to(ROOT))]+[str((HARDWARE/'src'/p).relative_to(ROOT)) for p in CORE+BOARD+['boards/hc1200/uj11_button.v']]
    sources=[str((out/'test_fw.v').relative_to(ROOT)) if p.endswith('/uj11_firmware_rom.v') else p for p in sources]
    sources+=['reference/lsi11/spi_fram_model.v']
    if vendor:
        sources+=['build/cp67-modules/uj11_m0_ebr.v']+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR','ODDRXE')]
        cmd=['iverilog','-g2012','-DUJ11_VENDOR_ROM','-s','tb_fp11_board_cp72','-I'+str(out),'-o',str(out/'sim')]+sources;sim=['vvp',str(out/'sim')]
    else:
        sources+=['rtl/uj11_rom.v','tb/models/ODDRXE.v']
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','-j','4','--top-module','tb_fp11_board_cp72','--Mdir',str(out/'obj'),'-I'+str(out)]+sources;sim=[str(out/'obj/Vtb_fp11_board_cp72')]
    paths=sources+['tools/test_fp_board_cp72.py','tb/tb_modules_cp67.v',str((out/'module_cases.vh').relative_to(ROOT)),str((out/'firmware-test.mem').relative_to(ROOT))]
    inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:r=subprocess.run(sim,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-2500:],flush=True);r.check_returncode()
    for p,h in inputs.items():assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h,p
    (out/'result.json').write_text(json.dumps(dict(passed=True,vendor=vendor,inputs=inputs,fp=fp,odt=od,sdboot=sd,recovery_window_overrides=overrides),indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--vendor',action='store_true');run(p.parse_args().vendor)
