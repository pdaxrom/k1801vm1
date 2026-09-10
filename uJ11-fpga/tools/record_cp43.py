#!/usr/bin/env python3
"""Bind selected CP43 MMR3 RTL to four fits, CSR/CPU/vendor and cold FB evidence."""
import gzip
import json
import shutil
import tarfile
from board_common import ROOT
from record_cp33 import digest, synthesis
from record_cp36 import board_counts
from record_cp37 import checked


def main():
    fits={f'cp43{x}':synthesis(f'cp43{x}',x=='d') for x in 'abcd'}
    selected=fits['cp43d'];baseline=synthesis('cp42d');production=synthesis('cp40h')
    assert selected['lut4']==1258 and selected['lut4']==baseline['lut4']+10
    assert selected['ff']==baseline['ff']+7 and selected['ebr']==7
    assert all(selected['lut4']<fits['cp43'+x]['lut4'] for x in 'abc')
    # CP43 adds a build-only bus and six-bit CSR. Every previous hardware
    # input still exists unchanged, including CP42 engine and original bus.
    for name in ('cp40h','cp42d'):
        inputs=json.loads((ROOT/f'synth/reports/{name}/inputs.json').read_text())['files']
        for p,h in inputs.items():
            if not p.startswith('generated:') and p.endswith(('.v','.mem','.lpf','.sty')):
                assert digest(ROOT/p)==h,(name,p,'baseline hardware changed')
    build=checked('cp43-mmr3/inputs.json')
    assert build['microcode_words']==963 and build['read_mux']=='masked'
    for p,h in build['outputs_sha256'].items():assert digest(ROOT/p)==h,p
    selected_inputs=json.loads((ROOT/'synth/reports/cp43d/inputs.json').read_text())['files']
    sources={p for p in selected_inputs if not p.startswith('generated:')}
    sources.update(['tools/record_cp43.py','tools/record_cp33.py','tools/record_cp36.py',
                    'tools/record_cp37.py','tools/archive_synthesis.py'])
    tests={};logs=['cp43-mmr3/inputs.json','cp43-unit-lint.log','cp43-oracle-cc.log']
    for suite in ('unit-portable','oracle-portable','bus-portable','cpu-portable','cpu-vendor','apr-portable','apr-vendor'):
        tag='cp43-'+suite;r=checked(tag+'.json');sources.update(r['inputs_sha256'])
        text=(ROOT/f'build/{tag}.log').read_text()
        assert all(line in text for line in r['pass_lines']) and 'FATAL' not in text
        if suite.startswith('cpu'):
            assert '324 readbacks, 516 beats, 3508 lookup reads, 157988 clocks' in text
            assert 'word read/write vector4' in text
        if suite.startswith('apr'):
            assert r['pass_lines']==json.loads((ROOT/f'tb/reports/cp42/cp42-cpu-{suite.split("-")[1]}.json').read_text())['pass_lines']
        tests[suite]=r['pass_lines'];logs.extend(tag+s for s in ('.json','.log','-build.log'))
    assert '1030 checked edges' in tests['unit-portable'][0]
    assert '262160 commands, 16 RESET' in tests['oracle-portable'][0]
    assert '2097152 decode/DMA combinations' in tests['bus-portable'][0]
    assert '131115 beats' in tests['bus-portable'][1]
    lint=(ROOT/'build/cp43-unit-lint.log').read_text()
    assert '%Warning' not in lint and '%Error' not in lint
    assert not (ROOT/'build/cp43-oracle-cc.log').read_text()
    board=checked('cp43-verified.json','files');sources.update(board['files'])
    counts=board_counts((ROOT/'build/cp43-board-rt11.log').read_text())
    assert counts==board_counts((ROOT/'tb/reports/cp42/cp42-apr-board-rt11.log').read_text())
    assert digest(ROOT/'build/cp43-uart.txt')==digest(ROOT/'tb/reports/cp42/cp42-apr-uart.txt')
    assert board['image_sha256']==digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    assert board['mmr3_csr'] and not board['translation'] and not board['rt11_xm']
    logs.extend('cp43'+s for s in ('-verified.json','-board-inputs.json','-board-build.log','-board-rt11.log','-uart.txt'))
    xm=ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha='9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm)==xm_sha and xm.stat().st_size==27540480
    manual='../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf'
    out=ROOT/'tb/reports/cp43';out.mkdir(parents=True,exist_ok=True)
    for path in logs:
        dest=out/path;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'build'/path,dest)
    corpus=(ROOT/'build/cp43-mmr3-oracle.txt').read_bytes()
    assert len(corpus.splitlines())==262160
    (out/'mmr3-oracle.txt.gz').write_bytes(gzip.compress(corpus,mtime=0))
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for p in sorted(sources):
            if p.startswith(('/', 'build/vendor/','synth/reports/')):continue
            # Keep the external emulator snapshot without parent traversal.
            name='reference-repository/'+p[3:] if p.startswith('../') else p
            archive.add(ROOT/p,arcname=name)
    report=dict(checkpoint='CP43: experimental MMR3 CSR storage/reset; no CPU translation',date='2026-09-10',
        synthesis=fits,selected='cp43d',baseline_apr_cp42d=baseline,production_cp40h=production,
        delta=dict(lut4=10,ff=7,ebr=0,microcode_words=0),free=dict(lut4=22,slices=10,ebr=0),
        baseline_hardware_inputs_unchanged=True,microcode_words=dict(production=954,experimental=963),
        register=dict(canonical_pa_octal='17772516',unmapped_va_octal='172516',writable_bits='5:0',
                      reserved_bits_read_zero='15:6',byte_writes=True,init_reset_clear=True,
                      legacy_177516_alias=False,controls_connected=False,registered_ack_bits=1),
        tests=tests,new_module_strict_lint=True,full_board_verilator_width_checks=True,
        cold_rt11fb=counts,fb_counts_and_uart_match_cp42=True,fb_image_sha256=board['image_sha256'],
        fis_full_corpus_rerun=False,physical_board='CP29a',board_programmed=False,
        rt11_xm=dict(boot_tested=False,image_modified=False,sha256=xm_sha),
        manual=dict(path=manual,sha256=digest(ROOT/manual),sections=['4.7.4','4.9','RESET flowchart']),
        sources_sha256={p:digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted(out.rglob('*')) if p.is_file()},
        archived_synthesis_sha256={str(p.relative_to(ROOT)):digest(p) for n in fits for p in sorted((ROOT/'synth/reports'/n).iterdir())},
        limits=['MMR3 bits are stored only; no MMR0/1/2, CPU translation, split I/D, CSM or MAP output.',
                'No PDR check/automatic W, abort/restart, CPU PA22 or high RK DMA; CPU FRAM bank remains zero.',
                '22 free LUT / 10 slices / 0 EBR do not establish full MMU fit.',
                'CSR differential uses existing DCJ11 helper functions, not a complete CPU/MMU differential.',
                'Production and FPGA unchanged; TRACE has no external pin delay constraints; RT-11XM not booted.'])
    (ROOT/'docs/verification-cp43.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP43: four fits; selected MMR3 +10 LUT/+7 FF; CSR/oracle/CPU/vendor/FB; production preserved')


if __name__=='__main__':main()
