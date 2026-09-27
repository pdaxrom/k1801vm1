#!/usr/bin/env python3
"""Validate the packaged XM disk in SIMH and optionally the complete HC7000 RTL."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
from board_common import ROOT
from build_sd_image import sha
from build_mmu_board import build, CORE, BOARD
from rt11_build import Console

MANUALS = ('README','BASIC','ODT','MEMORY','TIMER','HOST','FILES','BUILD')


def simh(out,image):
    out.mkdir(parents=True,exist_ok=False)
    disk=out/'test.dsk';shutil.copyfile(image,disk)
    ini=out/'test.ini'
    ini.write_text('set cpu 11/73\nset cpu 2m\nset clk 50hz\nset hk enable\nset hk0 rk07\n'
                   f'attach hk0 {disk}\nboot hk0\n')
    steps=[]
    def command(c,text,pattern=rb'\r\n\.',expected=()):
        c.send(text+'\r');c.expect(re.escape(text.encode())+rb'\r\n')
        reply=c.expect(pattern,120)
        assert all(token in reply for token in expected),(text,reply)
        if not expected and pattern==rb'\r\n\.':assert b'?' not in reply,(text,reply)
        steps.append(dict(command=text,response=reply.decode('ascii','backslashreplace')))
        return reply
    with (out/'console.log').open('wb') as log:
        c=Console(ini,log)
        try:
            c.expect(rb'RT-11XM[^\r\n]*V05\.03');c.expect(rb'HC7000 XM KIT READY\r\n');c.expect(rb'\r\n\.')
            command(c,'SHOW CONFIGURATION',expected=[b'Booted from DM0:RT11XM',b'22 bit addressing is on',
                b'PDP 11/73A Processor',b'2048KB of memory',b'50 Cycle System Clock'])
            command(c,'SHOW MEMORY',expected=[b'10000000  MEMTOP'])
            for name in MANUALS:command(c,'TYPE '+name+'.TXT',expected=[('END '+name+'.TXT').encode()])
            command(c,'DIR *.SAV',expected=[b'B81FPU',b'B81FPD',b'HGTIME',b'TMRATE',b'MACRO',b'LINK'])
            for name in ('B81FPU','B81FPD'):
                ready=rb'\r\nREADY\r\n'
                command(c,'RUN '+name,rb'INDIVIDUAL\)\?');command(c,'A',ready)
                command(c,'RUN B81TST',ready,[b'CHECKS= 29  ERRORS= 0',b'CP81 PASS'])
                if name=='B81FPD':command(c,'RUN B81DBL',ready,[b'CP81 DOUBLE PASS'])
                for text,expected in (('PRINT 1/0',b'?DIVISION BY ZERO'),
                        ('PRINT SQR(-1)',b'?NEGATIVE SQUARE ROOT'),('PRINT 1E30*1E30',b'?FLOATING OVERFLOW')):
                    command(c,text,ready,[expected]);command(c,'PRINT 2+2',ready,[b' 4 \r\n'])
                command(c,'BYE')
            c.send('RUN TMRATE\r');c.expect(rb'CFG [0-7]{6}\r\n')
            samples=[c.expect(rb'T [0-7]{6} [0-7]{6}\r\n') for _ in range(2)]
            assert samples[0]!=samples[1]
            c.send('Q');c.expect(rb'\r\n\.')
            steps.append(dict(command='RUN TMRATE / Q',samples=[s.decode() for s in samples]))
            command(c,'COPY README.TXT XMTEST.TXT')
            command(c,'DIFFERENCES/BINARY README.TXT XMTEST.TXT',expected=[b'No differences found'])
            command(c,'DELETE/NOQUERY XMTEST.TXT')
            command(c,'INITIALIZE/NOQUERY VM:')
            command(c,'COPY RT11XM.SYS VM:MMUTST.SYS')
            command(c,'DIFFERENCES/BINARY RT11XM.SYS VM:MMUTST.SYS',expected=[b'No differences found'])
            command(c,'UNPROTECT VM:MMUTST.SYS')
            command(c,'DELETE/NOQUERY VM:MMUTST.SYS')
        finally:c.close()
    record=dict(passed=True,cpu='11/73',memory_kib=2048,clock_hz=50,steps=steps,
        image_sha256=sha(image.read_bytes()),simulator_sha256=sha(Path(shutil.which('pdp11')).read_bytes()),
        hg='Production GPIO driver qualified separately on real HC7000; SIMH has no GPIO')
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    print('PASS packaged XM: SIMH 11/73, manuals, BASIC, timer, SD and VM round trips',flush=True)
    return record


def rtl(out,image):
    out.mkdir(parents=True,exist_ok=False)
    record=build();template=ROOT/'tests/mmu/tb_mmu_boot.v'
    text=template.read_text()
    includes=['tests/mmu/basic_rt11.vh','tests/mmu/sd_kit.vh']
    text=text.replace('    reg clk=0,','\n'.join((ROOT/p).read_text() for p in includes)+'\n    reg clk=0,')
    start=text.index('        phase=1;shell(');end=text.index('        $display("PASS MMU RT11:',start)
    text=text[:start]+'''        phase=1;test_sd_kit();
        phase=2;test_basic("B81FPU",0);
        phase=3;test_basic("B81FPD",1);
        phase=4;shell("SHOW CONFIGURATION");contains("Booted from DM0:RT11XM");
        check(high_writes>27000 && high_reads>27000,"VM accesses extended RAM");
        check(mmu_fetches>1000 && dma_words>1000 && concurrent_fetches>100,"MMU and concurrent disk IO");
'''+text[end:]
    text=text.replace('PASS MMU RT11:','PASS MMU SD KIT:').replace('clocks>600000000','clocks>1200000000')
    tb=out/'tb.v';tb.write_text(text)
    inventory=CORE+BOARD+[str(tb.relative_to(ROOT)),'tests/models/async_sram_model.v','tests/models/spi_sd_model.v']
    paths=inventory+includes+['tests/mmu/tb_mmu_boot.v','tools/test_sd_mmu.py']
    hashes={p:sha((ROOT/p).read_bytes()) for p in paths}
    with (out/'build.log').open('w') as log:
        subprocess.run(['verilator','--binary','--timing','-Wno-WIDTH','-Wno-TIMESCALEMOD',
            '--top-module','tb_mmu_boot','-j','4','--Mdir',str(out/'obj')]+inventory,
            cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:
        result=subprocess.run([str(out/'obj/Vtb_mmu_boot'),'+MONITOR=xm',f'+SD_IMAGE={image}',
            f'+UART_LOG={out}/uart.txt'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-3000:]);result.check_returncode()
    assert 'PASS MMU SD KIT:' in (out/'simulation.log').read_text()
    assert all(sha((ROOT/p).read_bytes())==h for p,h in hashes.items())
    result=dict(passed=True,hardware=record,files=hashes,image_sha256=sha(image.read_bytes()))
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--image',type=Path,required=True)
    p.add_argument('--rtl',action='store_true')
    a=p.parse_args();a.out=a.out.resolve();a.image=a.image.resolve()
    digest=sha(a.image.read_bytes());result=dict(simh=simh(a.out/'simh',a.image))
    if a.rtl:result['rtl']=rtl(a.out/'rtl',a.image)
    assert sha(a.image.read_bytes())==digest
    result.update(passed=True,image_sha256=digest)
    (a.out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
