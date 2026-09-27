#!/usr/bin/env python3
"""Exercise HGX's real XM buffer services and HGTIME with explicit wire fixtures.

SIMH has no HC7000 GPIO. Only handshake/byte I/O is replaced; RMON mapping,
HG header/checksum/error handling, .SPFUN and .SDTTM are the production code.
"""
import argparse
import json
import subprocess
from pathlib import Path
from board_common import ROOT
from build_hgx import build, boot, command
from build_sd_image import sha, text_bytes
from rt11_build import Console
from test_hg_time import fixture as fb_fixture


def fixture(mode):
    mapped = (ROOT/'demos/rt11/hostdisk/HGX.MAC').read_text().replace('166000','060000')
    wire = fb_fixture(mode)['demos/rt11/hostdisk/HG.MAC']
    wire = wire[wire.index('HGTXBY:\n'):wire.index('BLKNUM:')]
    mapped = mapped.replace('HGXFER:\n','HGXFER:\n\tCLR RXPOS\n\tCLR TXPOS\n\tJMP HGHEAD\n')
    start = mapped.index('HGTXBY:\n')
    end = mapped.index('; Preserve the serial loop')
    return {'demos/rt11/hostdisk/HGX.MAC': mapped[:start]+wire+mapped[end:]}


def run(out):
    out.mkdir(parents=True,exist_ok=False)
    records = {}
    for mode in ('success','status','checksum','timeout','invalid_date','day_limit','high_ticks'):
        folder = out/mode
        inputs = build(folder,overrides=fixture(mode))
        sync=folder/'HGSYNC.COM';sync.write_bytes(text_bytes(ROOT/'demos/rt11/sd-hc7000/HGSYNC.COM'))
        subprocess.run([str(ROOT.parent/'lsi11/rt11tool'),'add',str(folder/'build.dsk'),str(sync),'HGSYNC.COM'],check=True,capture_output=True)
        with (folder/'test.log').open('wb') as log:
            c = Console(folder/'build.ini',log)
            try:
                boot(c,'XM')
                command(c,'DATE 1-JAN-90');command(c,'TIME 12:34:56')
                for text in ('REMOVE HG','INSTALL HG','LOAD HG'):
                    command(c,text)
                c.send('@HGSYNC\r' if mode=='success' else 'RUN HGTIME\r')
                c.expect(rb'RUN HGTIME\r\n')
                text = c.expect(rb'\r\n\.')
                if mode == 'success':
                    assert b'RT-11 SYSTEM DATE AND TIME SET FROM HOST' in text,text
                    assert b'23:59:50 22/09' in text,text
                    assert b'22-Sep-2026' in command(c,'DATE')
                    assert b'23:59:5' in command(c,'TIME')
                else:
                    assert b'TIME REQUEST FAILED; SYSTEM TIME UNCHANGED' in text,text
                    assert b'1-Jan-1990' in command(c,'DATE')
                    assert b'12:34:' in command(c,'TIME')
                command(c,'UNLOAD HG')
            finally:
                c.close()
        records[mode] = dict(passed=True,inputs=inputs)
        print('PASS XM HGTIME:',mode,flush=True)
    (out/'result.json').write_text(json.dumps(dict(passed=True,cases=records,
        gpio='handshake/byte I/O fixture; real XM monitor and buffer services',
        inputs={p:sha((ROOT/p).read_bytes()) for p in ('tools/test_hgx.py','tools/test_hg_time.py',
            'demos/rt11/sd-hc7000/HGSYNC.COM')},sync_command_file=True),indent=2)+'\n')


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    run(p.parse_args().out.resolve())
