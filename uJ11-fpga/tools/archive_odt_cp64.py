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


def archive(odt,rt11,cores,panel,patch=False,cp65=False,cp66=False,breakpoints=None):
    hardware=check(odt)
    module=json.loads((odt/'result.json').read_text())
    assert sum((patch,cp65,cp66))<=1
    assert (breakpoints is not None)==cp66
    revision='cp66' if cp66 else ('cp65' if cp65 else ('cp64a' if patch else 'cp64'))
    target=ROOT/'tb/reports'/revision;target.mkdir(parents=True,exist_ok=True)
    source=set(module['sources'])
    source.update(hardware['hardware_source_sha256'])
    source={p for p in source if not p.startswith('generated:')}
    artifacts={}
    def copy(src,dest):
        p=target/dest;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,p)
        assert sha(src)==sha(p);artifacts[dest]=sha(p)
    counters=[]
    # A software-only patch still requires the full RT-11 test and the exact
    # CP63b hardware check; do not claim repeated logic/vendor tests for it.
    expected_modes={'sync'} if patch or cp65 or cp66 else {'logic','sync','vendor'}
    assert len(cores)==len(expected_modes)
    for directory in cores+[panel]+([breakpoints] if cp66 else []):
        record=json.loads((directory/'result.json').read_text());assert record['passed']
        for p,h in record['files'].items():assert sha(ROOT/p)==h,p
        source.update(record['files'])
        name=record.get('mode','breakpoints' if directory==breakpoints else 'panel')
        assert name in ('logic','sync','vendor','panel','breakpoints'),f'invalid test mode: {name!r}'
        for f in ('result.json','build.log','simulation.log','uart.txt'):
            copy(directory/f,'directed/'+name+'/'+f)
        if name not in ('panel','breakpoints'):
            m=re.search(r'PASS CP64 monitor: (\d+) checks (\d+) commands (\d+) clocks',(directory/'simulation.log').read_text())
            assert m
            counters.append(dict(mode=name,checks=int(m[1]),commands=int(m[2]),clocks=int(m[3])))
    assert {r['mode'] for r in counters}==expected_modes
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
    rt11_tag='CP66' if cp66 else ('CP65' if cp65 else 'CP64')
    m=re.search(r'PASS '+rt11_tag+r' RT11 \+ panel: (\d+) checks, (\d+) clocks, (\d+) UART bytes, (\d+) HDSP frames',(rt11/'simulation.log').read_text());assert m
    counts=dict(checks=int(m[1]),clocks=int(m[2]),uart_bytes=int(m[3]),hdsp_frames=int(m[4]),asserted_rx_overruns=0)
    release=ROOT/'demos/rt11/service'/revision;release.mkdir(parents=True,exist_ok=True)
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
    summary=dict(checkpoint=revision.upper().replace('64A','64a'),module=module['format'],free_slot_bytes=hardware['free_slot_bytes'],
        hardware=dict(unchanged_from='CP63b',lut=1230,ff=381,ebr=6,fmax_mhz=32.246,microinstructions=1005,new_synthesis=False),
        core=counters,panel_passed=True,rt11=counts,programmed=False,
        archive_sha256=sha(target/'archive.json'),source_archive_sha256=artifacts['source.tgz'],
        limits=['No breakpoints/STEP OVER, RAW HALT/I-O view, disassembly history or held-key repeat.',
                'No full HG exchange; see board-bringup-cp64.md for physical validation separate from this simulation archive.',
                'UART wire input paced at about 1.77 ms; arbitrary continuous paste is not qualified.'])
    panel_record=json.loads((panel/'result.json').read_text())
    summary['panel']={k:panel_record[k] for k in ('checks','clocks','windows') if k in panel_record}
    if cp65 or cp66:
        timing=re.search(rt11_tag+r' AUTO timing at nominal 29.56 MHz: initial (\d+) clocks, interior (\d+) clocks',(rt11/'simulation.log').read_text());assert timing
        summary['auto_scroll_timing']=dict(initial_clocks=int(timing[1]),interior_clocks=int(timing[2]),nominal_cpu_mhz=29.56,
                                           initial_ms=int(timing[1])/29560,interior_ms=int(timing[2])/29560,source='full RTL FRAM simulation, not a physical stopwatch measurement')
        summary['limits']=['STEP OVER is a bounded CP63 STEP loop: ordinary IRQ/trace is deferred; WAIT/HALT stops it.',
                           'PREV uses 32 known viewed boundaries, never guesses before the first retained entry.',
                           'No USER RAM breakpoint patches, RAW HALT/I-O view, or held-key repeat.',
                           'Physical CP65 software deployment is separate; UART wire input remains paced at about 1.77 ms.']
    if cp66:
        bp=json.loads((breakpoints/'result.json').read_text())
        summary['breakpoints']={k:bp[k] for k in ('checks','clocks')}
        summary['limits']=['Four persistent HALT patches plus one temporary point; even USER RAM 001000..157776, original HALT rejected.',
                           'Normal IRQ/trace during T/U; a current-PC point or recursive inner return needs one real STEP before rearming.',
                           'Short RESET cancels free RUN. Legacy T limit defers IRQ/trace and uses UART/panel cancellation.',
                           'Self-modifying/overlay code is not supported; changed nonzero words are preserved, same-value HALT writes cannot be detected.',
                           'Native loader calls disarm all patches; cold reset ends the session and requires program reload.',
                           'No full HG session, RAW HALT/I-O view or unpaced UART burst qualification. CP66 is not installed on the board.']
    (ROOT/'docs'/('verification-'+revision+'.json')).write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(dict(source_files=len(source),artifacts=len(artifacts),core=counters,rt11=counts),indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('odt','rt11','panel'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--core',type=Path,nargs='+',required=True)
    p.add_argument('--patch',action='store_true',help='archive software-only CP64a separately; sync core, panel and full RT-11 required')
    p.add_argument('--cp65',action='store_true',help='archive CP65 panel/navigation/STEP OVER; unchanged hardware, sync core and full RT-11 required')
    p.add_argument('--cp66',action='store_true',help='CP66 software breakpoints, directed failure tests and full RT-11 IRQ/disk run')
    p.add_argument('--breakpoints',type=Path,help='required CP66 breakpoint test result directory')
    a=p.parse_args();archive(a.odt.resolve(),a.rt11.resolve(),[p.resolve() for p in a.core],a.panel.resolve(),patch=a.patch,cp65=a.cp65,cp66=a.cp66,breakpoints=a.breakpoints.resolve() if a.breakpoints else None)
