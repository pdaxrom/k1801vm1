#!/usr/bin/env python3
"""Freeze CP69 passing inputs, native assembly, measurements and release files."""
import argparse
import hashlib
import json
import shutil
import tarfile
from pathlib import Path
from board_common import ROOT
from build_fp11_cp69 import OUT


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def archive(rt11):
    target=ROOT/'tb/reports/cp69'
    assert not target.exists(),'immutable archive already exists'
    sources={};copies=[]
    def verify(mapping):
        for name,digest in mapping.items():
            assert not Path(name).is_absolute(),name
            if '..' in Path(name).parts:
                assert name in ('../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h','../core/pdp11_fp.c'),name
            assert sha(ROOT/name)==digest,name
            if name in sources:assert sources[name]==digest,name
            sources[name]=digest
    modes=('sync','logic','vendor','events-sync','events-vendor','board-sync')
    for mode in modes:
        out=OUT/mode;r=json.loads((out/'result.json').read_text());assert r['passed']
        verify(r['inputs'])
        assert 'PASS CP69' in (out/'simulation.log').read_text(),mode
        for name in ('result.json','build.log','simulation.log','metrics.csv','oracle.log'):
            if (out/name).exists():copies.append((out/name,mode+'/'+name))
    r=json.loads((rt11/'result.json').read_text());assert r['passed']
    for name,digest in r['files'].items():assert sha(rt11/name)==digest,name
    inputs=json.loads((rt11/'inputs.json').read_text());verify(inputs['files'])
    for name in ('result.json','inputs.json','build.log','simulation.log','uart.txt'):
        copies.append((rt11/name,'rt11/'+name))
    fp=json.loads((OUT/'software/result.json').read_text());verify(fp['sources']);verify(fp['outputs'])
    for record,folder in ((fp,'fp11'),):
        assembly=ROOT/record['assembly']
        for name,digest in record['assembler']['outputs'].items():assert sha(assembly/name)==digest,name
        for p in assembly.iterdir():
            if p.suffix in ('.LST','.MAP','.OBJ','.SAV') or p.name in ('console.log','build-inputs.json'):
                copies.append((p,'assembly/'+folder+'/'+p.name))
    guest=rt11/('asm-'+sha(rt11/'FPTST.MAC')[:12])
    for name,digest in inputs['guest_assembly']['outputs'].items():assert sha(guest/name)==digest,name
    for p in guest.iterdir():
        if p.suffix in ('.LST','.MAP','.OBJ','.SAV') or p.name in ('console.log','build-inputs.json'):
            copies.append((p,'assembly/fptst/'+p.name))
    # All non-generated entries, including RTL, ROM, constraints and generators,
    # must still equal the measured CP67b revision. This is not a new synthesis.
    measured=json.loads((ROOT/'synth/reports/cp67b/inputs.json').read_text())
    hw={n:h for n,h in measured['files'].items() if not n.startswith('generated:')}
    verify(hw)
    release=ROOT/'demos/rt11/service/cp69';release.mkdir(parents=True,exist_ok=True)
    for p in (OUT/'software/FP11.BIN',ROOT/'firmware/fp11/FP11.MAC',rt11/'FPTST.MAC',rt11/'FPTST.SAV'):
        shutil.copyfile(p,release/p.name)
    record=dict(abi=3,relocatable=False,checkpoint='CP69 control/status addressing',requires_fpga='CP67b',
                installed_on_board=False,supported_encodings=197,allocation_bytes=fp['allocation_bytes'],
                format=fp['format'],files={n:sha(release/n) for n in ('FP11.BIN','FP11.MAC','FPTST.MAC','FPTST.SAV')})
    (release/'release.json').write_text(json.dumps(record,indent=2)+'\n')
    copies.append((OUT/'software/result.json','software.json'))
    for p in release.iterdir():
        if p.is_file():sources[str(p.relative_to(ROOT))]=sha(p)
    for name in ('tools/archive_fp69.py','tools/verify_fp69.py','docs/fp11-memory-cp69.md',
                 'synth/reports/cp67b/inputs.json','synth/reports/cp67b/result.json'):
        sources[name]=sha(ROOT/name)
    target.mkdir(parents=True)
    artifacts={}
    for src,name in copies:
        dest=target/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest)
        assert sha(dest)==sha(src);artifacts[name]=sha(dest)
    mapping={name:dict(member=('oracle/'+name[3:] if name.startswith('../') else 'project/'+name),sha256=h)
             for name,h in sorted(sources.items())}
    with tarfile.open(target/'source.tgz','w:gz') as t:
        for name,m in mapping.items():
            assert not name.lower().endswith('.dsk') and 'microasm11' not in name
            assert '..' not in Path(m['member']).parts and not Path(m['member']).is_absolute()
            t.add(ROOT/name,arcname=m['member'],recursive=False)
    artifacts['source.tgz']=sha(target/'source.tgz')
    (target/'archive.json').write_text(json.dumps(dict(passed=True,installed_on_board=False,
        source_files=mapping,artifacts=artifacts,hardware=dict(checkpoint='CP67b',new_synthesis=False,files=hw),
        rt11={k:r[k] for k in ('checks','clocks','uart_bytes')}),indent=2)+'\n')
    print(json.dumps(dict(source_files=len(mapping),artifacts=len(artifacts),rt11=r),indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--rt11',type=Path,required=True)
    archive(p.parse_args().rt11.resolve())
