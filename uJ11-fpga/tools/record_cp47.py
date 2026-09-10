#!/usr/bin/env python3
"""Archive CP47 local verification while the remote area gate is pending."""
import json
import re
import shutil
import tarfile
from board_common import ROOT
from record_cp33 import digest,synthesis
from record_cp36 import board_counts
from record_cp37 import checked,failed_fit


def main():
    assert not list((ROOT/'synth/reports').glob('cp47*')), 'update evidence for actual synthesis before recording measured CP47'
    sources={'tools/record_cp47.py','tools/record_cp33.py','tools/record_cp36.py','tools/record_cp37.py',
             'tools/checkpoint_fram_cp47.py','rtl/uj11_engine.v','rtl/uj11_decode_rom.v','rtl/uj11_mem.v'}
    for name in ('cp40h','cp43d','cp44e','cp45k'):
        old=json.loads((ROOT/f'synth/reports/{name}/inputs.json').read_text())['files']
        for path,h in old.items():
            if not path.startswith('generated:') and path.endswith(('.v','.mem','.lpf','.sty')):
                assert digest(ROOT/path)==h,(name,path,'baseline hardware changed')
    build=checked('cp47-fram/inputs.json');sources.update(build['inputs_sha256'])
    for path,h in build['outputs_sha256'].items():assert digest(ROOT/path)==h;sources.add(path)
    proof=checked('cp47-proof.json');sources.update(proof['inputs_sha256'])
    units=checked('cp47-units.json');sources.update(units['inputs_sha256'])
    negative=checked('cp47-negative.json');sources.update(negative['inputs_sha256'])
    logs=['cp47-fram/inputs.json','cp47-proof.json','cp47-units.json','cp47-negative.json']
    assert len(proof['tests'])==8 and len(units['tests'])==18 and len(negative['tests'])==2
    for test in proof['tests']:
        name=test['tag']+'.log';logs.append(name);text=(ROOT/'build'/name).read_text()
        if test['defect']=='none':
            success='Induction step proven: SUCCESS!' if test['qualified_data'] else 'Equivalence successfully proven!'
            assert test['returncode']==0 and success in text
        else: assert test['returncode']!=0 and ('proof did fail' in text or 'unproven' in text)
    for test in units['tests']:
        tag=test['tag'];logs.extend([tag+'.log',tag+'-build.log'])
        assert test['pass_line'] in (ROOT/f'build/{tag}.log').read_text()
        assert not (ROOT/f'build/{tag}-build.log').read_text()
    for variant in ('byte-mux','shared-rx','combined'):
        name=f'cp47-{variant}-lint.log';logs.append(name);text=(ROOT/'build'/name).read_text()
        assert '%Warning' not in text and '%Error' not in text
    for test in negative['tests']:
        tag=test['tag'];logs.extend([tag+'.log',tag+'-build.log'])
        assert test['returncode']!=0 and test['detected'] in (ROOT/f'build/{tag}.log').read_text()
        assert not (ROOT/f'build/{tag}-build.log').read_text()
    prefix='cp47-combined';tests={}
    for suite in ('direct-cpu-portable-4096','direct-cpu-vendor-4',
                  'direct-cpu-portable-edges','direct-cpu-vendor-edges','bus'):
        tag=prefix+'-'+suite;result=checked(tag+'.json');sources.update(result['inputs_sha256'])
        assert result['cp47_variant']=='combined'
        text=(ROOT/f'build/{tag}.log').read_text()
        assert 'FATAL' not in text and all(line in text for line in result['pass_lines'])
        old=json.loads((ROOT/f'tb/reports/cp45/cp45-narrow-rom-{suite}.json').read_text())
        assert result['pass_lines']==old['pass_lines']
        tests[suite]=result['pass_lines'];logs.extend(tag+s for s in ('.json','.log','-build.log'))
    board=checked(prefix+'-board-verified.json','files');sources.update(board['files'])
    assert board['cp47_variant']=='combined' and board['cp47_suite']=='board'
    text=(ROOT/f'build/{prefix}-board-rt11.log').read_text()
    old=(ROOT/'tb/reports/cp45/cp45-narrow-rom-board-rt11.log').read_text()
    counts=board_counts(text);assert counts==board_counts(old)
    mmu=re.search(r'^MMU COUNTS .*$',text,re.M)[0]
    assert mmu==re.search(r'^MMU COUNTS .*$',old,re.M)[0]
    assert digest(ROOT/f'build/{prefix}-uart.txt')==digest(ROOT/'tb/reports/cp45/cp45-narrow-rom-uart.txt')
    assert board['image_sha256']==digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    logs.extend(prefix+s for s in ('-board-verified.json','-board-inputs.json','-board-build.log','-board-rt11.log','-uart.txt'))
    xm=ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha='9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm)==xm_sha and xm.stat().st_size==27540480
    for folder in (prefix+'-test-reference',prefix+'-bus',prefix+'-board-reference'):
        sources.update(str(p.relative_to(ROOT)) for p in (ROOT/'build'/folder).glob('*.v'))
    out=ROOT/'tb/reports/cp47';out.mkdir(parents=True,exist_ok=True)
    for path in logs:
        dest=out/path;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/'build'/path,dest)
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for path in sorted(sources):
            if path.startswith(('/', 'build/vendor/', 'synth/reports/', 'tb/reports/')):continue
            archive.add(ROOT/path,arcname='reference-repository/'+path[3:] if path.startswith('../') else path)
    report=dict(checkpoint='CP47 local FRAM candidates verified; synthesis pending',date='2026-09-10',
        synthesis_complete=False,synthesis={},synthesis_pending_reason='CP47 source transfer rejected by automatic approval review; explicit approval requested and not received.',
        candidate_resources=None,candidate_fmax_mhz=None,adopted=False,retained_candidate='cp45k',
        baseline_cp45k=failed_fit('cp45k'),production_cp40h=synthesis('cp40h'),accepted_apr_cp43d=synthesis('cp43d'),
        baseline_hardware_unchanged=True,microcode_words=954,native_cpu_and_mmu_unchanged=True,
        interface_contract='SPI/ACK/error/busy equal each cycle; byte-mux rdata equal each cycle; shared-rx/combined rdata equal when ready or idle, high byte scratch while busy.',
        formal_positive_runs=6,formal_rejected_mutations=2,simulation_rejected_mutations=2,
        unit_runs=18,unit_dividers=[1,2,3],formal_dividers=[1,3],tests=tests,
        cold_rt11fb=counts,cold_rt11fb_mmu_line=mmu,cold_counts_and_uart_match_cp45=True,
        fb_image_sha256=board['image_sha256'],fis_full_corpus_rerun=False,
        rt11_xm=dict(boot_tested=False,image_modified=False,sha256=xm_sha),physical_board='CP29a',board_programmed=False,
        sources_sha256={p:digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted(out.rglob('*')) if p.is_file()},
        limits=['No CP47 synthesis/MAP/PAR/TRACE was run; do not infer LUT/FF savings or Fmax from RTL.',
                'Shared-rx deliberately changes high rdata while busy; only use with ready-qualified consumers.',
                'SAT induction begins with a reset; all subsequent inputs including reset are unconstrained two-state signals.',
                'Four-state simulation uses known control/address and X/Z payload/MISO; it does not model analog SPI timing.',
                'No new MMU functionality: kernel unified PAR only, no PDR protection/W, MMR1/2 or abort/restart.',
                'RT-11XM and active-MMU private RK/high DMA remain untested.'])
    (ROOT/'docs/verification-cp47.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP47 local evidence: formal/mutations/18 unit runs/CPU/bus/cold FB; synthesis explicitly pending')


if __name__=='__main__':main()
