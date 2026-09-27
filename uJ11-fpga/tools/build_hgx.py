#!/usr/bin/env python3
"""Build the isolated XM HG driver and SD utilities using DEC MACRO/LINK.

Only a new private disk is writable. The boot blocks produced by COPY/BOOT
are exported separately, so packaging never includes simulator swap writes.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
from board_common import ROOT
from build_sd_image import BASE, BASE_SHA, sha, text_bytes
from rt11_build import Console

SOURCES = ('demos/rt11/hostdisk/HGX.MAC', 'demos/rt11/hostdisk/HGTIME.MAC',
           'demos/rt11/panel/TMDRV.MAC', 'demos/rt11/panel/TMRATE.MAC')


def command(c, text, pattern=rb'\r\n\.'):
    c.send(text+'\r')
    c.expect(re.escape(text.encode())+rb'\r\n')
    reply = c.expect(pattern)
    assert b'?' not in reply and b'Errors detected' not in reply, reply
    return reply


def boot(c, monitor):
    c.expect(rb'RT-11'+monitor.encode()+rb'[^\r\n]*V05\.03')
    for _ in range(3):
        c.expect(rb'\r\n\.')
    command(c, 'SET SL OFF')


def build(out, overrides=None, extra_sources=None, extra_links=()):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    assert sha(BASE.read_bytes()) == BASE_SHA
    disk = out/'build.dsk'
    shutil.copyfile(BASE, disk)
    rt = ROOT.parent/'lsi11/rt11tool'
    sources = {p: (ROOT/p).read_text() for p in SOURCES}
    sources.update(overrides or {})
    sources.update(extra_sources or {})
    for path, text in sources.items():
        target = out/Path(path).name
        target.write_bytes(text.replace('\r\n','\n').replace('\n','\r\n').encode('ascii'))
        subprocess.run([str(rt),'add',str(disk),str(target),target.name],check=True,capture_output=True)
    ini = out/'build.ini'
    ini.write_text('set cpu 11/73\nset cpu 2m\nset clk 50hz\nset hk enable\nset hk0 rk07\n'
                   f'attach hk0 {disk}\nboot hk0\n')
    with (out/'console.log').open('wb') as log:
        c = Console(ini, log)
        try:
            boot(c,'FB')
            for path in sources:
                name = Path(path).stem
                command(c,'R MACRO',rb'\*')
                command(c,f'{name},{name}={name}',rb'\*')
                c.send('\x03'); c.expect(rb'\r\n\.')
            for text in ('LINK/NOBITMAP/EXECUTE:HGX.SYS HGX',
                         'LINK/MAP:HGTIME HGTIME,TMDRV', 'LINK/MAP:TMRATE TMRATE', *extra_links):
                command(c,text)
            command(c,'COPY/BOOT SY:RT11XM.SYS SY:')
            c.send('BOOT SY:\r')
            boot(c,'XM')
            assert b'Booted from DM0:RT11XM' in command(c,'SHOW CONFIGURATION')
        finally:
            c.close()
    files = ['HGX.SYS','HGTIME.SAV','TMRATE.SAV','HGTIME.MAP','TMRATE.MAP']
    files += [Path(p).stem+ext for p in sources for ext in ('.LST','.OBJ')]
    for name in files:
        subprocess.run([str(rt),'extract',str(disk),str(out),name],check=True,capture_output=True)
        if name.endswith('.LST'):
            assert re.search(r'Errors detected:\s+0\b',(out/name).read_text(errors='replace')),name
    # Boot block 0 plus secondary boot blocks 2..5; block 1 is the home block.
    before = BASE.read_bytes(); after = disk.read_bytes()
    assert before[512:1024] == after[512:1024]
    assert before[:3072] != after[:3072]
    (out/'xm-boot.bin').write_bytes(after[:3072])
    record = dict(base_sha256=BASE_SHA, tool='DEC MACRO/LINK and COPY/BOOT in SIMH 11/73',
        sources={p:sha((ROOT/p).read_bytes()) for p in SOURCES},
        assembled_sources={p:sha(text.encode('ascii')) for p,text in sources.items()},
        outputs={name:sha((out/name).read_bytes()) for name in files+['xm-boot.bin']},
        scripts={p:sha((ROOT/p).read_bytes()) for p in ('tools/build_hgx.py','tools/rt11_build.py')},
        fixtures=bool(overrides or extra_sources))
    (out/'manifest.json').write_text(json.dumps(record,indent=2)+'\n')
    assert sha(BASE.read_bytes()) == BASE_SHA
    print('Built HGX.SYS, HGTIME.SAV, TMRATE.SAV and XM boot blocks:', out, flush=True)
    return record


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    build(p.parse_args().out)
