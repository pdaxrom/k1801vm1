#!/usr/bin/env python3
"""Audit CP38 read-mux savings, exact formal inputs and both cold FB runs."""
import hashlib
import json
import shutil
import tarfile
from board_common import ROOT
from record_cp33 import digest, synthesis
from record_cp36 import board_counts
from record_cp37 import checked, failed_fit


def main():
    fits = {n: failed_fit(n) if n in ('cp38a','cp38c') else synthesis(n, n in ('cp38f','cp38g'))
            for n in ('cp38a','cp38b','cp38c','cp38d','cp38e','cp38f','cp38g')}
    assert fits['cp38f']==fits['cp38e'] and fits['cp38g']==fits['cp38d']
    baseline = synthesis('cp36f')
    apr_baseline = synthesis('cp37e')
    production_inputs = json.loads((ROOT/'synth/reports/cp36f/inputs.json').read_text())['files']
    changed = sorted(p for p,h in production_inputs.items() if not p.startswith('generated:') and digest(ROOT/p)!=h)
    assert changed == ['boards/hc1200/uj11_board_bus.v'], changed
    bus = ROOT/'boards/hc1200/uj11_board_bus.v'
    assert digest(bus) == digest(ROOT/'build/cp38-bus/split-base/uj11_board_bus.v')
    # The adopted change is bounded to the read-data mux. No decoder, state
    # update, peripheral instance or ACK expression changed at all.
    old = (ROOT/'build/cp38-bus/baseline/uj11_board_bus.v').read_text()
    new = bus.read_text()
    assert old[:old.index('\tassign rdata')] == new[:new.index('\twire [15:0] small_rdata')]
    assert old[old.index('\n\tassign acknowledge'):] == new[new.index('\n\tassign acknowledge'):]
    build = checked('cp38-bus/inputs.json')
    for p,h in build['outputs_sha256'].items(): assert digest(ROOT/p)==h
    proof = checked('cp38-proof.json')
    assert len(proof['tests'])==6
    units = checked('cp38-bus-units.json')
    assert len(units['tests'])==2
    # A test may name the frozen selected build copy or its byte-identical
    # adopted file. Both must cover the exact input used by final synthesis.
    unit_bus = next(p for p in units['inputs_sha256'] if p.endswith('/uj11_board_bus.v'))
    assert digest(ROOT/unit_bus)==digest(bus)
    boards = {}
    logs = ['cp38-proof.json','cp38-bus-units.json']
    sources = {'Makefile','tools/record_cp38.py','tools/checkpoint_board_decode.py',str(bus.relative_to(ROOT))}
    for kind, historical in [('production','tb/reports/cp36/cp36-board-rt11.log'),
                              ('apr','tb/reports/cp37/cp37-entry-board-rt11.log')]:
        tag = 'cp38-'+kind
        report = checked(tag+'-verified.json','files')
        actual_bus = next(p for p in report['files'] if p.endswith('/uj11_board_bus.v'))
        assert digest(ROOT/actual_bus)==digest(bus)
        counts = board_counts((ROOT/f'build/{tag}-board-rt11.log').read_text())
        assert counts==board_counts((ROOT/historical).read_text()), (kind,counts)
        assert digest(ROOT/f'build/{tag}-uart.txt')==digest(ROOT/'tb/reports/cp36/cp36-uart.txt')
        assert report['image_sha256']==digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')
        boards[kind] = counts
        sources.update(report['files'])
        logs += [tag+'-verified.json', tag+'-board-inputs.json', tag+'-board-build.log',
                 tag+'-board-rt11.log', tag+'-uart.txt']
    for test in proof['tests']:
        tag=test['tag'];logs.append(tag+'.log')
        text=(ROOT/'build'/(tag+'.log')).read_text()
        if test['defect']=='none':
            assert test['returncode']==0 and '38 are proven and 0 are unproven' in text
        else:assert test['returncode']!=0 and 'unproven' in text
    for test in units['tests']:
        tag=test['tag'];logs += [tag+'.log', tag+'-build.log']
        assert test['pass_line'] in (ROOT/'build'/(tag+'.log')).read_text()
    for report in (build, proof, units):sources.update(report['inputs_sha256'])
    sources.update(build['outputs_sha256'])
    xm = ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha='9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm)==xm_sha and xm.stat().st_size==27540480
    out = ROOT/'tb/reports/cp38'; out.mkdir(parents=True,exist_ok=True)
    for name in logs:shutil.copyfile(ROOT/'build'/name,out/name)
    # Vendor model files retain their own hashes; their installation path is
    # not a portable archive name. They remain available in build/vendor.
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for p in sorted(sources):
            if not p.startswith('/'):
                archive.add(ROOT/p,arcname=p)
    report = dict(checkpoint='CP38: separate firmware/FRAM selection from small-register read data',
        date='2026-09-10',synthesis=fits,baseline_cp36f=baseline,apr_baseline_cp37e=apr_baseline,
        production='cp38f',apr_experiment='cp38g',changed_production_inputs=changed,
        production_lut_saved=34,apr_lut_saved=37,
        production_free=dict(lut4=92,slices=43,ebr=1),apr_free=dict(lut4=52,slices=21,ebr=0),
        microcode_words=dict(production=954,apr=963),new_cycles=0,new_rtl_state_bits=0,
        formal=dict(equivalence_points=38,variants=4,defects_rejected=2,
                    method=proof['method'],scope='22 selects + 16 read-data bits; unchanged sequential/peripheral blocks'),
        units=units['tests'],cold_rt11fb=boards,counts_and_uart_unchanged=True,
        physical_board='CP29a',board_programmed=False,production_has_mmu=False,
        rt11_xm=dict(boot_tested=False,image_modified=False,path='lsi11/disks/rt11v5.3/system.dsk',sha256=xm_sha),
        sources_sha256={p:digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted(out.iterdir())},
        archived_synthesis_sha256={str(p.relative_to(ROOT)):digest(p)
            for name in fits for p in sorted((ROOT/'synth/reports'/name).iterdir())},
        limits=['No CSR/translation/MMR/abort-restart/PA22 CPU bus or high RK DMA integrated.',
                'APR scope remains read-only cost floor, not a fit for the complete MMU.',
                'Formal result covers two-state combinational behavior, not analog glitches.',
                'Fmax is Diamond TRACE; external pin delays are not constrained.',
                'RT-11FB simulation does not establish RT-11XM or high-memory operation.'])
    (ROOT/'docs/verification-cp38.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP38 evidence: seven synthesis archives, 38 formal outputs per variant, exact paired board counts')


if __name__=='__main__':
    main()
