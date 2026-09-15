#!/usr/bin/env python3
"""Freeze the CP78 functional candidate; never imply a completed HC1200 gate."""
import hashlib,json,shutil,tarfile
from board_common import ROOT

TARGET=ROOT/'tb/reports/cp78'
RUNS={**{f'psw-d{d}-a{a}':f'cp78-tests/rtl-d{d}-a{a}' for d in (0,1) for a in (0,1)},
      'psw-vendor':'cp78-tests/vendor-d1-a1',
      'events-sync':'cp78-fp-events/sync','events-vendor':'cp78-fp-events/vendor',
      'dec-a':'cp78-diagnostics/FFPAA1','dec-b':'cp78-diagnostics/FFPBA0',
      'dec-c':'cp78-diagnostics/FFPCB0','rt11':'cp78-rt11'}

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())

def archive():
    assert not (TARGET/'archive.json').exists(),'immutable archive; use a new checkpoint'
    TARGET.mkdir(parents=True,exist_ok=True)
    sources={};artifacts={};results={}
    def source(p,expected=None):
        p=p.resolve()
        if p.suffix.lower() in ('.dsk','.img','.iso'):return
        assert p.is_relative_to(ROOT),'only uJ11 project sources are archived'
        name=str(p.relative_to(ROOT));digest=sha(p)
        assert expected is None or expected==digest,name
        sources[name]=dict(sha256=digest,member='project/'+name)
    def collect(record):
        if isinstance(record,dict):
            for k,v in record.items():
                if isinstance(v,str) and len(v)==64 and '/' in k and (ROOT/k).is_file():source(ROOT/k,v)
                elif isinstance(v,(dict,list)):collect(v)
        elif isinstance(record,list):
            for v in record:collect(v)
    def artifact(p,name):
        dest=TARGET/name;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(p,dest);artifacts[name]=sha(dest)
    for label,folder in RUNS.items():
        d=ROOT/'build'/folder;r=read(d/'result.json');assert r['passed'],label
        if label.startswith('dec-'):assert r['hardware']=='cp78' and not r['psw_read_adapter']
        collect(r)
        if (d/'inputs.json').exists():collect(read(d/'inputs.json'))
        results[label]={k:r[k] for k in ('passed','cases','checks','clocks','uart_bytes','psw_read_adapter','hardware','vendor','decode','aligned') if k in r}
        for name in ('result.json','inputs.json','simulation.log','uart.txt','build.log'):
            if (d/name).exists():artifact(d/name,label+'/'+name)
    old_uart=(ROOT/'tb/reports/cp77/rt11/uart.txt').read_bytes()
    new_uart=(ROOT/'build/cp78-rt11/uart.txt').read_bytes()
    old_date=b'FPTST .SAV     3  14-Sep-2026';new_date=b'FPTST .SAV     3  15-Sep-2026'
    assert old_uart.count(old_date)==1 and new_uart.count(new_date)==1
    assert old_uart.replace(old_date,new_date)==new_uart
    source(ROOT/'tb/reports/cp77/rt11/uart.txt')
    hw=read(ROOT/'build/cp78-psw/inputs.json');collect(hw)
    source(ROOT/'build/cp78-psw/inputs.json')
    candidate=read(ROOT/'build/cp78a/inputs.json')
    for p,h in candidate['files'].items():
        if not p.startswith('generated:'):source(ROOT/p,h)
    for name in ('inputs.json','clock.lpf','build.tcl','cp78a.ldf'):artifact(ROOT/'build/cp78a'/name,'prepared/'+name)
    # Verify the sole intended differences against the exact CP67b hardware.
    old=read(ROOT/'synth/reports/cp67b/inputs.json');changes=[]
    with tarfile.open(ROOT/'synth/reports/cp67b/source.tgz') as t:
        for p,h in hw['outputs'].items():
            original=p.replace('build/cp78-psw/','build/cp67-modules/')
            data=t.extractfile(original).read();assert hashlib.sha256(data).hexdigest()==old['files'][original]
            data=data.replace(b'build/cp67-modules/',b'build/cp78-psw/')
            if data!=(ROOT/p).read_bytes():changes.append(p.removeprefix('build/cp78-psw/'))
    assert sorted(changes)==['src/rtl/uj11_core.v','src/rtl/uj11_engine.v','src/rtl/uj11_psw.v'],changes
    # FP and ODT images used by tests are exactly the released CP77 modules.
    for src,release in [('build/cp77-odt/ODT.BIN','demos/rt11/service/cp77/ODT.BIN'),
                        ('build/cp77-fp11/software/FP11.BIN','demos/rt11/service/cp77/FP11.BIN')]:
        assert sha(ROOT/src)==sha(ROOT/release);source(ROOT/src);source(ROOT/release)
    extraction=read(ROOT/'build/cp77-diagnostics/extraction.json')
    assert sha(ROOT/extraction['disk'])==extraction['disk_sha256']
    artifact(ROOT/'build/cp77-diagnostics/extraction.json','diagnostics/extraction.json')
    for e in extraction['entries']:
        for name,key in ((e['name'],'file_sha256'),(e['name']+'.ram','ram_sha256')):
            p=ROOT/'build/cp77-diagnostics'/name;assert sha(p)==e[key];artifact(p,'diagnostics/'+name)
    for p in (ROOT/'tools').glob('*cp78.py'):source(p)
    for p in (ROOT/'rtl/cp78').glob('*.v'):source(p)
    for name in ('tb/tb_psw_cp78.v','docs/psw-cp78.md','README.md','TODO.md','docs/README.md','docs/implementation-status.md','docs/synthesis.md','docs/benchmarks.md'):source(ROOT/name)
    with tarfile.open(TARGET/'source.tgz','w:gz') as t:
        for p,entry in sorted(sources.items()):t.add(ROOT/p,arcname=entry['member'],recursive=False)
    artifacts['source.tgz']=sha(TARGET/'source.tgz')
    result=dict(checkpoint='CP78',functional_pass=True,synthesis_completed=False,installed_on_board=False,
                qualification='Functional candidate only; native DEC passes without PSW adapter; HC1200 gate pending',
                synthesis_blocker='Automatic approval review denied source transfer twice; renewed trusted-host confirmation pending',
                source_files=sources,artifacts=artifacts,results=results,hardware_changes=changes,
                microcode_words=1005,expected_ebr=7,diagnostics=extraction)
    (TARGET/'archive.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'CP78: {len(sources)} sources, {len(artifacts)} artifacts; functional PASS, synthesis pending')

if __name__=='__main__':archive()
