#!/usr/bin/env python3
"""Check SERV/sector RAM arbitration, byte lanes and real shared-bank instruction fetches."""
import argparse
import json
from pathlib import Path
import subprocess
from build_mmu_board import build, CORE, BOARD, ROOT, sha


def run(out, vendor=None):
    record=build()
    if record['iop']['profile']!='storage':
        raise ValueError('Set UJ11_MMU_IOP=storage and UJ11_MMU_FPP=off')
    out.mkdir(parents=True,exist_ok=True)
    tests=['tests/tb_storage_ram.v','tests/mmu/tb_serv_bootstrap_tail.v']
    files={p:sha(ROOT/p) for p in CORE+BOARD+tests+['tools/test_storage_ram.py']}
    cases=[]

    def check(name,top,inputs,flags=()):
        with (out/(name+'-build.log')).open('w') as log:
            subprocess.run(['iverilog','-g2012','-s',top,'-o',str(out/name),*flags,*inputs],
                cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (out/(name+'.log')).open('w') as log:
            subprocess.run(['vvp',str(out/name)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        text=(out/(name+'.log')).read_text()
        assert 'PASS ' in text
        print(text.strip())
        cases.append(name)

    ram=['tests/tb_storage_ram.v','build/hc7000-mmu-iop/uj11_mmu_iop_ram.v',
         'build/hc7000-mmu-iop/uj11_sector_ram.v']
    check('ram','tb_storage_ram',ram)
    vendor_files={}
    if vendor:
        models=[str(vendor/(name+'.v')) for name in ('DP8KC','PDPW8KC','GSR','PUR')]
        vendor_files={p:sha(Path(p)) for p in models}
        check('ram-vendor','tb_storage_ram',ram+models,['-DUJ11_IOP_VENDOR_RAM'])
    for mhz in (24,50):
        for boundary in (16*1024-2,18*1024-2):
            check(f'tail-exec-{mhz}-{boundary}','tb_serv_bootstrap_tail',CORE+BOARD+[tests[1]],
                [f'-Ptb_serv_bootstrap_tail.CLOCK_HZ={mhz*1000000}',f'-Ptb_serv_bootstrap_tail.BOOT_ADDRESS={boundary}'])
    assert all(sha(ROOT/p)==digest for p,digest in files.items())
    (out/'result.json').write_text(json.dumps(dict(passed=True,inputs=record,
        files=files,vendor_files=vendor_files,cases=cases),indent=2)+'\n')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=ROOT/'build/test-storage-ram')
    parser.add_argument('--vendor-library',type=Path)
    args=parser.parse_args()
    run(args.out.resolve(),args.vendor_library.resolve() if args.vendor_library else None)
