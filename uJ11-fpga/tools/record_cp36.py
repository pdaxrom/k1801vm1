#!/usr/bin/env python3
"""Bind CP36 production/context area savings to exact RTL and regression results."""
import json
import re
import shutil
from board_common import ROOT
from record_cp33 import digest, synthesis


def checked(name, key='inputs_sha256'):
    result=json.loads((ROOT/'build'/name).read_text())
    for path, expected in result[key].items():
        assert digest(ROOT/path)==expected, (name,path,'test input changed')
    return result


def board_counts(text):
    match=re.search(r'PASS uJ11 cold RT-11 boot \+ DIR: (\d+) clocks, (\d+) RK commands, (\d+) timer edges, (\d+) UART wire bytes, (\d+) SD reads/(\d+) writes',text)
    assert match
    result=dict(zip(('clocks','rk_commands','timer_edges','uart_wire_bytes','sd_reads','sd_writes'),map(int,match.groups())))
    match=re.search(r'BOARD COUNTS retired(\d+) reads(\d+) writes(\d+) FRAM-CS(\d+)',text)
    assert match
    result.update(zip(('retirements','read_beats','write_beats','fram_transactions'),map(int,match.groups())))
    return result


def main():
    fits={name:synthesis(name,name=='cp36f') for name in [f'cp36{x}' for x in 'abcdefg']}
    baseline=synthesis('cp31c')
    context_baseline=synthesis('cp35e')
    selected=json.loads((ROOT/'synth/reports/cp36g/inputs.json').read_text())['files']
    for path, expected in selected.items():
        if path.startswith('generated:'):continue
        actual=ROOT/path
        if path=='build/cp36g/uj11_board.v':actual=ROOT/'build/cp35-board/uj11_board.v'
        if path=='tools/checkpoint_decode_area.py':
            old='decode_variant=args.variant, fixed_context_hook=args.context, aligned_word_bus=args.word_bus,'
            new="decode_variant=args.variant, fixed_context_hook=args.context, word_bus_build_override=args.word_bus,\n                aligned_word_bus='.ALIGNED_WORD_READS(1)' in (ROOT/next(s for s in sources if s.endswith('/uj11_board.v'))).read_text(),"
            text=actual.read_text();assert text.count(new)==1
            import hashlib
            assert hashlib.sha256(text.replace(new,old).encode()).hexdigest()==expected
        else:assert digest(actual)==expected,(path,'final context source differs')
    assert '.ALIGNED_WORD_READS(1)' in (ROOT/'build/cp35-board/uj11_board.v').read_text()
    old_inputs=json.loads((ROOT/'synth/reports/cp31c/inputs.json').read_text())['files']
    changed=sorted(p for p,h in old_inputs.items() if not p.startswith('generated:') and digest(ROOT/p)!=h)
    assert changed==sorted(['rtl/uj11_core.v','rtl/uj11_decode_rom.v','boards/hc1200/uj11_board.v',
                           'tools/build_decode_rom.py','tools/checkpoint_board.py']),changed
    assert (ROOT/'microcode/generated/decode.mem').read_bytes()==(ROOT/'build/cp36-decode/decode.mem').read_bytes()
    assert (ROOT/'rtl/uj11_core.v').read_bytes()==(ROOT/'build/cp36-word-bus/uj11_core.v').read_bytes()
    assert (ROOT/'rtl/uj11_decode_rom.v').read_bytes()==(ROOT/'build/cp36-decode/bits/uj11_decode_rom.v').read_bytes()
    decode=checked('cp36-decode-tests.json');assert len(decode['tests'])==4
    cpu=[checked(f'cp36-cpu-{s}.json') for s in ('verilator','iverilog','negative')]
    assert [r['cases'] for r in cpu]==[69632,1024,1024]
    assert len(cpu[0]['covered_memory_upcs'])==88 and cpu[2]['result_returncode']!=0
    previous_cpu=json.loads((ROOT/'docs/verification-cp35.json').read_text())['tests']['miter_pass_lines']
    assert [r['pass_lines'] for r in cpu[:2]]==previous_cpu
    lint=checked('cp36-lint.json')
    fis=checked('cp35-fis.json')
    assert [t['cases'] for t in fis['tests']]==[23840,23840,645]
    for path,expected in fis['vendor_sha256'].items():assert digest(ROOT/'build/vendor'/path)==expected
    # Reused CP35 harnesses retain their established build-log prefixes.
    # Their new source manifests and logs are archived separately under CP36.
    board=checked('cp36-board-inputs.json','files')
    context=checked('cp35-entry-board.json','files')
    fb=board_counts((ROOT/'build/cp36-board-rt11.log').read_text())
    hook_fb=board_counts((ROOT/'build/cp35-entry-board-rt11.log').read_text())
    historical=board_counts((ROOT/'tb/reports/cp31/cp31c-board-rt11.log').read_text())
    assert fb==historical
    assert hook_fb==json.loads((ROOT/'docs/verification-cp35.json').read_text())['tests']['rt11fb_cold_board']
    assert (ROOT/'build/cp35-entry-uart.txt').read_bytes()==(ROOT/'tb/reports/cp35/cp35-entry-uart.txt').read_bytes()
    fb_image=ROOT/'../lsi11-fpga/images/rt11v503.dsk'
    assert digest(fb_image)==board['image_sha256']==context['image_sha256']
    assert digest(ROOT/'build/cp36-uart.txt')==digest(ROOT/'build/cp35-entry-uart.txt')
    logs=['cp36-decode-tests.json','cp36-lint.json','cp36-lint.log',
          'cp36-board-inputs.json','cp36-board-build.log','cp36-board-rt11.log','cp36-uart.txt',
          'cp35-entry-board.json','cp35-entry-board-build.log','cp35-entry-board-rt11.log','cp35-entry-uart.txt',
          'cp35-fis.json']
    for test in decode['tests']+fis['tests']:
        tag=test['tag'];logs += [tag+'.log',tag+'-build.log']
        assert test['pass_line'] in (ROOT/'build'/(tag+'.log')).read_text()
    for result,suffix in zip(cpu,('verilator','iverilog','negative')):
        tag='cp36-cpu-'+suffix;logs += [tag+'.json',tag+'.log',tag+'-build.log']
        text=(ROOT/'build'/(tag+'.log')).read_text()
        if result['negative_lanes']:assert text.strip()==result['failure']
        else:assert all(line in text for line in result['pass_lines'])
    text=(ROOT/'build/cp36-lint.log').read_text()
    assert 'PASS strict CP36 core lint' in text and '%Warning' not in text and '%Error' not in text
    xm=ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha='9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm)==xm_sha and xm.stat().st_size==27540480
    out=ROOT/'tb/reports/cp36';out.mkdir(parents=True,exist_ok=True)
    for name in logs:shutil.copyfile(ROOT/'build'/name,out/name)
    sources={'Makefile','tools/record_cp36.py','tools/record_cp33.py',
             'tools/build_decode_compact.py','tools/build_decode_word_bus.py','tools/checkpoint_decode_area.py'}
    for report in [decode,lint,fis]+cpu:
        sources.update(p for p in report['inputs_sha256'] if not p.startswith('build/'))
    sources.update(p for r in (board,context) for p in r['files'] if not p.startswith('build/'))
    report=dict(checkpoint='CP36: compact opcode index and aligned-word input before byte operand mux',
        date='2026-09-10',synthesis=fits,baseline_cp31c=baseline,baseline_cp35e=context_baseline,
        production='cp36f',production_has_mmu=False,experimental_context='cp36g',
        production_changed_inputs=changed,production_lut_saved=30,production_free_lut=58,
        context_lut_saved=30,context_free_lut=37,context_free_slices=14,
        production_microcode_words=954,context_microcode_words=963,decode_ebr=1,
        microcode_firmware_and_decode_images_unchanged=True,fp11_removed=True,fis_retained=True,
        board_programmed=False,physical_board_revision='CP29a',
        tests=dict(opcodes_per_variant=65536,enable_holds_per_variant=65536,decode_variants=decode['tests'],
                   cpu_cases=69632,four_state_cpu_cases=1024,covered_memory_words=88,
                   cpu_cycle_counts_match_cp35=True,swapped_byte_lanes_rejected=True,
                   strict_default_and_word_bus_lint=True,fis=fis['tests'],
                   rt11fb=fb,rt11fb_context=hook_fb,board_counts_unchanged=True,uart_transcripts_equal=True),
        rt11_xm=dict(path='lsi11/disks/rt11v5.3/system.dsk',sha256=xm_sha,image_modified=False,boot_tested=False),
        cp36g_metadata_note='The archived aligned_word_bus flag is the old build override flag. '
                            'The archived production board explicitly enables ALIGNED_WORD_READS. '
                            'The launcher now reports actual mode and override separately; no RTL change.',
        sources_sha256={p:digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted(out.iterdir())},
        archived_synthesis_sha256={str(p.relative_to(ROOT)):digest(p)
                                  for name in fits for p in sorted((ROOT/'synth/reports'/name).iterdir())},
        limits=['All fits are full HC1200 board tops; external pin delays remain unconstrained.',
                'LUT savings reflect global mapping and placement, not standalone index-gate counts.',
                'The word-bus variant is 3 LUT larger than bits-only without the hook, but saves 24 with the hook.',
                'The default core read contract remains right-justified; the board opts into aligned words.',
                'No new CPU stages or cycles; no translation/MMR/abort-restart or PA22 CPU/RK DMA integration.',
                '37 free LUT in the context prototype do not establish that the complete MMU fits.',
                'RT-11FB regressions do not establish RT-11XM compatibility. No FPGA programming.'])
    (ROOT/'docs/verification-cp36.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(production=fits['cp36f'],context=fits['cp36g'],board=fb,context_board=hook_fb,
                          unchanged_cycle_counts=True,xm_boot_tested=False),indent=2))


if __name__=='__main__':main()
