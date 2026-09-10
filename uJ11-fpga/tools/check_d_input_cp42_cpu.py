#!/usr/bin/env python3
"""Reuse established CP39 CPU tests with isolated CP42 output names."""
import argparse
import hashlib
import json
import subprocess
import sys
from board_common import ROOT
from build_mmu_entry import change


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('suite',choices=('miter','csr'))
    parser.add_argument('--vendor',action='store_true')
    args=parser.parse_args();assert not args.vendor or args.suite=='csr'
    out=ROOT/'build/cp42-cpu';out.mkdir(exist_ok=True)
    if args.suite=='miter':
        source='tools/check_mmu_apr_csr_miter.py'
        code=(ROOT/source).read_text().replace('cp39-tests','cp42-cpu').replace('cp39-miter-','cp42-miter-')
        flags=[]
    else:
        source='tools/check_mmu_apr_csr.py'
        code=(ROOT/source).read_text().replace('cp39-tests','cp42-cpu')
        code=change(code,"tag='cp39-'","tag='cp42-'")
        flags=['cpu']+(['--vendor'] if args.vendor else [])
    code='import sys\nfrom pathlib import Path\nsys.path.insert(0,str(Path(__file__).resolve().parents[2]/"tools"))\n'+code
    name=args.suite+('-vendor' if args.vendor else '-portable')
    driver=out/(name+'.py');driver.write_text(code)
    paths=[source,'tools/check_d_input_cp42_cpu.py',str(driver.relative_to(ROOT))]
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    subprocess.run([sys.executable,str(driver)]+flags,cwd=ROOT,check=True)
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    (out/(name+'.json')).write_text(json.dumps(dict(inputs_sha256=hashes),indent=2)+'\n')


if __name__=='__main__':main()
