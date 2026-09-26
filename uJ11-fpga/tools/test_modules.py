#!/usr/bin/env python3
"""Exercise assembled generic cold initializers on the real CP67 FRAM bus."""
import argparse
import hashlib
import json
import subprocess
from board_common import ROOT
from build_hardware import adapt, OUT, firmware_rom
from build_software import odt, bootstrap, OUT as SOFTWARE
from module_image import checksum


def run(vendor=False,full_window=False):
    core,board=adapt()
    mode=('vendor' if vendor else 'portable')+('-window' if full_window else '')
    testout=OUT/mode
    testout.mkdir(exist_ok=True)
    symbols=json.loads((OUT/'boot-symbols.json').read_text())
    words=[int(w,16) for w in (OUT/'firmware.mem').read_text().split()]
    overrides=[]
    if not full_window:
        for name in ('RLOOPS','RSPINS'):
            index=(1056+symbols[name]-symbols['MSTART'])//2
            overrides.append(dict(symbol=name,index=index,original=words[index],test=1))
            words[index]=1
    (testout/'firmware-test.mem').write_text(''.join(f'{w:04x}\n' for w in words))
    (testout/'test_fw.v').write_text(firmware_rom(words).replace('build/hardware/firmware.mem',str((testout/'firmware-test.mem').relative_to(ROOT))))
    board=[str((testout/'test_fw.v').relative_to(ROOT)) if p.endswith('/uj11_firmware_rom.v') else p for p in board]
    lines=[]
    n=0
    def case():
        nonlocal n
        n+=1;lines.append(f'scenario={n};fresh();')
    def put(a,v):lines.append(f"put({a},16'h{v&65535:04x});")
    def image(base,marker,result=0,fail=False,ready=False):
        w=[]
        if ready:w += [0o12700,1,0o42]
        w += [0o5237,marker]  # INC absolute
        if fail:w += [0o5737,0o7500]
        w += [0o12700,result,0o207] # R0 status; RTS PC
        for i,v in enumerate(w):put(base+2*i,v)
        return len(w),sum(w)&65535
    def entry(slot,base,words,checksum,status=0xc103):
        for i,v in enumerate((base,words,checksum,status)):put(0o7000+8*slot+2*i,v)
    def check(a,v,why):lines.append(f'eq(peek({a}),16\'h{v:04x},"{why}");')
    case();lines.append('boot();');check(0o7006,0,'empty table left empty')
    case()
    for a in range(0o7000,0o7100,2):put(a,65535)
    lines.append('boot();');check(0o7006,65535,'erased table not interpreted')
    case();a=image(0o10000,0o7400);b=image(0o40000,0o7402)
    entry(0,0o10000,*a);entry(7,0o40000,*b);lines.append('boot();')
    check(0o7400,1,'low module called after ROM removal');check(0o7402,1,'last entry called')
    check(0o7006,0xc107,'low module successful');check(0o7076,0xc107,'last module successful')
    lines.append('@(negedge clk);reset=1;repeat(4)@(negedge clk);clocks=0;boot();')
    check(0o7400,2,'mutable state does not invalidate code');check(0o7402,2,'second cold initialization')
    for status in (0,0xc100,0xc102,0xc101,0xc10d,0xc113,0xffff):
        case();a=image(0o10000,0o7400);entry(0,0o10000,*a,status=status);lines.append('boot();')
        check(0o7400,0,'disabled uncommitted or unknown status not executed')
        if status==0xc10d:check(0o7006,0xc101,'disabled stale results cleared')
    for base,length in ((0o5776,5),(0o6776,2),(0o7000,5),(0o7776,5),(0o10001,5),(0o10000,0),(0o10000,0x8000),(0o157776,2),(0o160000,1)):
        case();entry(0,base,length,0);lines.append('boot();');check(0o7006,0xc10b,'bad bounds rejected')
    case();a=image(0o10000,0o7400);entry(0,0o10000,a[0],a[1]^1)
    b=image(0o40000,0o7402);entry(1,0o40000,*b);lines.append('boot();')
    check(0o7400,0,'bad checksum not called');check(0o7006,0xc10b,'bad checksum result');check(0o7402,1,'later good module survives')
    case();a=image(0o10000,0o7400,result=1);entry(0,0o10000,*a);lines.append('boot();')
    check(0o7400,1,'initializer ran');check(0o7006,0xc10b,'initializer error recorded')
    case();a=image(0o10000,0o7400,ready=True);entry(0,0o10000,*a)
    b=image(0o40000,0o7402,fail=True);entry(1,0o40000,*b)
    lines.append('fail_read=1;boot();')
    check(0o7400,1,'first module initialized');check(0o7016,0xc10b,'init bus fault recorded')
    lines.append('eq(dut.cpu.engine.service_ready,1,"failure retains earlier ready state");')
    # The same initializer ABI can prepare/select a replacement USER loader.
    for target,result,good in ((0o6000,0,True),(0o6001,0,False),(0o160000,0,False),(0o6000,1,False)):
        case()
        w=[0o12737,target,0o120,0o12700,result,0o207]
        for i,v in enumerate(w):put(0o10000+2*i,v)
        entry(0,0o10000,len(w),sum(w)&65535)
        if good:lines.append("expected_pc=16'o6000;")
        lines.append('boot();')
        check(0o7006,0xc10b if result else 0xc107,'bootstrap initializer result')
    case();a=image(0o10000,0o7400);entry(0,0o10000,*a)
    lines.append("fork begin wait(!reset);send_byte(8'h1b);end begin boot();end join")
    check(0o7400,0,'UART ESC bypasses initializer');check(0o74,1,'recovery selection recorded')
    od=odt();sd=bootstrap();os=od['symbols']
    def load_module(name,record,slot):
        data=(SOFTWARE/name/'image.bin').read_bytes()
        h=record['format']
        for i in range(0,len(data),2):put(h['base']+i,int.from_bytes(data[i:i+2],'little'))
        entry(slot,h['base'],h['words'],checksum(data))
    case();load_module('odt',od,0);load_module('sdboot',sd,1)
    for address in range(os['IMMEND'],os['MEMEND'],2):put(address,0xa5a5)
    lines.append('boot();')
    check(0o7006,0xc107,'actual ODT initializer success');check(0o7016,0xc107,'actual replaceable SD bootstrap success')
    check(0o110,os['ENTER'],'debug vector installed by module');check(os['RADIX'],8,'octal default restored')
    check(os['BPTAB'],0,'retained breakpoint table discarded');check(os['RCOUNT'],0,'retained UART queue discarded')
    check(os['SHADOW'],0o22,'panel shadow initialized');check(os['KLAST'],65535,'key debounce reset')
    lines.append('eq(dut.cpu.engine.debug_enabled,1,"ODT automatically enabled after cold reset");')
    # Retain code and table, dirty live state, then perform another real reset.
    put(os['BPRUN'],1);put(os['BPPOST'],0o4000);put(os['RCOUNT'],11);put(os['RADIX'],16)
    lines.append('@(negedge clk);reset=1;repeat(4)@(negedge clk);clocks=0;boot();')
    check(os['BPRUN'],0,'old running breakpoint context reset');check(os['BPPOST'],0,'old post-breakpoint context reset')
    check(os['RCOUNT'],0,'live UART state reset');check(os['RADIX'],8,'cold defaults repeated')
    check(0o7006,0xc107,'live mutable state does not break immutable checksum')
    case();load_module('odt',od,0)
    put(os['FONT'],0x1234);lines.append('boot();')
    check(0o7006,0xc10b,'corrupted glyph table rejected')
    lines.append('eq(dut.cpu.engine.debug_enabled,0,"corrupted ODT never enabled");')
    case();put(0o6000,0o777);entry(0,0o6000,1,0o777)
    lines.append("fork begin wait(!reset);send_byte(8'h1b);end begin boot();end join")
    check(0o7006,0xc103,'ESC bypasses checksum-valid infinite-loop module');check(0o74,1,'forced recovery despite valid directory')
    if full_window:
        lines=lines[:next(i for i,l in enumerate(lines) if l.startswith('scenario=2;'))]
        n=1
    (testout/'module_cases.vh').write_text('\n'.join(lines)+'\n')
    sources=['tests/tb_modules.v']+core+board+['tests/models/spi_fram_model.v']
    flags=[]
    if vendor:
        flags=['-DUJ11_VENDOR_ROM']
        sources+=['build/hardware/uj11_m0_ebr.v']+['.cache/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR','ODDRXE')]
    else:sources+=['rtl/uj11_rom.v','tests/models/ODDRXE.v']
    binary=OUT/f'modules-{mode}.vvp'
    cmd=['iverilog','-g2012','-I'+str(testout),'-s','tb_modules','-o',str(binary)]+flags+sources
    sim=['vvp',str(binary)]
    if not vendor:
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','-j','4','--top-module','tb_modules','--Mdir',str(testout/'obj'),'-I'+str(testout)]+sources
        sim=[str(testout/'obj/Vtb_modules')]
    with (OUT/f'{mode}-build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (OUT/f'{mode}.log').open('w') as log:r=subprocess.run(sim,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((OUT/f'{mode}.log').read_text()[-2500:],flush=True);r.check_returncode()
    record=dict(passed=True,cases=n,mode=mode,recovery_window_overrides=overrides,odt=od,bootstrap=sd,
                files={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources+['tools/test_modules.py',str((testout/'firmware-test.mem').relative_to(ROOT)),str((testout/'module_cases.vh').relative_to(ROOT))]})
    (OUT/f'{mode}-result.json').write_text(json.dumps(record,indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--vendor',action='store_true');p.add_argument('--full-window',action='store_true')
    a=p.parse_args();run(a.vendor,a.full_window)
