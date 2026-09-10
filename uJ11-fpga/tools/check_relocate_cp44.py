#!/usr/bin/env python3
"""CP44 directed actual CPU + board + SPI FRAM verification."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from board_common import ROOT,CORE,BOARD
from build_relocate_cp44 import adapt


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--edges',action='store_true')
    p.add_argument('--direct',action='store_true')
    p.add_argument('--vendor',action='store_true');p.add_argument('--words',type=int,default=4096)
    args=p.parse_args();assert 1<=args.words<=4096
    if args.direct:
        from build_direct_relocate_cp44 import adapt as direct_adapt
        core,board=direct_adapt(CORE,BOARD)
    else:core,board=adapt(CORE,BOARD)
    top='tb_relocate_edges_cp44' if args.edges else 'tb_relocate_cpu_cp44'
    tag='cp44-'+('direct-' if args.direct else '')+'cpu-'+('vendor' if args.vendor else 'portable')+('-edges' if args.edges else '-'+str(args.words))
    sources=['tb/'+top+'.v']+core+board+['reference/lsi11/spi_fram_model.v',
             ('microcode/generated/uj11_m0_ebr.v' if args.direct else 'build/cp44-relocate/uj11_m0_ebr.v') if args.vendor else 'rtl/uj11_rom.v']
    if args.vendor:sources+=['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')]
    extras=['tools/check_relocate_cp44.py','tools/build_relocate_cp44.py','build/cp44-relocate/inputs.json',
            'build/cp44-relocate/relocate.mem','microcode/generated/firmware.mem','microcode/generated/decode.mem']
    if args.direct:extras+=['tools/build_direct_relocate_cp44.py','build/cp44-direct/inputs.json','microcode/generated/m0.mem']
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources+extras}
    compiled=sources.copy()
    if args.vendor:
        command=['iverilog','-g2012','-Wall','-DUJ11_VENDOR_ROM','-s',top]+([] if args.edges else ['-P'+top+'.WORDS='+str(args.words)])+['-o','build/'+tag]+compiled
        exe=['vvp','build/'+tag]
    else:
        out=ROOT/'build/cp44-test-reference';out.mkdir(exist_ok=True)
        for i,name in enumerate(compiled):
            if name.startswith('reference/'):
                path=out/Path(name).name
                path.write_text('/* verilator lint_off WIDTH */\n'+(ROOT/name).read_text()+'\n/* verilator lint_on WIDTH */\n')
                compiled[i]=str(path)
        command=['verilator','--binary','--timing','--top-module',top,'-j','4']+([] if args.edges else ['-GWORDS='+str(args.words)])+['--Mdir','build/obj-'+tag]+compiled
        exe=['build/obj-'+tag+'/V'+top]
    with (ROOT/f'build/{tag}-build.log').open('w') as log:
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (ROOT/f'build/{tag}.log').open('w') as log:
        subprocess.run(exe,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in hashes}
    lines=[s for s in (ROOT/f'build/{tag}.log').read_text().splitlines() if s.startswith('PASS')]
    assert len(lines)==2,lines
    (ROOT/f'build/{tag}.json').write_text(json.dumps(dict(words=args.words,vendor=args.vendor,direct=args.direct,edges=args.edges,
        inputs_sha256=hashes,pass_lines=lines),indent=2)+'\n')
    print('\n'.join(lines))


if __name__=='__main__':main()
