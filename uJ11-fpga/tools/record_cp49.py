#!/usr/bin/env python3
"""Archive CP49 local checks while explicit source-transfer approval is pending."""
import json
import re
import shutil
import tarfile
from board_common import ROOT
from record_cp33 import digest, synthesis
from record_cp36 import board_counts
from record_cp37 import checked, failed_fit


def main():
    assert not list((ROOT/'synth/reports').glob('cp49*')), 'update evidence for actual synthesis before recording local-only CP49'
    fits={}
    sources={'tools/record_cp49.py','tools/record_cp33.py','tools/record_cp36.py',
             'tools/record_cp37.py','tools/checkpoint_fram_binary_cp49.py'}
    for name in ('cp40h','cp43d','cp45k','cp47c'):
        old=json.loads((ROOT/f'synth/reports/{name}/inputs.json').read_text())['files']
        for path,h in old.items():
            if not path.startswith('generated:') and path.endswith(('.v','.mem','.lpf','.sty')):
                assert digest(ROOT/path)==h,(name,path,'baseline hardware changed')
    build=checked('cp49-binary/inputs.json');sources.update(build['inputs_sha256'])
    for path,h in build['outputs_sha256'].items():assert digest(ROOT/path)==h;sources.add(path)
    proof=checked('cp49-proof.json');sources.update(proof['inputs_sha256'])
    units=checked('cp49-units.json');sources.update(units['inputs_sha256'])
    negative=checked('cp49-negative.json');sources.update(negative['inputs_sha256'])
    assert len(proof['tests'])==9 and len(units['tests'])==18 and len(negative['tests'])==3
    logs=['cp49-binary/inputs.json','cp49-proof.json','cp49-units.json','cp49-negative.json']
    for test in proof['tests']:
        name=test['tag']+'.log';logs.append(name);text=(ROOT/'build'/name).read_text()
        if test['defect']=='none':assert test['returncode']==0 and 'Induction step proven: SUCCESS!' in text
        else:assert test['returncode']!=0 and 'proof did fail' in text
    for test in units['tests']:
        tag=test['tag'];logs.extend([tag+'.log',tag+'-build.log'])
        assert test['pass_line'] in (ROOT/f'build/{tag}.log').read_text()
        assert not (ROOT/f'build/{tag}-build.log').read_text()
    for variant in ('original','split-low','equations'):
        name=f'cp49-{variant}-lint.log';logs.append(name);text=(ROOT/'build'/name).read_text()
        assert '%Warning' not in text and '%Error' not in text
    for test in negative['tests']:
        tag=test['tag'];logs.extend([tag+'.log',tag+'-build.log'])
        assert test['returncode']!=0 and test['detected'] in (ROOT/f'build/{tag}.log').read_text()
        assert not (ROOT/f'build/{tag}-build.log').read_text()
    integration={}
    for variant in ('original','split-low','equations'):
        prefix='cp49-'+variant;tests={}
        for suite in ('direct-cpu-portable-4096','direct-cpu-vendor-4',
                      'direct-cpu-portable-edges','direct-cpu-vendor-edges','bus'):
            tag=prefix+'-'+suite;result=checked(tag+'.json');sources.update(result['inputs_sha256'])
            assert result['cp49_variant']==variant
            text=(ROOT/f'build/{tag}.log').read_text()
            assert 'FATAL' not in text and result['pass_lines'] and all(line in text for line in result['pass_lines'])
            old=json.loads((ROOT/f'tb/reports/cp47/cp47-shared-rx-{suite}.json').read_text())
            assert result['pass_lines']==old['pass_lines']
            tests[suite]=result['pass_lines'];logs.extend(tag+s for s in ('.json','.log','-build.log'))
        integration[variant]=tests
        for folder in (prefix+'-test-reference',prefix+'-bus'):
            sources.update(str(p.relative_to(ROOT)) for p in (ROOT/'build'/folder).glob('*.v'))
    prefix='cp49-split-low'
    board=checked(prefix+'-board-verified.json','files');sources.update(board['files'])
    assert board['cp49_variant']=='split-low' and board['cp49_suite']=='board'
    text=(ROOT/f'build/{prefix}-board-rt11.log').read_text()
    old=(ROOT/'tb/reports/cp47/cp47-shared-rx-board-rt11.log').read_text()
    counts=board_counts(text);assert counts==board_counts(old)
    mmu=re.search(r'^MMU COUNTS .*$',text,re.M)[0]
    assert mmu==re.search(r'^MMU COUNTS .*$',old,re.M)[0]
    assert digest(ROOT/f'build/{prefix}-uart.txt')==digest(ROOT/'tb/reports/cp47/cp47-shared-rx-uart.txt')
    assert board['image_sha256']==digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    logs.extend(prefix+s for s in ('-board-verified.json','-board-inputs.json','-board-build.log','-board-rt11.log','-uart.txt'))
    sources.update(str(p.relative_to(ROOT)) for p in (ROOT/'build'/f'{prefix}-board-reference').glob('*.v'))
    xm=ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha='9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm)==xm_sha and xm.stat().st_size==27540480
    out=ROOT/'tb/reports/cp49';out.mkdir(parents=True,exist_ok=True)
    for path in logs:
        dest=out/path;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/'build'/path,dest)
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for path in sorted(sources):
            if path.startswith(('/', 'build/vendor/', 'synth/reports/', 'tb/reports/')):continue
            archive.add(ROOT/path,arcname='reference-repository/'+path[3:] if path.startswith('../') else path)
    report=dict(checkpoint='CP49: binary FRAM state locally verified; synthesis pending',date='2026-09-11',
        synthesis=fits,synthesis_complete=False,synthesis_pending_reason='Explicit approval for CP49 payload transfer has been requested after automatic review rejection; not received.',
        adopted=False,retained_candidate='cp47c',retained_resources=failed_fit('cp47c'),candidate_resources=None,candidate_fmax_mhz=None,
        over_capacity_retained=dict(lut4=17,slices=10),
        production_cp40h=synthesis('cp40h'),accepted_apr_cp43d=synthesis('cp43d'),
        baseline_hardware_unchanged=True,microcode_words=954,native_cpu_and_mmu_unchanged=True,
        interface_contract='Every output including busy rdata matches CP47c every cycle after reset; no illegal-state injection claim.',
        formal_positive_runs=6,formal_rejected_mutations=3,simulation_rejected_mutations=3,
        unit_runs=18,unit_dividers=[1,2,3],formal_dividers=[1,3],integration_variants=integration,
        cold_rt11fb_variant='split-low',cold_rt11fb=counts,cold_rt11fb_mmu_line=mmu,
        cold_counts_and_uart_match_cp47=True,fb_image_sha256=board['image_sha256'],fis_full_corpus_rerun=False,
        rt11_xm=dict(boot_tested=False,image_modified=False,sha256=xm_sha),physical_board='CP29a',board_programmed=False,
        sources_sha256={p:digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted(out.rglob('*')) if p.is_file()},
        archived_synthesis_sha256={str(p.relative_to(ROOT)):digest(p) for name in fits for p in sorted((ROOT/'synth/reports'/name).iterdir())},
        limits=['CP49 synthesis has not run; no measured candidate LUT/FF/EBR/Fmax.',
                'No variant adopted: CP47c and production/APR/physical board remain unchanged.',
                'Sequential induction starts with reset; all later inputs, including reset, are unconstrained two-state values.',
                'Canonical-state and active-only-in-serial-state invariants are proved jointly, not assumed.',
                'Four-state simulation has known control/address and X/Z payload/MISO, not analog SPI timing.',
                'Only split-low has a new cold FB run; all three variants have CPU/vendor/bus tests.',
                'No new MMU functionality: kernel unified PAR only, no PDR protection/W, MMR1/2 or abort/restart.',
                'RT-11XM and active-MMU private RK/high DMA remain untested.'])
    (ROOT/'docs/verification-cp49.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP49 local evidence: formal/unit/CPU/bus/cold FB; synthesis explicitly pending')


if __name__=='__main__':main()
