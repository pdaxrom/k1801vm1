#!/usr/bin/env python3
"""Bind CP42 APR D-input savings to synthesis, proofs and integration regressions."""
import hashlib
import json
import shutil
import tarfile
from board_common import ROOT
from record_cp33 import digest,synthesis
from record_cp36 import board_counts
from record_cp37 import checked


def main():
    fits={f'cp42{x}':synthesis(f'cp42{x}',x=='d') for x in 'abcd'}
    assert fits['cp42d']==fits['cp42b']
    old_apr=synthesis('cp40i');production=synthesis('cp40h')
    assert old_apr['lut4']-fits['cp42d']['lut4']==10
    assert fits['cp42c']['lut4']>production['lut4']
    previous=json.loads((ROOT/'synth/reports/cp40i/inputs.json').read_text())['files']
    changed=sorted(p for p,h in previous.items() if not p.startswith('generated:') and p.endswith(('.v','.mem','.lpf','.sty')) and digest(ROOT/p)!=h)
    assert changed==['build/cp39-csr/uj11_engine.v'],changed
    production_inputs=json.loads((ROOT/'synth/reports/cp40h/inputs.json').read_text())['files']
    for p,h in production_inputs.items():
        if not p.startswith('generated:') and p.endswith(('.v','.mem','.lpf','.sty')):assert digest(ROOT/p)==h,p
    build=checked('cp42-input/inputs.json')
    for p,h in build['outputs_sha256'].items():assert digest(ROOT/p)==h
    old=(ROOT/'build/cp42-input/baseline/apr/uj11_engine.v').read_text()
    now=(ROOT/'build/cp39-csr/uj11_engine.v').read_text()
    assert now==(ROOT/'build/cp42-input/sliced/apr/uj11_engine.v').read_text()
    old_start=old.index('    always @* begin\n        case (uword[12:10])')
    now_start=now.index('    wire [2:0] d_select=')
    assert old[:old_start]==now[:now_start]
    assert old[old.index('    uj11_datapath dp(',old_start):]==now[now.index('    uj11_datapath dp(',now_start):]
    assert digest(ROOT/'rtl/uj11_engine.v')==digest(ROOT/'build/cp42-input/baseline/production/uj11_engine.v')
    proof=checked('cp42-proof.json');sim=checked('cp42-sim.json')
    assert sum(not t['negative'] for t in proof['tests'])==4 and sum(t['negative'] for t in proof['tests'])==3
    sources={'tools/record_cp42.py','tools/checkpoint_d_input_cp42.py','tools/build_mmu_apr_lookup.py',
             'tools/build_mmu_apr_csr.py','tools/build_mmu_entry.py','tools/record_cp33.py','tools/record_cp36.py','tools/record_cp37.py',
             'build/cp42-proof/proof.ys','build/cp42-proof/miter.v'}
    logs=['cp42-input/inputs.json','cp42-proof.json','cp42-sim.json']
    for r in (build,proof,sim):sources.update(r['inputs_sha256'])
    sources.update(build['outputs_sha256'])
    for t in proof['tests']:
        path='cp42-proof/'+t['tag']+'.log';assert digest(ROOT/'build'/path)==t['log_sha256']
        assert (t['returncode']!=0)==t['negative'];logs.append(path)
    assert len(sim['tests'])==4
    for t in sim['tests']:
        path='cp42-sim/'+t['tag']+'.log';assert digest(ROOT/'build'/path)==t['log_sha256']
        assert '135168 comparisons' in t['pass_line'] and t['pass_line'] in (ROOT/'build'/path).read_text()
        logs.extend('cp42-sim/'+t['tag']+s for s in ('.log','-build.log'))
    lint=checked('cp42-lint.json');assert lint['configurations']==4
    lint_log=(ROOT/'build/cp42-lint.log').read_text();assert '%Warning' not in lint_log and '%Error' not in lint_log
    sources.update(lint['inputs_sha256']);logs+=['cp42-lint.json','cp42-lint.log']
    cpu=checked('cp42-miter-verilator.json');assert cpu['cases']==69632 and len(cpu['memory_upcs'])==88
    for tag in ('cp42-miter-verilator','cp42-cpu-portable','cp42-cpu-vendor'):
        r=checked(tag+'.json');sources.update(r['inputs_sha256']);text=(ROOT/'build'/(tag+'.log')).read_text()
        assert all(line in text for line in r['pass_lines'])
        if tag.startswith('cp42-cpu'):
            assert '432 readbacks, 720 beats, 4902 lookup reads, 221988 clocks' in text
            assert 'vector4, no write, APR preserved' in text
        logs.extend(tag+s for s in ('.json','.log','-build.log'))
    for kind in ('miter-portable','csr-portable','csr-vendor'):
        path='cp42-cpu/'+kind+'.json';r=checked(path);sources.update(r['inputs_sha256']);logs.append(path)
    wrapper=json.loads((ROOT/'build/cp42-miter-wrapper.json').read_text())
    for p,h in wrapper.items():assert digest(ROOT/p)==h
    sources.update(wrapper);logs.append('cp42-miter-wrapper.json')
    fis=checked('cp42-fis.json');sources.update(fis['inputs_sha256']);logs.append('cp42-fis.json')
    assert [t['cases'] for t in fis['tests']]==[23840,23840,645]
    old_fis=json.loads((ROOT/'docs/verification-cp40.json').read_text())['sources_sha256']['build/fis-vectors.txt']
    assert digest(ROOT/'build/fis-vectors.txt')==old_fis
    for t in fis['tests']:
        assert t['pass_line'] in (ROOT/f'build/{t["tag"]}.log').read_text()
        for ext in ('.log','-build.log','.csv'):logs.append(t['tag']+ext)
        assert digest(ROOT/f'build/{t["tag"]}.csv')==digest(ROOT/f'tb/reports/cp40/{t["tag"].replace("cp42-","cp40-")}.csv')
    for p,h in fis['vendor_sha256'].items():assert digest(ROOT/'build/vendor'/p)==h
    assert digest(ROOT/'build/cp42-fis/vendor-vectors.txt')==fis['vendor_subset_sha256']
    sources.update(str(p.relative_to(ROOT)) for p in (ROOT/'build/cp42-fis').iterdir() if p.suffix in ('.v','.txt'))
    tag='cp42-apr';board=checked(tag+'-verified.json','files');sources.update(board['files'])
    counts=board_counts((ROOT/f'build/{tag}-board-rt11.log').read_text())
    assert counts==board_counts((ROOT/'tb/reports/cp40/cp40-apr-board-rt11.log').read_text())
    assert digest(ROOT/f'build/{tag}-uart.txt')==digest(ROOT/'tb/reports/cp40/cp40-apr-uart.txt')
    assert board['image_sha256']==digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    logs.extend(tag+s for s in ('-verified.json','-board-inputs.json','-board-build.log','-board-rt11.log','-uart.txt'))
    xm=ROOT/'../lsi11/disks/rt11v5.3/system.dsk';xm_sha='9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm)==xm_sha and xm.stat().st_size==27540480
    out=ROOT/'tb/reports/cp42';out.mkdir(parents=True,exist_ok=True)
    for path in logs:
        dest=out/path;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'build'/path,dest)
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for path in sorted(sources):
            if not path.startswith(('/', 'build/vendor/','synth/reports/')):archive.add(ROOT/path,arcname=path)
    report=dict(checkpoint='CP42: split D-input only in experimental APR engine',date='2026-09-10',
        synthesis=fits,baseline_apr_cp40i=old_apr,production_cp40h=production,
        selected=dict(apr='cp42d',variant='sliced',production='Unchanged CP40h'),lut_saved=dict(apr=10,production=0),
        free=dict(apr=dict(lut4=32,slices=15,ebr=0),production=dict(lut4=121,slices=56,ebr=1)),
        changed_hardware_inputs=changed,engine_change_restricted_to_d_cone=True,production_hardware_inputs_unchanged=True,
        microcode_words=dict(production=954,apr=963),extra_cycles=0,new_state_bits=0,
        proofs=proof['tests'],four_state_comparisons_per_variant=135168,four_state_variants=4,strict_lint_configurations=4,
        cpu_miter=cpu['pass_lines'],fis=fis['tests'],fis_csv_matches_cp40=True,cold_rt11fb_apr=counts,counts_and_uart_unchanged=True,
        production_cold_boot_rerun=False,physical_board='CP29a',board_programmed=False,
        rt11_xm=dict(boot_tested=False,image_modified=False,sha256=xm_sha),
        sources_sha256={p:digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted(out.rglob('*')) if p.is_file()},
        archived_synthesis_sha256={str(p.relative_to(ROOT)):digest(p) for n in fits for p in sorted((ROOT/'synth/reports'/n).iterdir())},
        limits=['Experimental APR CSR/lookup only; no translation/MMRs/abort-restart/CPU PA22 or high DMA.',
                '32 free LUT and no free EBR do not establish full MMU fit.',
                'Production RTL/board unchanged; production candidate CP42c was rejected by area priority.',
                'Four-state checks allow unknown operand data, but D selector remains known.',
                'FIS uses established independent oracle corpus; vendor subset is 645 cases.',
                'TRACE with unconstrained external pin delays; no FPGA programming or RT-11XM boot.'])
    (ROOT/'docs/verification-cp42.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP42: four fits; APR -10 LUT; formal/four-state/CPU/FIS/vendor/FB; native production preserved')


if __name__=='__main__':main()
