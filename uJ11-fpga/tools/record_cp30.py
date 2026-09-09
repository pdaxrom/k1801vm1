#!/usr/bin/env python3
"""Archive the completed FP control checkpoint; reject missing or failed evidence."""
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
from board_common import ROOT,CORE,BOARD

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    out=ROOT/'tb/reports/cp30';out.mkdir(parents=True,exist_ok=True)
    required={
        'cp30-fp-portable-tb_fp_control.log':'90112 cases',
        'cp30-fp-vendor-tb_fp_control.log':'90112 cases',
        'cp30-fp-portable-tb_fp_state.log':'66720 checks',
        'cp30-fp-vendor-tb_fp_state.log':'66720 checks',
        'cp30-fp-events-portable.log':'448 cases',
        'cp30-fp-events-vendor.log':'448 cases',
        'cp30-board-selector.log':'16777216 combinations',
        'cp30-core-baseline.log':'12928 DCJ11 cases',
        'cp30-python.log':'\nOK\n',
        'cp30-sync-fis.log':'23840 exact state/PSW/bus/memory cases',
        'cp30-fis-fp--1.log':'23840 exact state/PSW/bus/memory cases',
        'cp30-fis-fp-1.log':'23840 exact state/PSW/bus/memory cases',
        'cp30-board-units.log':'PASS board bus: 29 beats',
        'cp30-fp-board-rt11.log':'355134893 clocks',
        'cp30-fp-bench-portable.log':'FPBENCH',
        'cp30-fp-bench-vendor.log':'FPBENCH',
    }
    for name,expected in required.items():
        data=(ROOT/'build'/name).read_text()
        assert expected in data and not re.search(r'FATAL|FAILED|%Error|Traceback',data),name
        shutil.copy2(ROOT/'build'/name,out/name)
    for name in ('cp30-fp-board-inputs.json','cp30-fp-board-build.log','cp30-fp-board-driver.log',
                 'cp30-fp-bench.json','cp30-binary-equivalence.json','cp30-fp-uart.txt'):
        shutil.copy2(ROOT/'build'/name,out/name)
    for name in ('fp-control-vectors.mem','fp-control-bench.mem','cp30-fis-fp--1.csv','cp30-fis-fp-1.csv'):
        (out/(name+'.gz')).write_bytes(gzip.compress((ROOT/'build'/name).read_bytes(),mtime=0))
    results={}
    for letter in 'abcde':
        name='cp30'+letter;directory=ROOT/'synth/reports'/name
        result=json.loads((directory/'result.json').read_text())
        for suffix,sha in result['reports'].items():
            assert digest(directory/(name+'_impl1'+suffix))==sha,(name,suffix)
        assert result['timing_pass']==(letter in 'de'),name
        results[name]={k:result.get(k) for k in ('lut4','ff','ebr','slices','fmax_mhz','timing_pass','fully_routed','fp11_control_enabled')}
        assert (directory/'source.tgz').exists()
    # HDL/vendor images are exactly the fitted source. Only assembler's JSON
    # metadata version was corrected after fit; the ROM bytes remain identical.
    original=json.loads((ROOT/'synth/reports/cp30d/inputs.json').read_text())['files']
    for name,sha in original.items():
        if name.startswith(('generated:','build/')) or name=='microasm/uj11asm.py':continue
        assert digest(ROOT/name)==sha,name
    assert (ROOT/'build/cp28-fis-sync.csv').read_bytes()==(ROOT/'build/cp30-fis-fp--1.csv').read_bytes()
    bench=json.loads((ROOT/'build/cp30-fp-bench.json').read_text())
    assert bench['portable']==bench['vendor']
    manifest={
        'checkpoint':'CP30 FP11 control/state; full FP11 not implemented',
        'date':'2026-09-10','mmu':False,'board_programmed':False,'physical_board_revision':'CP29a',
        'fp11_control_default':False,'microcode_words':987,'microcode_width':36,'encoding_version':13,
        'supported_encodings':21,'mnemonics':['CFCC','SETF','SETI','SETD','SETL','LDFPS Rn','STFPS Rn'],
        'synthesis':results,'full_fp11_fit_proven':False,
        'tests':{'control_cases_per_rom':90112,'control_total_clocks_per_rom':757760,
                 'trace_irq_reset_cases_per_rom':448,'state_checks_per_rom':66720,
                 'selector_combinations':16777216,'integer_cases':12928,'fis_cases':23840,
                 'fis_fp_enabled_cases_per_memory':23840,'fis_fp_enabled_memory_modes':['RAM','SPI FRAM no prefetch'],'python_tests':24},
        'benchmarks':bench,
        'rt11':{'fp11_control_enabled':True,'clocks':355134893,'retirements':3983747,
                'read_beats':5215923,'write_beats':423448,'fram_cs':3393856,'uart_wire_bytes':3270,
                'sd_read_commands':162,'sd_write_commands':6,'backing_image_modified':False},
        'manual':{'title':"DEC EK-FP11A-UG-001 FP11-A User's Guide",
                  'url':"https://ftpmirror.your.org/pub/misc/bitsavers/www.computer.museum.uq.edu.au/pdf/EK-FP11A-UG-001%20FP11-A%20Floating%20Point%20User%27s%20Manual.pdf",
                  'sha256':digest(ROOT/'build/fp11-doc/fp11a.pdf'),
                  'sections':['4.4 / Table 4-1','5.1','Table 5-2','5.3.15-5.3.22']},
        'fitted_source_note':'Only assembler statistics encoding_version 12->13 changed after CP30d/e; HDL and ROM unchanged.',
        'source_sha256':{name:digest(ROOT/name) for name in sorted(set(CORE+BOARD+[
            'microcode/m0.uasm','microcode/fis.uasm','microcode/fp11_control.uasm',
            'microasm/uj11asm.py','tools/link_fis.py','tools/build_decode_rom.py',
            'tb/reference_fp_control.c','tb/tb_fp_control.v','tb/tb_fp_state.v','tb/tb_fp_events.v',
            'tb/tb_board_bus.v','tb/test_fp_control.py','tools/run_fp_control.py','tools/benchmark_fp_control.py',
            'tools/record_cp30.py','tools/check_fp_compat.py','../core/core.c','../core/core.h','../core/pdp11_fp.c','../core/hardware.c']))},
        'logs_sha256':{str(p.relative_to(ROOT)):digest(p) for p in sorted(out.iterdir()) if p.is_file()},
        'limits':['Memory addressing modes, STST, FP exceptions, AC transfers and F/D arithmetic absent.',
                  '90,112-case corpus ran before optional benchmark parameters were added to the testbench; default case semantics unchanged.',
                  'No full native-FP diagnostic or differential arithmetic corpus yet.',
                  'FP-enabled top leaves 15 LUT and 5 slices; control store leaves 37 words.',
                  'External pin timing and worst-case OSCH tolerance closure not claimed.'],
    }
    (ROOT/'docs/verification-cp30.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({'archived_logs':len(manifest['logs_sha256']),'synthesis':results},indent=2))
if __name__=='__main__':main()
