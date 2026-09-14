#!/usr/bin/env python3
"""Freeze CP77 software, qualified verification results and DEC provenance."""
import hashlib,json,shutil,tarfile
from pathlib import Path
from board_common import ROOT
from module_image_cp67 import decode

TARGET=ROOT/'tb/reports/cp77'
RELEASE=ROOT/'demos/rt11/service/cp77'
RUNS={'core':'cp77-odt-core','breakpoints':'cp77-breakpoints','fp-debug':'cp77-odt-fp','panel':'cp77-panel',
      'logic':'cp77-fp11/logic','events-sync':'cp77-fp11/events-sync','events-vendor':'cp77-fp11/events-vendor',
      'rt11':'cp77-rt11','dec-native':'cp77-diagnostics/FFPAA1',
      'dec-a':'cp77-diagnostics/FFPAA1-psw','dec-b':'cp77-diagnostics/FFPBA0-psw','dec-c':'cp77-diagnostics/FFPCB0-psw'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())

def archive():
    assert not (TARGET/'archive.json').exists(),'archive is immutable; choose a fresh checkpoint'
    TARGET.mkdir(parents=True,exist_ok=True);RELEASE.mkdir(parents=True,exist_ok=True)
    sources={};references={};artifacts={};results={}
    previous=read(ROOT/'tb/reports/cp76/archive.json')
    def source(p,expected=None):
        p=p.resolve();name=str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else '../'+str(p.relative_to(ROOT.parent))
        assert p.exists(),name
        digest=sha(p);assert expected is None or digest==expected,(name,digest,expected)
        if p.suffix.lower() in ('.dsk','.img','.iso'):return
        if name.endswith('/vectors.txt'):
            old=name.replace('cp77','cp76');ref=previous['source_files'][old];assert digest==ref['sha256'],name
            references[name]=dict(archive='tb/reports/cp76/source.tgz',member=ref['member'],sha256=digest)
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
        dest=TARGET/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest);artifacts[name]=sha(dest)
    for label,folder in RUNS.items():
        d=ROOT/'build'/folder;r=read(d/'result.json');results[label]={k:r[k] for k in ('passed','checks','commands','clocks','uart_bytes','psw_read_adapter') if k in r}
        assert r['passed']==(label!='dec-native'),label
        if label.startswith('dec-') and label!='dec-native':assert r['psw_read_adapter'],label
        collect(r)
        if (d/'inputs.json').exists():collect(read(d/'inputs.json'))
        for n in ('result.json','inputs.json','simulation.log','uart.txt','build.log','metrics.csv'):
            if (d/n).exists():artifact(d/n,label+'/'+n)
    assert '004712' in (TARGET/'dec-native/uart.txt').read_text()
    assert 'TOTAL ERRORS SINCE LAST REPORT      1' in (TARGET/'dec-native/uart.txt').read_text()
    assert '215784 cases / 15052576 checks / 6014 manual' in (TARGET/'logic/simulation.log').read_text()
    for label in ('dec-a','dec-b','dec-c'):
        assert 'TOTAL ERRORS SINCE LAST REPORT      0' in (TARGET/label/'uart.txt').read_text()
    for label in ('events-sync','events-vendor'):assert '111 cases / 4174 checks' in (TARGET/label/'simulation.log').read_text()
    production={}
    for label,folder in (('odt','cp77-odt'),('fp11','cp77-fp11/software')):
        d=ROOT/'build'/folder;r=read(d/'result.json');collect(r);production[label]=r
        source(d/'result.json');source(d/'payload.bin' if label=='odt' else d/'image.bin')
        for p in (ROOT/r['assembly']).iterdir():
            if p.suffix.upper() in ('.LST','.MAP','.OBJ','.SAV') or p.name=='build-inputs.json':artifact(p,'assembly/'+label+'/'+p.name)
    assert production['odt']['symbols']['MEMEND']<=production['fp11']['symbols']['INIT']
    for src,name in ((ROOT/'build/cp77-odt/ODT.BIN','ODT.BIN'),(ROOT/'build/cp77-odt/UJMON.MAC','UJMON.MAC'),
                     (ROOT/'build/cp77-fp11/software/FP11.BIN','FP11.BIN'),(ROOT/'firmware/fp11/FP11.MAC','FP11.MAC'),
                     (ROOT/'build/cp77-rt11/FPTST.SAV','FPTST.SAV'),(ROOT/'firmware/fp11/FPTST.MAC','FPTST.MAC')):
        shutil.copyfile(src,RELEASE/name);source(src)
    release=dict(checkpoint='CP77',installed_on_board=False,modules={name:dict(format=decode((RELEASE/file).read_bytes()),allocation_bytes=production[name]['allocation_bytes']) for name,file in (('odt','ODT.BIN'),('fp11','FP11.BIN'))},
                 files={p.name:sha(p) for p in RELEASE.iterdir() if p.is_file() and p.name!='release.json'})
    (RELEASE/'release.json').write_text(json.dumps(release,indent=2)+'\n')
    for p in RELEASE.iterdir():source(p)
    for name in previous['hardware']['files']:source(ROOT/name,previous['hardware']['files'][name])
    for p in (ROOT/'tools').glob('*cp77.py'):source(p)
    for p in (ROOT/'docs').glob('*cp77.md'):source(p)
    for name in ('README.md','TODO.md','docs/README.md','docs/implementation-status.md','docs/synthesis.md','docs/benchmarks.md',
                 'demos/rt11/service/cp76/FP11.MAC','tb/odt_fp_panel_cp77.vh','tb/odt_cp66_panel.vh'):
        source(ROOT/name)
    d=ROOT/'build/cp77-diagnostics';extraction=read(d/'extraction.json')
    assert sha(ROOT/extraction['disk'])==extraction['disk_sha256']
    for p in d.iterdir():
        if p.is_file() and (p.name.endswith(('.BIN','.BIC','.ram','.json','.txt'))):artifact(p,'diagnostics/'+p.name)
    with tarfile.open(TARGET/'source.tgz','w:gz') as t:
        for name,entry in sorted(sources.items()):t.add(ROOT/name,arcname=entry['member'],recursive=False)
    artifacts['source.tgz']=sha(TARGET/'source.tgz')
    result=dict(checkpoint='CP77',passed=True,installed_on_board=False,
                qualification='ODT/FP software regressions pass; native DEC diagnostics require missing PSW register 177776',
                source_files=sources,references=references,artifacts=artifacts,results=results,
                hardware=previous['hardware'],release=release,diagnostics=extraction)
    (TARGET/'archive.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'CP77: {len(sources)} sources, {len(artifacts)} artifacts, {len(references)} unchanged CP76 vector reference')

if __name__=='__main__':archive()
