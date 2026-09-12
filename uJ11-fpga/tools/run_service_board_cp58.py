#!/usr/bin/env python3
"""Exercise loaded service firmware on the full native bus and SPI FRAM."""
import hashlib
import json
import re
import subprocess
from board_common import ROOT
from build_service_cp58 import adapt, OUT
from run_service_cp58 import Program,cases


def fixture():
    cases()
    source=(OUT/'service_cp58_cases.vh').read_text()
    blocks=re.split(r'(?=scenario=\d+;)',source)
    source='\n'.join(b for b in blocks if re.match(r'scenario=(1|4|5|33|34|36|37|38|42|43|82);',b))
    source=re.sub(r"memory\[(16'h[0-9a-f]+|[01])\]=([^;]+);",r'put(2*(\1),\2);',source)
    source=re.sub(r"memory\[(16'h[0-9a-f]+)\]",r'peek(2*(\1))',source)
    source=source.replace('dut.engine','dut.cpu.engine').replace("expect16(psw,","expect16(dut.cpu.psw,").replace("expect16(fault,","expect16(dut.cpu.fault_code,")
    # The service sees shared logical I/O; physical lower/upper accesses bypass
    # that same CSR, without changing its pins or touching the opposite RAM bank.
    h=Program(0o400).mov(0,0x5b00).store(0,0o166000).mov(5,0o166000).emit(0o21).store(0,0o200)
    h.mov(0,0x2400).emit(0o31,0o44).store(0,0o202).mov(0,0xabcd).emit(0o45).mov(0,0x5678).emit(0o41,0o10)
    p=Program().install(h).mov(0,2).emit(0o42,0xf001,1)
    source+='\nscenario=40;fresh(); put(0,16\'o000137);put(2,16\'o020000);put(17\'o166000,16\'h89ab);\n'
    for i,w in enumerate(p.words):source+=f"put(17'h{p.base+2*i:05x},16'h{w:04x});\n"
    source+='''go();finish_case();
expect16(peek(17'h10080),16'h5b00,"MFUS shared panel CSR");
expect16(peek(17'h10082),16'h89ab,"physical lower read bypass");
expect16(peek(17'h0ec00),16'habcd,"physical lower write");
expect16(peek(17'h1ec00),16'h5678,"physical upper write");
expect16({8'b0,dut.bus.panel_io.pins},16'h0024,"raw access leaves panel unchanged");
'''
    (OUT/'service_cp58_board_cases.vh').write_text(source)


def main():
    core,board=adapt();fixture()
    for vendor in (False,True):
        tag='vendor' if vendor else 'portable'
        cmd=['iverilog','-g2012','-I'+str(OUT),'-s','tb_service_board_cp58','-o',f'build/cp58-service/board-{tag}.vvp']
        sources=['tb/tb_service_board_cp58.v']+core+board+['reference/lsi11/spi_fram_model.v']
        if vendor:
            cmd+=['-DUJ11_VENDOR_ROM'];sources+=['build/cp58-service/uj11_m0_ebr.v']+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR','ODDRXE')]
        else:sources+=['rtl/uj11_rom.v','tb/models/ODDRXE.v']
        subprocess.run(cmd+sources,cwd=ROOT,check=True)
        log=subprocess.run(['vvp',f'build/cp58-service/board-{tag}.vvp'],cwd=ROOT,text=True,capture_output=True)
        (OUT/f'board-{tag}.log').write_text(log.stdout+log.stderr);print(log.stdout);log.check_returncode()
        files=sources+['build/cp58-service/inputs.json','build/cp58-service/m0.mem','build/cp58-service/decode.mem',
            'build/cp58-service/service_cp58_board_cases.vh','tools/run_service_board_cp58.py','tools/run_service_cp58.py','microcode/generated/firmware.mem']
        (OUT/f'board-{tag}-inputs.json').write_text(json.dumps(dict(mode=tag,files={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files}),indent=2)+'\n')

if __name__=='__main__':main()
