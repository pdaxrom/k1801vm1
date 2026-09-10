#!/usr/bin/env python3
"""Actual board bus contracts on portable and unmodified vendor firmware EBR."""
import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path
from board_common import ROOT, BOARD


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--variant', default='current', choices=('prefix','split','split-base','priority','current'))
    args=p.parse_args()
    bus='boards/hc1200/uj11_board_bus.v' if args.variant=='current' else f'build/cp38-bus/{args.variant}/uj11_board_bus.v'
    sources=['tb/tb_board_bus.v']+[bus if s=='boards/hc1200/uj11_board_bus.v' else s for s in BOARD]+['reference/lsi11/spi_fram_model.v']
    library=Path(os.environ.get('LATTICE_SIM_DIR',ROOT/'build/vendor'))
    vendor=[str(library/(n+'.v')) for n in ('DP8KC','GSR','PUR')]
    inputs=sources+['tools/check_board_decode_units.py','microcode/generated/firmware.mem']
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs+vendor}
    results=[]
    for is_vendor in (False,True):
        tag='cp38-bus-'+('vendor' if is_vendor else 'portable')
        with (ROOT/f'build/{tag}-build.log').open('w') as log:
            subprocess.run(['iverilog','-g2012','-Wall','-s','tb_board_bus','-o','build/'+tag]+
                (['-DUJ11_VENDOR_ROM']+vendor if is_vendor else [])+sources,
                cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (ROOT/f'build/{tag}.log').open('w') as log:
            subprocess.run(['vvp','build/'+tag],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        line=next(s for s in (ROOT/f'build/{tag}.log').read_text().splitlines() if s.startswith('PASS board bus:'))
        assert '30 beats' in line
        results.append(dict(tag=tag,pass_line=line,vendor=is_vendor))
        print(line,flush=True)
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in hashes}
    (ROOT/'build/cp38-bus-units.json').write_text(json.dumps(dict(variant=args.variant,tests=results,inputs_sha256=hashes),indent=2)+'\n')


if __name__=='__main__':
    main()
