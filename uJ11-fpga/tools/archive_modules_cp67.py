#!/usr/bin/env python3
"""Archive CP67 native programs, verified RTL regressions and exact sources."""
import argparse
import json
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path
from board_common import ROOT
from build_software_cp67 import OUT as SW, sha
from build_modules_cp67 import OUT as HW


def archive(rt11):
    target=ROOT/'tb/reports/cp67'
    assert not target.exists(),'immutable archive already exists'
    sources=set();copies=[]
    def verify(mapping):
        for name,digest in mapping.items():
            assert not Path(name).is_absolute() and '..' not in Path(name).parts,name
            assert sha(ROOT/name)==digest,name
            sources.add(name)
    def keep(path,name):copies.append((path,name))
    counts={}
    unit=SW/'format-tests.log'
    with unit.open('w') as log:
        subprocess.run([sys.executable,str(ROOT/'tools/test_module_image_cp67.py'),'-v'],stdout=log,stderr=subprocess.STDOUT,check=True)
    keep(unit,'regressions/format-tests.log')
    counts['file-format']=dict(tests=6,passed=True)
    for mode in ('portable','vendor','portable-window'):
        result=json.loads((HW/(mode+'-result.json')).read_text());assert result['passed']
        verify(result['files'])
        text=(HW/(mode+'.log')).read_text()
        m=re.search(r'PASS CP67 modules: (\d+) cases, (\d+) checks',text);assert m
        counts[mode]=dict(cases=int(m[1]),checks=int(m[2]))
        for suffix in ('-result.json','-build.log','.log'):keep(HW/(mode+suffix),'cold/'+mode+suffix)
        if mode=='portable-window':
            assert not result['recovery_window_overrides']
            counts[mode]['clocks']=int(re.search(r'cold clocks (\d+)',text)[1])
    assert counts['portable']==counts['vendor']==dict(cases=30,checks=249)
    native=ROOT/'build/cp67-loader-functions'
    r=json.loads((native/'result.json').read_text());assert r['passed'];verify(r['files'])
    counts['native-loader']={k:r[k] for k in ('cases','checks','clocks')}
    for f in ('result.json','build.log','simulation.log','uart.txt'):keep(native/f,'regressions/native-loader/'+f)
    hw=json.loads((HW/'inputs.json').read_text());verify(hw['inputs']);verify(hw['outputs'])
    sources.add(str((HW/'inputs.json').relative_to(ROOT)))
    legacy=ROOT/'build/cp63-debug/inputs.json'
    prior=json.loads(legacy.read_text());verify(prior['inputs']);verify(prior['outputs'])
    sources.add(str(legacy.relative_to(ROOT)))
    verify(json.loads((ROOT/'firmware/cp62/loader-inputs.json').read_text())['files'])
    sources.update(('firmware/cp62/loader-inputs.json','tools/run_service_cp59.py'))
    synthesis=ROOT/'synth/reports/cp67b'
    measured=json.loads((synthesis/'result.json').read_text())
    assert measured['fully_routed'] and measured['timing_pass'] and measured['trace_timing_pass']
    verify({p:h for p,h in json.loads((synthesis/'inputs.json').read_text())['files'].items() if not p.startswith('generated:')})
    for name in ('core','panel','odt'):
        out=ROOT/('build/cp67-'+name+'-regression')
        r=json.loads((out/'result.json').read_text());assert r['passed'];verify(r['files'])
        m=re.search(r'PASS CP64 monitor: (\d+) checks (\d+) commands (\d+) clocks',(out/'simulation.log').read_text());assert m
        counts[name]=dict(checks=int(m[1]),commands=int(m[2]),clocks=int(m[3]))
        for f in ('result.json','build.log','simulation.log','uart.txt'):keep(out/f,'regressions/'+name+'/'+f)
    r=json.loads((rt11/'result.json').read_text());assert r['passed']
    for p,h in r['files'].items():assert sha(rt11/p)==h,p
    manifest=json.loads((rt11/'inputs.json').read_text());verify(manifest['files'])
    counts['rt11']={k:r[k] for k in ('checks','clocks','uart_bytes')}
    for f in ('result.json','inputs.json','build.log','simulation.log','uart.txt'):keep(rt11/f,'rt11/'+f)
    for name in ('odt','sdboot','loader'):
        out=SW/name;r=json.loads((out/'result.json').read_text());verify(r['sources']);verify(r['outputs'])
        asm=ROOT/r['assembly']
        for p,h in r['assembler']['outputs'].items():assert sha(asm/p)==h,p
        verify(r['assembler']['source_sha256'])
        for p in asm.iterdir():
            if p.suffix in ('.MAC','.LST','.OBJ','.SAV','.MAP') or p.name in ('console.log','build-inputs.json'):
                keep(p,'assembly/'+name+'/'+p.name)
        for p in out.iterdir():
            if p.is_file() and p.suffix in ('.json','.bin','.BIN','.SAV','.MAC'):keep(p,'software/'+name+'/'+p.name)
    # Locate the exact current cold-walker native assembly by the build hash.
    coldsha=sha(ROOT/'firmware/cp67/BOOT.MAC')
    asm=HW/('assembly-'+coldsha[:12])
    for p,h in hw['assembler']['outputs'].items():assert sha(asm/p)==h,p
    for p in asm.iterdir():
        if p.suffix in ('.MAC','.LST','.OBJ','.SAV','.MAP') or p.name in ('console.log','build-inputs.json'):keep(p,'assembly/cold/'+p.name)
    sources.update(('tools/archive_modules_cp67.py','tools/verify_modules_cp67.py','tools/test_module_image_cp67.py','tools/rt11_build.py'))
    # Deliveries remain explicitly separate from FPGA programming.
    release=ROOT/'demos/rt11/service/cp67';release.mkdir(parents=True,exist_ok=True)
    for p in (SW/'odt/ODT.BIN',SW/'sdboot/SDBOOT.BIN',SW/'loader/UJMOD.SAV',SW/'loader/UJMOD.MAC'):
        shutil.copyfile(p,release/p.name);assert sha(p)==sha(release/p.name)
        sources.add(str((release/p.name).relative_to(ROOT)))
    (release/'release.json').write_text(json.dumps(dict(abi=3,relocatable=False,requires_fpga='CP67 --modules-cp67',programmed=False,
        files={p.name:sha(release/p.name) for p in (SW/'odt/ODT.BIN',SW/'sdboot/SDBOOT.BIN',SW/'loader/UJMOD.SAV',SW/'loader/UJMOD.MAC')}),indent=2)+'\n')
    sources.add(str((release/'release.json').relative_to(ROOT)))
    target.mkdir(parents=True)
    artifacts={}
    for src,name in copies:
        dest=target/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest)
        assert sha(dest)==sha(src);artifacts[name]=sha(dest)
    hashes={p:sha(ROOT/p) for p in sorted(sources)}
    for p in hashes:assert 'microasm11' not in p and not p.lower().endswith('.dsk'),p
    with tarfile.open(target/'source.tgz','w:gz') as t:
        for p in hashes:t.add(ROOT/p,arcname=p,recursive=False)
    with tarfile.open(target/'source.tgz') as t:
        import hashlib
        for p,h in hashes.items():assert hashlib.sha256(t.extractfile(p).read()).hexdigest()==h,p
    artifacts['source.tgz']=sha(target/'source.tgz')
    record=dict(passed=True,counts=counts,source_files=hashes,artifacts=artifacts,programmed=False,
                hardware=dict(checkpoint='cp67b',lut=measured['lut4'],ff=measured['ff'],ebr=measured['ebr'],fmax_mhz=measured['fmax_mhz'],
                              files={str(p.relative_to(ROOT)):sha(p) for p in synthesis.iterdir() if p.is_file()}))
    (target/'archive.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(dict(counts=counts,source_files=len(hashes),artifacts=len(artifacts)),indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--rt11',type=Path,required=True)
    archive(p.parse_args().rt11.resolve())
