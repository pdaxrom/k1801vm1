#!/usr/bin/env python3
"""Freeze the qualified DCJ11 FPP software release, sources and raw results."""
import hashlib
import json
import shutil
import tarfile
from pathlib import Path
from board_common import ROOT
from module_image_cp67 import decode

TARGET=ROOT/'tb/reports/cp79'
RELEASE=ROOT/'demos/rt11/service/cp79'
RUNS={n:'cp79-fp11/'+n for n in ('math','conversions','logic','sync','events-sync','events-vendor')}
RUNS['rt11']='cp79-rt11'

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())


def archive():
    assert not (TARGET/'archive.json').exists(),'immutable archive; choose a new checkpoint'
    TARGET.mkdir(parents=True,exist_ok=True);RELEASE.mkdir(parents=True,exist_ok=True)
    sources={};artifacts={};results={};references={}
    def source(p,expected=None):
        p=p.resolve()
        name=str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else '../'+str(p.relative_to(ROOT.parent))
        digest=sha(p);assert expected is None or digest==expected,(name,digest,expected)
        # Record disk hashes in their original manifests; never archive images.
        if p.suffix.lower() in ('.dsk','.img','.iso'):return
        if p.name=='vectors.txt' and p.parent.parent.name=='parts':
            master=p.parent.parent.parent/'vectors.txt'
            parts=read(master.parent/'result.json')['partitions']
            part=next(x for x in parts if name in x['files'])
            source(master)
            references[name]=dict(master=str(master.relative_to(ROOT)),begin=part['begin'],end=part['end'],sha256=digest)
            return
        sources[name]=dict(sha256=digest,member=('project/'+name if not name.startswith('../') else 'external/'+name[3:]))
    def collect(record):
        if isinstance(record,dict):
            for k,v in record.items():
                if isinstance(v,str) and len(v)==64 and '/' in k and (ROOT/k).is_file():source(ROOT/k,v)
                elif isinstance(v,(dict,list)):collect(v)
        elif isinstance(record,list):
            for x in record:collect(x)
    def artifact(p,name):
        dest=TARGET/name;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(p,dest);artifacts[name]=sha(dest)
    for label,folder in RUNS.items():
        d=ROOT/'build'/folder;r=read(d/'result.json');assert r['passed'],label
        results[label]={k:r[k] for k in ('passed','checks','clocks','uart_bytes','comparisons') if k in r}
        collect(r)
        if (d/'inputs.json').exists():collect(read(d/'inputs.json'))
        for n in ('result.json','inputs.json','coverage.json','simulation.log','uart.txt','build.log','metrics.csv'):
            if (d/n).exists():artifact(d/n,label+'/'+n)
    for label in ('sync','logic'):
        expected=read(TARGET/label/'coverage.json')['cases']
        assert f'{expected} cases /' in (TARGET/label/'simulation.log').read_text()
    for label in ('events-sync','events-vendor'):
        assert '111 cases / 4174 checks' in (TARGET/label/'simulation.log').read_text()
    assert results['rt11']['checks']==99
    production={}
    for label,folder in (('odt','cp79-odt'),('fp11','cp79-fp11/software')):
        d=ROOT/'build'/folder;r=read(d/'result.json');collect(r);production[label]=r
        source(d/'result.json');source(d/('payload.bin' if label=='odt' else 'image.bin'))
        for p in (ROOT/r['assembly']).iterdir():
            if p.suffix.upper() in ('.LST','.MAP','.OBJ','.SAV') or p.name=='build-inputs.json':
                artifact(p,'assembly/'+label+'/'+p.name)
    assert production['odt']['symbols']['MEMEND']<=production['fp11']['symbols']['INIT']
    for src,name in ((ROOT/'build/cp79-odt/ODT.BIN','ODT.BIN'),(ROOT/'build/cp79-odt/UJMON.MAC','UJMON.MAC'),
                     (ROOT/'build/cp79-fp11/software/FP11.BIN','FP11.BIN'),(ROOT/'firmware/fp11/FP11.MAC','FP11.MAC'),
                     (ROOT/'build/cp79-rt11/FPTST.SAV','FPTST.SAV'),(ROOT/'firmware/fp11/FPTST.MAC','FPTST.MAC')):
        shutil.copyfile(src,RELEASE/name);source(src)
    assert sha(RELEASE/'ODT.BIN')==sha(ROOT/'demos/rt11/service/cp77/ODT.BIN'),'ODT changed'
    release=dict(checkpoint='CP79',profile='DEC DCJ11 FPP software',installed_on_board=False,
        modules={name:dict(format=decode((RELEASE/file).read_bytes()),allocation_bytes=production[name]['allocation_bytes'])
                 for name,file in (('odt','ODT.BIN'),('fp11','FP11.BIN'))},
        files={p.name:sha(p) for p in RELEASE.iterdir() if p.is_file() and p.name!='release.json'})
    (RELEASE/'release.json').write_text(json.dumps(release,indent=2)+'\n')
    for p in RELEASE.iterdir():source(p)
    previous=read(ROOT/'tb/reports/cp77/archive.json')['hardware']
    hardware={};tool_changes={}
    for name,digest in previous['files'].items():
        current=sha(ROOT/name)
        if current!=digest:
            assert name=='tools/checkpoint_board.py',name
            tool_changes[name]=dict(cp77=digest,current=current,reason='explicit CP78 experiment options; CP67b unchanged')
        else:hardware[name]=digest
        source(ROOT/name,current)
    assert len(hardware)==52
    for p in (ROOT/'tools').glob('*cp79.py'):source(p)
    for p in (ROOT/'tb').glob('*cp79*'):
        if p.is_file():source(p)
    for name in ('README.md','TODO.md','docs/README.md','docs/implementation-status.md','docs/synthesis.md','docs/benchmarks.md','docs/fpp-j11-cp79.md'):
        source(ROOT/name)
    with tarfile.open(TARGET/'source.tgz','w:gz') as t:
        for name,entry in sorted(sources.items()):t.add(ROOT/name,arcname=entry['member'],recursive=False)
    artifacts['source.tgz']=sha(TARGET/'source.tgz')
    result=dict(checkpoint='CP79',passed=True,installed_on_board=False,
        qualification='DCJ11 documented FPP software profile; PSW MMIO operands and physical-board qualification remain open',
        source_files=sources,references=references,artifacts=artifacts,results=results,release=release,
        hardware=dict(baseline='CP67b',unchanged_files=hardware,tool_changes=tool_changes,
                      added_lut=0,added_ff=0,added_ebr=0,added_uwords=0))
    (TARGET/'archive.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'CP79: {len(sources)} sources, {len(artifacts)} artifacts')


if __name__=='__main__':archive()
