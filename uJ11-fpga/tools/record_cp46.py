#!/usr/bin/env python3
"""Archive exact CP46 rejected gates, functional evidence and final EDIF audits."""
import gzip
import json
import re
import shutil
import tarfile
from board_common import ROOT
from record_cp33 import digest, synthesis
from record_cp36 import board_counts
from record_cp37 import checked, failed_fit
from check_edif_drivers_cp46 import audit


def main():
    fits = {f'cp46{x}': failed_fit(f'cp46{x}') for x in 'abcde'}
    assert [(r['lut4'],r['ff'],r['slices']) for r in fits.values()] == [
        (1316,356,659),(1315,359,661),(1317,359,661),(1317,356,659),(1302,359,652)]
    assert fits['cp46e']==failed_fit('cp45k')
    assert all(r['ebr']==7 for r in fits.values())
    sources = {'tools/record_cp46.py','tools/record_cp33.py','tools/record_cp36.py',
               'tools/record_cp37.py','tools/archive_synthesis.py','tools/check_edif_drivers_cp46.py'}
    for name in fits:
        inputs = json.loads((ROOT/f'synth/reports/{name}/inputs.json').read_text())['files']
        for path, expected in inputs.items():
            if path.startswith('generated:'): continue
            assert digest(ROOT/path)==expected, (name,path,'synthesis input changed')
            sources.add(path)
    for name in ('cp40h','cp43d','cp44e','cp45k'):
        old = json.loads((ROOT/f'synth/reports/{name}/inputs.json').read_text())['files']
        for path, expected in old.items():
            if not path.startswith('generated:') and path.endswith(('.v','.mem','.lpf','.sty')):
                assert digest(ROOT/path)==expected, (name,path,'baseline hardware changed')
    build = checked('cp46-control/inputs.json'); sources.update(build['inputs_sha256'])
    for path, expected in build['outputs_sha256'].items():
        assert digest(ROOT/path)==expected; sources.add(path)
    logs = ['cp46-control/inputs.json','cp46-proof.json','cp46-units.json',
            'cp46-oracle.log','cp46-oracle-build.log','cp46-oracle-lint.log',
            'cp46-four-state-classified.log','cp46-four-state-classified-build.log']
    proof = checked('cp46-proof.json'); sources.update(proof['inputs_sha256'])
    assert len(proof['tests'])==6
    points = dict(bridge=70,apr=55,bus=51)
    for test in proof['tests']:
        path = test['tag']+'.log'; logs.append(path); text = (ROOT/'build'/path).read_text()
        if test['defect']=='none':
            assert test['returncode']==0 and f"{points[test['kind']]} are proven and 0 are unproven" in text
        else: assert test['returncode']!=0 and 'unproven' in text
    units = checked('cp46-units.json'); sources.update(units['inputs_sha256'])
    assert '262144 addresses, 196608 stalled lookup edges' in units['oracle_pass_line']
    assert units['four_state']['cases']==131072 and units['four_state']['returncode']==0
    assert not (ROOT/'build/cp46-four-state-classified-build.log').read_text()
    prefix = 'cp46-combined'; tests = {}
    for suite in ('direct-cpu-portable-4096','direct-cpu-vendor-4',
                  'direct-cpu-portable-edges','direct-cpu-vendor-edges','bus',
                  'apr-port-portable','apr-port-vendor'):
        tag = prefix+'-'+suite; result = checked(tag+'.json'); sources.update(result['inputs_sha256'])
        assert result['cp46_variant']=='combined'
        text = (ROOT/f'build/{tag}.log').read_text()
        assert 'FATAL' not in text and all(line in text for line in result['pass_lines'])
        if suite.startswith('apr-'):
            assert '1144373 commands' in result['pass_lines'][0]
            assert '2288746 coherent lookup reads' in result['pass_lines'][1]
        else:
            prior = json.loads((ROOT/f'tb/reports/cp45/cp45-narrow-rom-{suite}.json').read_text())
            assert result['pass_lines']==prior['pass_lines'],suite
        tests[suite] = result['pass_lines']; logs.extend(tag+s for s in ('.json','.log','-build.log'))
    board = checked(prefix+'-board-verified.json','files'); sources.update(board['files'])
    assert board['cp46_variant']=='combined' and board['cp46_suite']=='board'
    text = (ROOT/f'build/{prefix}-board-rt11.log').read_text()
    old = (ROOT/'tb/reports/cp45/cp45-narrow-rom-board-rt11.log').read_text()
    counts = board_counts(text); assert counts==board_counts(old)
    mmu = re.search(r'^MMU COUNTS .*$',text,re.M)[0]
    assert mmu==re.search(r'^MMU COUNTS .*$',old,re.M)[0]
    assert digest(ROOT/f'build/{prefix}-uart.txt')==digest(ROOT/'tb/reports/cp45/cp45-narrow-rom-uart.txt')
    assert board['image_sha256']==digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    logs.extend(prefix+s for s in ('-board-verified.json','-board-inputs.json','-board-build.log','-board-rt11.log','-uart.txt'))
    xm = ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha = '9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm)==xm_sha and xm.stat().st_size==27540480
    for folder in (prefix+'-test-reference',prefix+'-bus'):
        sources.update(str(p.relative_to(ROOT)) for p in (ROOT/'build'/folder).glob('*.v'))
    out = ROOT/'tb/reports/cp46'; out.mkdir(parents=True,exist_ok=True)
    netlists = {}
    for choice,name in [('baseline','cp45k'),('combined','cp46d')]:
        path = ROOT/f'build/cp46-{choice}.edi'
        result = json.loads((ROOT/f'build/cp46-{choice}-drivers.json').read_text())
        current = audit(path)
        assert current=={k:v for k,v in result.items() if k!='negative_controls'}
        assert not current['multiple_drivers'] and not current['floating']
        assert len(current['proven_unused_carry_inputs'])==7
        assert len(result['negative_controls'])==3
        for test in result['negative_controls']:
            folder = 'cp46-edif-negative' if choice=='baseline' else 'cp46-combined-edif-negative'
            mutated = ROOT/'build'/folder/(test['mutation']+'.edi')
            assert audit(mutated)==test['result'] and test['result'][test['detected']]
            (out/f'{name}-{test["mutation"]}.edi.gz').write_bytes(gzip.compress(mutated.read_bytes(),mtime=0))
        (out/(name+'.edi.gz')).write_bytes(gzip.compress(path.read_bytes(),mtime=0))
        logs.append(f'cp46-{choice}-drivers.json')
        netlists[name]=result
    # Vendor model is referenced for the masking equation; do not redistribute it.
    vendor_model='build/vendor/CCU2D.v'; sources.add(vendor_model)
    for path in logs:
        dest=out/path; dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/'build'/path,dest)
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for path in sorted(sources):
            if path.startswith(('/', 'build/vendor/', 'synth/reports/', 'tb/reports/')) or path.endswith('-oracle.txt'): continue
            archive.add(ROOT/path,arcname='reference-repository/'+path[3:] if path.startswith('../') else path)
    report=dict(checkpoint='CP46: control/region alternatives rejected; CP45k retained',date='2026-09-10',
        synthesis=fits,baseline_cp45k=failed_fit('cp45k'),retained_candidate='cp45k',adopted=False,
        production_cp40h=synthesis('cp40h'),accepted_apr_cp43d=synthesis('cp43d'),
        all_synthesis_inputs_match_current=True,baseline_hardware_unchanged=True,control_cp46e_matches_cp45k=True,
        retained_resources_delta=dict(lut4=0,ff=0,ebr=0,slices=0,clocks=0),
        retained_over_capacity=dict(lut4=22,slices=12),fmax_mhz=None,microcode_words=954,
        equivalence_points=points,detected_rtl_mutations=3,four_state=units['four_state'],
        oracle=units['oracle_pass_line'],tests=tests,cold_rt11fb=counts,cold_rt11fb_mmu_line=mmu,
        cold_counts_and_uart_match_cp45=True,netlists=netlists,vendor_carry_model_sha256=digest(ROOT/vendor_model),
        fb_image_sha256=board['image_sha256'],fis_full_corpus_rerun=False,
        rt11_xm=dict(boot_tested=False,image_modified=False,sha256=xm_sha),
        physical_board='CP29a',board_programmed=False,
        sources_sha256={p:digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted(out.rglob('*')) if p.is_file()},
        archived_synthesis_sha256={str(p.relative_to(ROOT)):digest(p) for n in fits for p in sorted((ROOT/'synth/reports'/n).iterdir())},
        limits=['Four alternatives failed MAP and increased LUT usage; control CP46e reproduces CP45k. None adopted.',
                'No new ISA/MMU functionality; only kernel unified PAR relocation, no PDR protection/W or MMR1/2/abort/restart.',
                'Bus four-state proof uses known control/address/state and arbitrary X/Z device data.',
                'APR proof observes RAM address only when enabled and write data only when its lane is enabled.',
                'Final EDIF audits count directional drivers; INOUT nets are counted separately, not electrically proven.',
                'CIN masks follow Lattice CCU2D model; no default zero or blanket undriven-pin waiver.',
                'Netlist audit is structural, not gate-level CPU equivalence or routed hardware validation.',
                'BN161 logs are capped at 100 displayed warnings; actual total is not inferred from that cap.',
                'RT-11XM, active-MMU private RK transfers and high DMA remain untested.'])
    (ROOT/'docs/verification-cp46.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP46 evidence: four rejected fits, all functional regressions, final EDIF audits and negative controls; CP45 retained')


if __name__=='__main__': main()
