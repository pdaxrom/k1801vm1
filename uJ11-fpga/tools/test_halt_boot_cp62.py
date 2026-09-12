#!/usr/bin/env python3
"""Directed CP62 cold boot and bounded vector-call tests, both EBR models."""
import hashlib
import json
import subprocess
from board_common import ROOT
from build_vector_loader_cp62 import adapt, OUT
from run_service_cp59 import Program


def fixtures():
    payload=(ROOT/'firmware/cp62/resident.bin').read_bytes()
    sd=bytes(b for w in (ROOT/'build/cp62-boot/firmware.mem').read_text().splitlines()[:213]
             for b in int(w,16).to_bytes(2,'little'))
    def initial():
        result=[]
        for offset in range(0,len(payload),2):
            result.append(f"gold(16'o{0o200+offset:06o},16'h{int.from_bytes(payload[offset:offset+2],'little'):04x});")
        for offset in range(0,len(sd),2):
            result.append(f"gold(16'o{0o4000+offset:06o},16'h{int.from_bytes(sd[offset:offset+2],'little'):04x});")
        for address,value in [(4,0o312),(6,0o340),(0o170,0o200),(0o172,0o340),(0o100,0o4000),(0o102,0),
                              (0o160,0o422),(0o776,0o402)]:
            result.append(f"gold(16'o{address:06o},16'o{value:06o});")
        return result
    lines=[]
    cases=[(0o20000,0o10000,1,0,0),(0o20000,0o10000,64,0,0),
           (0o157776,0o157776,1,0,0),(0o20000,0o1000,1,0,0),
           (0o20000,0o10000,0,0,0),(0o20000,0o10000,65,1,0),
           (0o20001,0o10000,1,1,0),(0o20000,0o10001,1,1,0),
           (0o20000,0o776,1,1,0),(0o157776,0o10000,2,1,0),
           (0o20000,0o157776,2,1,0),(0o177776,0o10000,1,1,0),
           (0o20000,0o177776,1,1,0),(0o20000,0o10000,1,1,2),
           (0o20000,0o10000,1,1,3)]
    for number,(source,dest,count,status,inject) in enumerate(cases,1):
        p=Program(0o4000).mov(6,0xffff).mov(4,0o125061).mov(5,source).mov(1,dest).mov(2,count)
        # Set all four NZVC through SCC; no T/IPL change. HALT itself keeps them.
        p.emit(0o277);pc=p.pc;p.emit(0,1)
        lines += [f'scenario={number}; fresh();']+initial()
        lines.append('boot();')
        if count==0:
            for offset,w in enumerate([0o5000,0o10]):
                lines.append(f"put(65536+{dest+2*offset},16'o{w:o});gold({dest+2*offset},16'o{w:o});")
        for i,w in enumerate(p.words):lines.append(f"put(16'o{p.base+2*i:06o},16'o{w:06o});")
        lines += ['for(i=0;i<65536;i=i+1)original_lower[i]=fram.memory[i];',
                  f'inject={inject};','finish_case();',f'eq(dut.cpu.engine.dp.rf.words[0],{status},"copy status");',
                  'eq(dut.cpu.psw,15,"START restores NZVC");','eq(dut.cpu.engine.dp.rf.words[6],65535,"odd SP untouched");',
                  'eq(dut.cpu.engine.dp.rf.words[4],16\'o125061,"signature register");',
                  f"gold(16'o100,16'o{pc+2:06o});gold(16'o102,15);"]
        if status==0:
            lines += [f'for(i=0;i<{2*count};i=i+1)expected_upper[{dest}+i]=original_lower[{source}+i];',
                      f'eq(dut.cpu.engine.dp.rf.words[5],{source+2*count},"MFUS count");',
                      f'eq(dut.cpu.engine.dp.rf.words[1],{dest+2*count},"destination count");',
                      'eq(dut.cpu.engine.dp.rf.words[2],0,"remaining count");']
        if inject==3:lines.append('eq(injected,1,"bus fault reached");')
        lines += ['compare_memory();',f'$display("PASS CP62 case {number}: boot%0d clocks, service%0d clocks",boot_clocks,service_clocks);']
    # Write-protected cold FRAM cannot install executable code or reach USER.
    lines += ['scenario=16;fresh();inject=1;@(negedge clk);reset=0;repeat(10000)@(negedge clk);',
              'eq(dut.cpu.engine.service_mode,1,"bad installation stays HALT");',
              'eq(dut.cpu.engine.service_ready,0,"bad installation no ready");',
              "wait(dut.bus_request && dut.opcode_fetch && dut.address==16'o10732);",
              'for(i=65536;i<131072;i=i+1)if(fram.memory[i]!==8\'ha5)$fatal(1,"WP changed RAM");checks=checks+1;']
    lines += ['scenario=17;fresh();inject=4;@(negedge clk);reset=0;',
              "wait(dut.bus_request && dut.opcode_fetch && dut.address==16'o320);repeat(500)@(negedge clk);",
              'eq(dut.cpu.engine.service_mode,1,"bad USER copy stays HALT");',
              'for(i=2048;i<2474;i=i+1)if(fram.memory[i]!==8\'ha5)$fatal(1,"USER WP changed RAM");checks=checks+1;']
    (OUT/'halt_boot_cases.vh').write_text('\n'.join(lines)+'\n')


def main():
    core,board=adapt();fixtures();results=[]
    for vendor in (False,True):
        mode='vendor' if vendor else 'portable'
        sources=['tb/tb_halt_boot_cp62.v']+core+board+['reference/lsi11/spi_fram_model.v']
        defines=[]
        if vendor:
            defines=['-DUJ11_VENDOR_ROM']
            sources+=['build/cp62-boot/uj11_m0_ebr.v']+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR','ODDRXE')]
        else:sources+=['rtl/uj11_rom.v','tb/models/ODDRXE.v']
        binary=OUT/f'boot-{mode}.vvp'
        with (OUT/f'boot-{mode}-build.log').open('w') as log:
            subprocess.run(['iverilog','-g2012','-I'+str(OUT),'-s','tb_halt_boot_cp62','-o',str(binary)]+defines+sources,
                           cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (OUT/f'boot-{mode}.log').open('w') as log:
            run=subprocess.run(['vvp',str(binary)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        output=(OUT/f'boot-{mode}.log').read_text();print(output,flush=True);run.check_returncode()
        assert 'PASS CP62: 17 cold HALT / copy cases' in output
        paths=sources+['tools/test_halt_boot_cp62.py','build/cp62-boot/halt_boot_cases.vh',
                      'build/cp62-boot/m0.mem','build/cp62-boot/decode.mem','build/cp62-boot/firmware.mem','build/cp62-boot/inputs.json']
        results.append(dict(mode=mode,passed=True,cases=17,files={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}))
    (OUT/'boot-tests.json').write_text(json.dumps(results,indent=2)+'\n')


if __name__=='__main__':main()
