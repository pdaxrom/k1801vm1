#!/usr/bin/env python3
"""Archive rejected CP41 sequencer experiments and an unchanged CP40 control."""
import hashlib
import json
import re
import shutil
import tarfile
from board_common import ROOT
from record_cp33 import digest,synthesis
from record_cp37 import checked


def rejected(name):
    folder=ROOT/'synth/reports'/name
    report=json.loads((folder/'result.json').read_text())
    assert not report['timing_pass']
    assert report['inputs']==json.loads((folder/'inputs.json').read_text())
    with tarfile.open(folder/'source.tgz') as archive:
        for path,h in report['inputs']['files'].items():
            data=(folder/'clock.lpf').read_bytes() if path.startswith('generated:') else archive.extractfile(path).read()
            assert hashlib.sha256(data).hexdigest()==h,(name,path)
    for suffix,h in report['reports'].items():assert digest(folder/('design'+suffix))==h
    mapped=(folder/'design.mrp').read_text()
    for key,label in [('lut4','LUT4s'),('ff','registers'),('ebr','block RAMs'),('slices','SLICEs')]:
        match=re.search(r'Number of '+label+r':\s+(\d+)\s+out of\s+(\d+)',mapped)
        assert int(match[1])==report[key]
    if name=='cp41c':
        assert report['diamond_returncode']==0 and report['fully_routed']
        assert report['fmax_mhz']==29.387 < report['constraint_mhz']==29.56
        assert any(v>0 for v in report['cumulative_negative_slack_reported'])
    else:
        assert report['diamond_returncode']!=0 and not report['fully_routed']
        assert 'fmax_mhz' not in report and (report['slices']>640 or report['lut4']>1280)
    return {k:report[k] for k in ('variant','lut4','ff','ebr','slices','fmax_mhz','timing_pass','fully_routed') if k in report}


def main():
    fits={f'cp41{x}':rejected(f'cp41{x}') for x in 'abcd'}
    control=synthesis('cp41e',True);prior=synthesis('cp40i',True)
    assert control==prior
    fits['cp41e']=control
    production=synthesis('cp40h',True)
    # The complete previous input manifests, not just the edited block, match.
    old_inputs=json.loads((ROOT/'synth/reports/cp40i/inputs.json').read_text())['files']
    current_inputs=json.loads((ROOT/'synth/reports/cp41e/inputs.json').read_text())['files']
    hardware={p:h for p,h in old_inputs.items() if p.endswith(('.v','.mem','.lpf','.sty'))}
    assert all(current_inputs[p]==h for p,h in hardware.items())
    assert digest(ROOT/'rtl/uj11_microseq.v')==digest(ROOT/'build/cp41-seq/baseline/uj11_microseq.v')
    assert digest(ROOT/'build/cp39-csr/uj11_microseq.v')==digest(ROOT/'build/cp41-seq/baseline/uj11_microseq_apr.v')
    build=checked('cp41-seq/inputs.json');proof=checked('cp41-proof.json');sim=checked('cp41-sim.json')
    for path,h in build['outputs_sha256'].items():assert digest(ROOT/path)==h
    assert sum(not t['negative'] for t in proof['tests'])==8
    assert sum(t['negative'] for t in proof['tests'])==3
    sources={'tools/record_cp41.py','tools/checkpoint_seq_mux.py','tools/record_cp33.py','tools/record_cp37.py',
             'tools/board_common.py','tools/build_mmu_entry.py'}
    logs=['cp41-seq/inputs.json','cp41-proof.json','cp41-sim.json']
    for r in (build,proof,sim):sources.update(r['inputs_sha256'])
    sources.update(build['outputs_sha256']);sources.add('build/cp41-proof/proof.ys')
    for t in proof['tests']:
        path='cp41-proof/'+t['tag']+'.log';logs.append(path)
        assert digest(ROOT/'build'/path)==t['log_sha256']
        assert (t['returncode']!=0)==t['negative']
    assert len(sim['tests'])==8
    for t in sim['tests']:
        tag=t['tag'];path='cp41-sim/'+tag+'.log'
        assert digest(ROOT/'build'/path)==t['log_sha256']
        assert '131072 cycles, 262145 comparisons' in t['pass_line']
        assert t['pass_line'] in (ROOT/'build'/path).read_text()
        lint=(ROOT/f'build/cp41-sim/{tag}-lint.log').read_text()
        assert t['lint_returncode']==0 and '%Warning' not in lint and '%Error' not in lint
        logs.extend('cp41-sim/'+tag+s for s in ('.log','-build.log','-lint.log'))
    xm=ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha='9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm)==xm_sha and xm.stat().st_size==27540480
    fb=ROOT/'../lsi11-fpga/images/rt11v503.dsk'
    assert digest(fb)=='e769228f2e1262220297bfa98b8f2841688849ab4c49ad9cd48d0d73d0a99553'
    out=ROOT/'tb/reports/cp41';out.mkdir(parents=True,exist_ok=True)
    for path in logs:
        dest=out/path;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'build'/path,dest)
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for path in sorted(sources):
            if not path.startswith('synth/reports/'):archive.add(ROOT/path,arcname=path)
    report=dict(checkpoint='CP41: reject four equivalent sequencer mux variants; retain CP40',date='2026-09-10',
        synthesis=fits,baseline_production_cp40h=production,baseline_apr_cp40i=prior,
        selected='Unchanged CP40',adopted_candidates=[],lut_saved=0,production_inputs_unchanged=True,
        apr_hardware_input_hashes_unchanged=hardware,microcode_words=dict(production=954,apr=963),
        new_state_bits=0,extra_cycles=0,proof=proof['tests'],four_state=sim['tests'],strict_lint_configurations=8,
        cold_rt11fb_rerun=False,cpu_fis_regressions_rerun=False,previous_regressions='CP40, unchanged complete synthesis input hashes',
        board_programmed=False,physical_board='CP29a',
        rt11_xm=dict(boot_tested=False,image_modified=False,sha256=xm_sha),
        free=dict(production=dict(lut4=121,slices=56,ebr=1),apr=dict(lut4=22,slices=9,ebr=0)),
        sources_sha256={p:digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted(out.rglob('*')) if p.is_file()},
        archived_synthesis_sha256={str(p.relative_to(ROOT)):digest(p) for n in fits for p in sorted((ROOT/'synth/reports'/n).iterdir())},
        limits=['No candidate adopted; this checkpoint establishes no additional resource headroom.',
                'Three MAP slice overflows; encoded candidate routes but fails 29.56 MHz.',
                'Equivalence is not proof of sequencer area optimality.',
                'Formal uses two-state inputs and matched arbitrary state; simulation uses known controls after reset.',
                'No CPU translation/MMRs/abort-restart/PA22/high DMA or RT-11XM boot.',
                'TRACE has unconstrained external pin delays; no hardware programming.'])
    (ROOT/'docs/verification-cp41.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP41: four rejected gates, exact CP40 control, 8 equivalence/simulation/lint checks and 3 detected mutations')


if __name__=='__main__':main()
