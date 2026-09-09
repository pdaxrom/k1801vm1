#!/usr/bin/env python3
"""Record CP28 evidence only after current-source checks and a real routed gate."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import tarfile
from board_common import ROOT,CORE,BOARD

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    gate_dir=ROOT/'synth/reports/cp28m'
    gate=json.loads((gate_dir/'result.json').read_text())
    assert gate['timing_pass'] and gate['fully_routed'] and gate['ebr']==6 and gate['lut4']==1217
    for n,h in gate['inputs']['files'].items():
        path=gate_dir/n.split(':')[1] if n.startswith('generated:') else ROOT/n
        assert sha(path)==h,n
    for suffix,h in gate['reports'].items():assert sha(gate_dir/('design'+suffix))==h,suffix
    archive_count=source_count=report_count=0
    for directory in sorted((ROOT/'synth/reports').glob('cp28?')):
        archived=json.loads((directory/'result.json').read_text())
        with tarfile.open(directory/'source.tgz') as bundle:
            for n,h in archived['inputs']['files'].items():
                data=(directory/n.split(':')[1]).read_bytes() if n.startswith('generated:') else bundle.extractfile(n).read()
                assert hashlib.sha256(data).hexdigest()==h,(directory.name,n)
                source_count+=1
        for suffix,h in archived['reports'].items():
            assert sha(directory/('design'+suffix))==h,(directory.name,suffix)
            report_count+=1
        archive_count+=1
    assert archive_count==13
    board=json.loads((ROOT/'build/cp28-board-inputs.json').read_text())
    for n,h in board['files'].items():assert sha(ROOT/n)==h,n
    with tarfile.open(ROOT/'synth/reports/cp27a/source.tgz') as archive:
        for name in ('alu','decode'):
            old=archive.extractfile('rtl/uj11_'+name+'.v').read().decode()
            got=(ROOT/f'reference/uj11/{name}_cp27.v').read_text().split('\n',1)[1]
            replacement='uj11_alu_cp27' if name=='alu' else 'uj11_decode_gold'
            assert got==old.replace('module uj11_'+name,'module '+replacement)
    transcript=(ROOT/'build/cp28-uart.txt').read_bytes()
    assert b'RT-11FB (S) V05.03' in transcript and b'.DIR' in transcript and b'98 Files, 2201 Blocks' in transcript
    log=(ROOT/'build/cp28-board-rt11.log').read_text()
    matches=re.search(r'PASS uJ11 cold RT-11 boot \+ DIR: (\d+) clocks, (\d+) RK commands, (\d+) timer edges, (\d+) UART wire bytes, (\d+) SD reads/(\d+) writes',log)
    assert matches
    clocks,rk,ticks,uart,reads,writes=map(int,matches.groups())
    match=re.search(r'BOARD COUNTS retired(\d+) reads(\d+) writes(\d+) FRAM-CS(\d+)',log);assert match
    retirements,read_beats,write_beats,fram_cs=map(int,match.groups())
    assert uart==len(transcript) and reads>2 and writes>0
    proof=json.loads((ROOT/'build/cp28-alu-equivalence.json').read_text())
    assert proof['candidate_sha256']==sha(ROOT/'rtl/uj11_alu.v') and proof['results']['negative']['returncode']!=0
    names=['cp28-board-rt11.log','cp28-board-build.log','cp28-uart.txt','cp28-board-inputs.json',
           'cp28-units.log','cp28-sync-driver.log','cp28-sync-fis-driver.log','cp28-default-regression.log',
           'cp28-new-alu-tests.log','cp28-alu-proof-driver.log','cp28-alu-positive.log','cp28-alu-negative.log','cp28-alu-equivalence.json']
    for n in ['cp28-units.log','cp28-sync-driver.log','cp28-sync-fis-driver.log','cp28-default-regression.log','cp28-new-alu-tests.log']:
        text=(ROOT/'build'/n).read_text()
        assert 'PASS' in text and 'FATAL:' not in text and '%Error:' not in text,n
    sync_text=(ROOT/'build/cp28-sync-driver.log').read_text()
    patterns={'register':r'PASS differential: (\d+) DCJ11',
              'addressing':r'PASS ea differential mode-1: (\d+) DCJ11',
              'interrupt':r'PASS irq differential mode-1: (\d+) DCJ11',
              'trace':r'PASS trace_bit differential mode-1: (\d+) DCJ11',
              'fault':r'PASS bus fault differential mode-1: (\d+) DCJ11'}
    counts={}
    for suite,pattern in patterns.items():
        match=re.search(pattern,sync_text);assert match,suite
        counts[suite]=int(match[1])
    assert counts==dict(register=12928,addressing=31671,interrupt=6654,trace=11196,fault=32780)
    fis_match=re.search(r'PASS FIS memory_mode=-1: (\d+) exact.*?, (\d+) injected faults',
                        (ROOT/'build/cp28-sync-fis-driver.log').read_text())
    assert fis_match and tuple(map(int,fis_match.groups()))==(23840,3072)
    inputs=CORE+BOARD+['rtl/uj11_rom.v','rtl/uj11_microseq.v','reference/uj11/alu_cp27.v','reference/uj11/decode_cp27.v',
       'reference/lsi11/spi_fram_model.v','reference/lsi11/spi_sd_model_cp28.v','Makefile','.gitattributes','.gitignore',
       '../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h',
       'tools/fis_reference.py','tools/make_fis_vectors.py',
       'microcode/generated/m0.mem','microcode/generated/firmware.mem','microcode/generated/decode.mem',
       'microcode/m0.uasm','microcode/fis.uasm','firmware/sd_boot.asm','firmware/rk_service.asm']
    inputs += ['tools/'+n for n in ['verify_cp28.py','record_cp28.py','check_sync_decode.py','check_board_units.py','check_alu_cp28.py',
                                  'build_firmware.py','build_decode_rom.py','run_board.py','run_fis_tests.py']]
    inputs += ['tb/'+n+'.v' for n in ['tb_core','tb_ea','tb_trace_bit','tb_bus_fault','tb_fis','tb_board_irq','tb_board_bus',
                                    'tb_board_tick','tb_board_fram','tb_firmware_rom','tb_decode_rom','tb_board_rt11','tb_alu','tb_datapath']]
    fixture_names=['isa_vectors.mem','ea-vectors.txt','irq-vectors.txt','trace_bit-vectors.txt','bus-fault-vectors.txt','fis-vectors.txt']
    fixtures={n:sha(ROOT/'build'/n) for n in fixture_names}
    stats=json.loads((ROOT/'microcode/generated/m0.stats.json').read_text())
    report=dict(checkpoint='CP28',date='2026-09-10',synthesis='cp28m',scope=gate['scope'],
        resources={k:gate[k] for k in ('lut4','ff','ebr','slices','fmax_mhz','constraint_mhz','timing_pass','fully_routed')},
        microcode_words=stats['used_words'],word_bits=stats['word_bits'],
        archive_audit=dict(archives=archive_count,source_hashes=source_count,raw_report_hashes=report_count),
        verification=dict(sync_suite_counts=counts,sync_integer_cases=sum(v for k,v in counts.items() if k!='fault'),sync_bus_fault_cases=counts['fault'],sync_fis_cases=23840,sync_fis_injected_faults=3072,
            opcode_cases_per_rom=65536,firmware_words_per_rom=512,fram_transactions=4096,
            bus_contract_beats=29,private_irq_profiles=2,tick_divisors=[1,2,3,257,591200],
            alu_independent_checks=66592,alu_proof=proof),
        board=dict(clocks=clocks,rk_csr_writes=rk,timer_events=ticks,uart_wire_bytes=uart,sd_reads=reads,sd_writes=writes,
            retirements=retirements,read_beats=read_beats,write_beats=write_beats,fram_cs_assertions=fram_cs,
            microclocks_per_retirement=clocks/retirements,nominal_seconds=clocks/29560000,
            nominal_retirements_per_second=retirements*29560000/clocks,
            workload='Cold SD bootstrap, STARTF.COM and DIR; counts include private firmware and device waits',
            image_sha256=board['image_sha256'],image_bytes=board['image_bytes'],backing_image_unchanged=True),
        limitations=['Full top has no prefetch; earlier prefetch core remains available separately',
                     '1217 LUT exceeds the desired 900-1100 budget; only 63 LUT and 30 slices remain',
                     'FP11, mode/register banking and native ODT are not implemented',
                     'No MMU or address translation',
                     'No physical FPGA programming, external pin-delay closure or vendor full-board RT-11 simulation',
                     'Full prior integer vendor suite was not rerun; listed CP28 suites are scoped tests'],
        files={n:sha(ROOT/n) for n in sorted(set(inputs))},fixtures=fixtures,
        logs={n:sha(ROOT/'build'/n) for n in names})
    out=ROOT/'tb/reports/cp28';out.mkdir(exist_ok=True)
    for name in names:shutil.copyfile(ROOT/'build'/name,out/name)
    (ROOT/'docs/verification-cp28.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(resources=report['resources'],board=report['board']),indent=2))
if __name__=='__main__':main()
