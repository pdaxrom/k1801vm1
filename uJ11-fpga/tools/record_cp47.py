#!/usr/bin/env python3
"""Bind CP47 FRAM area measurements to unchanged RTL and passing regressions."""
import gzip
import json
import re
import shutil
import tarfile
from board_common import ROOT
from record_cp33 import digest,synthesis
from record_cp36 import board_counts
from record_cp37 import checked,failed_fit
from check_edif_drivers_cp46 import audit


def main():
    fits={f'cp47{x}':failed_fit(f'cp47{x}') for x in 'abcd'}
    assert [(r['lut4'],r['ff'],r['ebr'],r['slices']) for r in fits.values()]==[
        (1302,359,7,652),(1326,359,7,665),(1297,351,7,650),(1317,351,7,660)]
    assert fits['cp47a']==failed_fit('cp45k')
    sources={'tools/record_cp47.py','tools/record_cp33.py','tools/record_cp36.py','tools/record_cp37.py',
             'tools/checkpoint_fram_cp47.py','tools/check_edif_drivers_cp46.py','tools/archive_synthesis.py',
             'build/vendor/CCU2D.v','rtl/uj11_engine.v','rtl/uj11_decode_rom.v','rtl/uj11_mem.v'}
    for name in fits:
        inputs=json.loads((ROOT/f'synth/reports/{name}/inputs.json').read_text())['files']
        for path,h in inputs.items():
            if not path.startswith('generated:'):
                assert digest(ROOT/path)==h,(name,path,'synthesis input changed')
                sources.add(path)
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
    integration={}
    for variant in ('combined','shared-rx'):
        prefix='cp47-'+variant;tests={}
        for suite in ('direct-cpu-portable-4096','direct-cpu-vendor-4',
                      'direct-cpu-portable-edges','direct-cpu-vendor-edges','bus'):
            tag=prefix+'-'+suite;result=checked(tag+'.json');sources.update(result['inputs_sha256'])
            assert result['cp47_variant']==variant
            text=(ROOT/f'build/{tag}.log').read_text()
            assert 'FATAL' not in text and all(line in text for line in result['pass_lines'])
            old=json.loads((ROOT/f'tb/reports/cp45/cp45-narrow-rom-{suite}.json').read_text())
            assert result['pass_lines']==old['pass_lines']
            tests[suite]=result['pass_lines'];logs.extend(tag+s for s in ('.json','.log','-build.log'))
        board=checked(prefix+'-board-verified.json','files');sources.update(board['files'])
        assert board['cp47_variant']==variant and board['cp47_suite']=='board'
        text=(ROOT/f'build/{prefix}-board-rt11.log').read_text()
        old=(ROOT/'tb/reports/cp45/cp45-narrow-rom-board-rt11.log').read_text()
        counts=board_counts(text);assert counts==board_counts(old)
        mmu=re.search(r'^MMU COUNTS .*$',text,re.M)[0]
        assert mmu==re.search(r'^MMU COUNTS .*$',old,re.M)[0]
        assert digest(ROOT/f'build/{prefix}-uart.txt')==digest(ROOT/'tb/reports/cp45/cp45-narrow-rom-uart.txt')
        assert board['image_sha256']==digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')
        logs.extend(prefix+s for s in ('-board-verified.json','-board-inputs.json','-board-build.log','-board-rt11.log','-uart.txt'))
        integration[variant]=dict(tests=tests,cold_rt11fb=counts,mmu_line=mmu,image_sha256=board['image_sha256'])
    xm=ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha='9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm)==xm_sha and xm.stat().st_size==27540480
    for variant in ('combined','shared-rx'):
        prefix='cp47-'+variant
        for folder in (prefix+'-test-reference',prefix+'-bus',prefix+'-board-reference'):
            sources.update(str(p.relative_to(ROOT)) for p in (ROOT/'build'/folder).glob('*.v'))
    out=ROOT/'tb/reports/cp47';out.mkdir(parents=True,exist_ok=True)
    netlist=json.loads((ROOT/'build/cp47c-drivers.json').read_text())
    current=audit(ROOT/'build/cp47c.edi')
    assert current=={k:v for k,v in netlist.items() if k!='negative_controls'}
    assert not current['multiple_drivers'] and not current['floating']
    assert len(current['proven_unused_carry_inputs'])==7 and len(netlist['negative_controls'])==3
    for test in netlist['negative_controls']:
        mutated=ROOT/'build/cp47c-edif-negative'/(test['mutation']+'.edi')
        assert audit(mutated)==test['result'] and test['result'][test['detected']]
        (out/('cp47c-'+test['mutation']+'.edi.gz')).write_bytes(gzip.compress(mutated.read_bytes(),mtime=0))
    (out/'cp47c.edi.gz').write_bytes(gzip.compress((ROOT/'build/cp47c.edi').read_bytes(),mtime=0))
    logs.append('cp47c-drivers.json')
    for path in logs:
        dest=out/path;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/'build'/path,dest)
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for path in sorted(sources):
            if path.startswith(('/', 'build/vendor/', 'synth/reports/', 'tb/reports/')):continue
            archive.add(ROOT/path,arcname='reference-repository/'+path[3:] if path.startswith('../') else path)
    report=dict(checkpoint='CP47 shared FRAM RX: -5 LUT / -8 FF, partial relocation still over HC1200 capacity',date='2026-09-10',
        synthesis_complete=True,synthesis=fits,synthesis_pending_reason=None,
        candidate_resources=fits['cp47c'],candidate_fmax_mhz=None,adopted=False,retained_candidate='cp47c',
        candidate_variant='shared-rx',all_synthesis_inputs_match_current=True,control_cp47a_matches_cp45k=True,
        delta=dict(lut4=-5,ff=-8,ebr=0,slices=-2,clocks=0),over_capacity=dict(lut4=17,slices=10),
        baseline_cp45k=failed_fit('cp45k'),production_cp40h=synthesis('cp40h'),accepted_apr_cp43d=synthesis('cp43d'),
        baseline_hardware_unchanged=True,microcode_words=954,native_cpu_and_mmu_unchanged=True,
        interface_contract='SPI/ACK/error/busy equal each cycle; byte-mux rdata equal each cycle; shared-rx/combined rdata equal when ready or idle, high byte scratch while busy.',
        formal_positive_runs=6,formal_rejected_mutations=2,simulation_rejected_mutations=2,
        unit_runs=18,unit_dividers=[1,2,3],formal_dividers=[1,3],tests=tests,integration_variants=integration,netlist=netlist,
        cold_rt11fb=counts,cold_rt11fb_mmu_line=mmu,cold_counts_and_uart_match_cp45=True,
        fb_image_sha256=board['image_sha256'],fis_full_corpus_rerun=False,
        rt11_xm=dict(boot_tested=False,image_modified=False,sha256=xm_sha),physical_board='CP29a',board_programmed=False,
        sources_sha256={p:digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted(out.rglob('*')) if p.is_file()},
        archived_synthesis_sha256={str(p.relative_to(ROOT)):digest(p) for name in fits for p in sorted((ROOT/'synth/reports'/name).iterdir())},
        limits=['All CP47 full-board MAP gates exceed HC1200 capacity; PAR/TRACE/Fmax are unavailable.',
                'Shared-rx is the retained area prototype; production CP40h/APR CP43d and physical CP29a are unchanged.',
                'Final EDIF audit checks directional drivers and masked CIN, not INOUT electrical contention or routed timing.',
                'Shared-rx deliberately changes high rdata while busy; only use with ready-qualified consumers.',
                'SAT induction begins with a reset; all subsequent inputs including reset are unconstrained two-state signals.',
                'Four-state simulation uses known control/address and X/Z payload/MISO; it does not model analog SPI timing.',
                'No new MMU functionality: kernel unified PAR only, no PDR protection/W, MMR1/2 or abort/restart.',
                'RT-11XM and active-MMU private RK/high DMA remain untested.'])
    (ROOT/'docs/verification-cp47.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP47 measured evidence: -5 LUT/-8 FF, exact-source gates, shared-rx and combined regressions, EDIF audit; MAP still fails')


if __name__=='__main__':main()
