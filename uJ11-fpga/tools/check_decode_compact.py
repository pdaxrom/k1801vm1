#!/usr/bin/env python3
"""CP36 synchronous dispatch equality and enable-hold checks on actual EBR models."""
import hashlib
import json
import subprocess
from board_common import ROOT


def main():
    paths = ['tools/check_decode_compact.py','tb/tb_decode_rom.v','reference/uj11/decode_cp27.v',
             'build/cp36-decode/decode.mem','build/cp36-decode/uj11_decode_table.v',
             'build/cp36-decode/planes/uj11_decode_rom.v','build/cp36-decode/bits/uj11_decode_rom.v']
    paths += ['build/vendor/'+p+'.v' for p in ('DP8KC','GSR','PUR')]
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    results = []
    for variant in ('planes','bits'):
        for vendor in (False,True):
            tag = f'cp36-{variant}-'+('vendor' if vendor else 'portable')
            flags = ['-DUJ11_VENDOR_ROM'] if vendor else []
            sources = ['tb/tb_decode_rom.v',f'build/cp36-decode/{variant}/uj11_decode_rom.v',
                       'build/cp36-decode/uj11_decode_table.v','reference/uj11/decode_cp27.v']
            if vendor:
                sources += ['build/vendor/'+p+'.v' for p in ('DP8KC','GSR','PUR')]
            with (ROOT/f'build/{tag}-build.log').open('w') as log:
                subprocess.run(['iverilog','-g2012']+flags+['-s','tb_decode_rom','-o','build/'+tag]+sources,
                               cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
            with (ROOT/f'build/{tag}.log').open('w') as log:
                subprocess.run(['vvp','build/'+tag],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
            text = (ROOT/f'build/{tag}.log').read_text()
            line = 'PASS synchronous decode: all 65536 opcodes and 65536 enable holds match CP27'
            assert line in text
            results.append(dict(tag=tag,variant=variant,vendor=vendor,pass_line=line))
            print(tag+': '+line,flush=True)
    assert hashes == {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    (ROOT/'build/cp36-decode-tests.json').write_text(json.dumps(dict(tests=results,inputs_sha256=hashes),indent=2)+'\n')


if __name__=='__main__':
    main()
