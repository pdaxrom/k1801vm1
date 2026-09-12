#!/usr/bin/env python3
"""Archive verified CP64 programs, raw DEC/RTL logs and exact source manifests."""
import argparse
import hashlib
import json
import re
import shutil
import tarfile
from pathlib import Path
from board_common import ROOT
from check_odt_cp64 import check


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def archive(odt,rt11,cores,panel):
    hardware=check(odt)
    module=json.loads((odt/'result.json').read_text())
    target=ROOT/'tb/reports/cp64';target.mkdir(parents=True,exist_ok=True)
    source=set(module['sources'])
    source.update(hardware['hardware_source_sha256'])
    source={p for p in source if not p.startswith('generated:')}
    artifacts={}
    def copy(src,dest):
        p=target/dest;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,p)
        assert sha(src)==sha(p);artifacts[dest]=sha(p)
    counters=[]
    assert len(cores)==3
    for directory in cores+[panel]:
        record=json.loads((directory/'result.json').read_text());assert record['passed']
        for p,h in record['files'].items():assert sha(ROOT/p)==h,p
        source.update(record['files'])
        name=record.get('mode','panel')
        assert name in ('logic','sync','vendor','panel'),f'invalid test mode: {name!r}'
        for f in ('result.json','build.log','simulation.log','uart.txt'):
            copy(directory/f,'directed/'+name+'/'+f)
        if name!='panel':
            m=re.search(r'PASS CP64 monitor: (\d+) checks (\d+) commands (\d+) clocks',(directory/'simulation.log').read_text())
            assert m
            counters.append(dict(mode=name,checks=int(m[1]),commands=int(m[2]),clocks=int(m[3])))
    assert {r['mode'] for r in counters}=={'logic','sync','vendor'}
    passed=json.loads((rt11/'result.json').read_text());assert passed['passed']
    for p,h in passed['files'].items():assert sha(rt11/p)==h,p
    full=json.loads((rt11/'inputs.json').read_text())
    for p,h in full['files'].items():assert sha(ROOT/p)==h,p
    source.update(full['files'])
    source.update(str((rt11/p).relative_to(ROOT)) for p in ('tb.v','odt_symbols.vh','font.hex'))
    # Archive the exact memory-model copies compiled by the full-board run.
    # Their only wrapper disables Verilator width lint in the inherited models.
    cp63=json.loads((ROOT/'tb/reports/cp63/archive.json').read_text())
    for p in ('rtl/uj11_rom.v','tb/models/ODDRXE.v',
              'reference/lsi11/spi_fram_model.v','reference/lsi11/spi_sd_model_cp28.v'):
        assert sha(ROOT/p)==cp63['sources'][p],p
        source.add(p)
        if p.startswith('reference/'):
            frozen=rt11/Path(p).name
            assert frozen.read_text()=='/* verilator lint_off WIDTH */\n'+(ROOT/p).read_text()+'\n/* verilator lint_on WIDTH */\n'
            source.add(str(frozen.relative_to(ROOT)))
            copy(frozen,'rt11/'+frozen.name)
    for f in ('result.json','inputs.json','build.log','simulation.log','uart.txt','odt_symbols.vh','font.hex'):
        copy(rt11/f,'rt11/'+f)
    for p in (rt11/'guest').iterdir():
        if p.suffix in ('.SAV','.LST','.MAP','.OBJ','.MAC') or p.name in ('build-inputs.json','console.log'):
            copy(p,'guest/'+p.name)
    for folder in ('asm','activation'):
        for p in (odt/folder).iterdir():
            if p.suffix in ('.SAV','.LST','.MAP','.OBJ','.MAC') or p.name in ('build-inputs.json','console.log'):
                copy(p,folder+'/'+p.name)
    for f in ('ODT.BIN','payload.bin','UJMON.MAC','UJON.MAC','result.json','checks.json'):
        copy(odt/f,'module/'+f);source.add(str((odt/f).relative_to(ROOT)))
    m=re.search(r'PASS CP64 RT11 \+ panel: (\d+) checks, (\d+) clocks, (\d+) UART bytes, (\d+) HDSP frames',(rt11/'simulation.log').read_text());assert m
    counts=dict(checks=int(m[1]),clocks=int(m[2]),uart_bytes=int(m[3]),hdsp_frames=int(m[4]),asserted_rx_overruns=0)
    release=ROOT/'demos/rt11/service/cp64'
    for src,name in ((odt/'ODT.BIN','ODT.BIN'),(odt/'activation/UJON.SAV','UJON.SAV')):
        shutil.copyfile(src,release/name);assert sha(src)==sha(release/name)
    delivery=dict(requires_fpga='CP63b --debug-cp63',loader='unchanged CP62 UJLOAD ABI2',
        module=module['format'],files={p:sha(release/p) for p in ('ODT.BIN','UJON.SAV')},
        source_sha256=module['sources'],assembly_sha256=sha(odt/'asm/UJMON.LST'),activation_sha256=sha(odt/'activation/UJON.LST'))
    (release/'release.json').write_text(json.dumps(delivery,indent=2)+'\n')
    source.update(str(p.relative_to(ROOT)) for p in release.iterdir() if p.is_file())
    source.update(('tools/archive_odt_cp64.py','tools/check_odt_cp64.py','tools/rt11_build.py',
                   'tools/service_image_cp62.py','tools/board_common.py','tools/run_service_cp59.py'))
    source.update(str(p.relative_to(ROOT)) for p in (ROOT/'firmware/odt').iterdir() if p.is_file())
    source.update(p for p in json.loads((ROOT/'build/cp63-debug/inputs.json').read_text())['inputs'])
    for p in source:
        assert (ROOT/p).is_file(),p
        assert 'microasm11' not in p and not p.lower().endswith('.dsk'),p
    source_hashes={p:sha(ROOT/p) for p in sorted(source)}
    with tarfile.open(target/'source.tgz','w:gz') as tar:
        for p in sorted(source):tar.add(ROOT/p,arcname=p,recursive=False)
    with tarfile.open(target/'source.tgz') as tar:
        for p,h in source_hashes.items():assert hashlib.sha256(tar.extractfile(p).read()).hexdigest()==h,p
    artifacts['source.tgz']=sha(target/'source.tgz')
    record=dict(passed=True,source_files=source_hashes,artifacts=artifacts,core=counters,rt11=counts)
    (target/'archive.json').write_text(json.dumps(record,indent=2)+'\n')
    summary=dict(checkpoint='CP64',module=module['format'],free_slot_bytes=hardware['free_slot_bytes'],
        hardware=dict(unchanged_from='CP63b',lut=1230,ff=381,ebr=6,fmax_mhz=32.246,microinstructions=1005,new_synthesis=False),
        core=counters,panel_passed=True,rt11=counts,programmed=False,
        archive_sha256=sha(target/'archive.json'),source_archive_sha256=artifacts['source.tgz'],
        limits=['No breakpoints/STEP OVER, RAW HALT/I-O view, disassembly history or held-key repeat.',
                'No full HG exchange or physical board/key-label validation.',
                'UART wire input paced at about 1.77 ms; arbitrary continuous paste is not qualified.'])
    (ROOT/'docs/verification-cp64.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(dict(source_files=len(source),artifacts=len(artifacts),core=counters,rt11=counts),indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('odt','rt11','panel'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--core',type=Path,nargs=3,required=True)
    a=p.parse_args();archive(a.odt.resolve(),a.rt11.resolve(),[p.resolve() for p in a.core],a.panel.resolve())
