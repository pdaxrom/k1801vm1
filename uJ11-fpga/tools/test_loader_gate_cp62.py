#!/usr/bin/env python3
"""HALT helper boundaries with real cold boot, guest upload, and SPI FRAM."""
import hashlib
import json
import struct
import subprocess
from board_common import ROOT
from build_vector_loader_cp62 import adapt, OUT
from build_loader_cp62 import build as check_helper
from run_service_cp59 import Program
from test_halt_boot_cp62 import fixtures


def main():
    core,board=adapt();check_helper();fixtures()
    init=(OUT/'halt_boot_cases.vh').read_text().split('scenario=1; fresh();\n',1)[1].split('boot();',1)[0]
    blob=(ROOT/'firmware/cp62/loader.bin').read_bytes();helper=struct.unpack('<228H',blob)
    # operation, address, count/value, USER source, failure, return value, writes
    cases=[(0,4,0,0,0,0o312,0),(3,0,0,0,0,128,0),
        (1,65534,0x789a,0,0,0x789a,1),(1,0o776,7,0,1,None,0),
        (1,65535,7,0,1,None,0),(5,65520,8,0,0,(0xa5a5*8)&65535,0),
        (6,65520,8,0,0,0,8),(7,65520,8,0o30000,0,None,8),
        (7,65534,1,0o157776,0,None,1),(8,65534,1,0,1,None,0),
        (6,65534,2,0,1,None,0),(6,0o776,1,0,1,None,0),
        (6,0o20000,0,0,1,None,0),(6,0o20000,9,0,1,None,0),
        (7,0o20000,2,0o157776,1,None,0),(7,0o20000,1,0o30001,1,None,0),
        (9,0o20000,1,0,1,None,0)]
    lines=[]
    for n,(op,addr,value,source,failed,result,nwrite) in enumerate(cases,1):
        p=Program(0o4000).mov(6,65535).mov(4,0o125061).mov(5,0o10000).mov(1,0o1000)
        left=len(helper)
        while left:
            count=min(64,left);p.mov(2,count).emit(0);left-=count
        p.mov(5,0o20000).mov(1,0o1000).mov(2,0).emit(0o277)
        pc=p.pc;p.emit(0,1)
        lines += [f'scenario={n};fresh();',init,'boot();']
        for i,w in enumerate(p.words):lines.append(f"put({p.base+2*i},16'h{w:04x});")
        for i,w in enumerate(helper):lines += [f"put({0o10000+2*i},16'h{w:04x});",f"gold({0o1000+2*i},16'h{w:04x});"]
        for i,w in enumerate([op,addr,value,source]):lines.append(f"put({0o20000+2*i},16'h{w:04x});")
        lines += ['for(i=0;i<65536;i=i+1)original_lower[i]=fram.memory[i];','finish_case();',
            f'eq(dut.cpu.engine.dp.rf.words[2],{failed},"helper status");',
            'eq(dut.cpu.engine.dp.rf.words[6],65535,"odd guest SP");','eq(dut.cpu.psw,15,"guest NZVC");',
            f"gold(16'o100,{pc+2});gold(16'o102,15);"]
        if result is not None:lines.append(f'eq(dut.cpu.engine.dp.rf.words[0],{result},"helper result");')
        for i in range(nwrite):
            if op==1:word=value
            elif op==6:word=0
            else:word=0xa5a5
            lines.append(f"gold({addr+2*i},16'h{word:04x});")
        lines += ['compare_memory();',f'$display("PASS CP62 helper {n}: op{op} clocks%0d",service_clocks);']
    (OUT/'helper_cases.vh').write_text('\n'.join(lines)+'\n')
    tb=OUT/'tb_helper.v';tb.write_text((ROOT/'tb/tb_halt_boot_cp62.v').read_text().replace('halt_boot_cases.vh','helper_cases.vh').replace('500000','900000'))
    records=[]
    for vendor in (False,True):
        mode='vendor' if vendor else 'portable'
        sources=[str(tb.relative_to(ROOT))]+core+board+['reference/lsi11/spi_fram_model.v']
        defines=[]
        if vendor:
            defines=['-DUJ11_VENDOR_ROM'];sources+=['build/cp62-boot/uj11_m0_ebr.v']+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR','ODDRXE')]
        else:sources+=['rtl/uj11_rom.v','tb/models/ODDRXE.v']
        with (OUT/f'helper-{mode}-build.log').open('w') as log:
            subprocess.run(['iverilog','-g2012','-I'+str(OUT),'-s','tb_halt_boot_cp62','-o',str(OUT/f'helper-{mode}.vvp')]+defines+sources,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (OUT/f'helper-{mode}.log').open('w') as log:
            run=subprocess.run(['vvp',str(OUT/f'helper-{mode}.vvp')],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        text=(OUT/f'helper-{mode}.log').read_text();print(text,flush=True);run.check_returncode()
        assert text.count('PASS CP62 helper ')==len(cases)
        paths=sources+['tools/test_loader_gate_cp62.py','build/cp62-boot/helper_cases.vh','firmware/cp62/loader.bin']
        records.append(dict(mode=mode,passed=True,cases=len(cases),files={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}))
    (OUT/'helper-tests.json').write_text(json.dumps(records,indent=2)+'\n')


if __name__=='__main__':main()
