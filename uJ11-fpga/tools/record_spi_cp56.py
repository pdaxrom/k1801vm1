#!/usr/bin/env python3
"""Archive CP56 local verification and the precise pending synthesis payload."""
import hashlib
import json
import re
import shutil
import tarfile
from board_common import ROOT, CORE, BOARD, MMU


def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    tests=json.loads((ROOT/'build/cp56-tests/result.json').read_text())
    generated=json.loads((ROOT/'build/cp56-spi/inputs.json').read_text())
    inputs={}
    def merge(hashes):
        for p,h in hashes.items():
            assert p not in inputs or inputs[p]==h,p
            assert digest(ROOT/p)==h,p
            inputs[p]=h
    for hashes in (tests['inputs_sha256'],generated['inputs'],generated['outputs']): merge(hashes)
    for run in tests['runs']: merge(run['sources_sha256'])
    logs=tests['logs_sha256']
    for p,h in logs.items(): assert digest(ROOT/p)==h,p
    manifest=json.loads((ROOT/'build/cp56-final-board-inputs.json').read_text())
    assert manifest['spi_cp56'] and not manifest['mmu']
    merge(manifest['files'])
    text=(ROOT/'build/cp56-final-board-rt11.log').read_text()
    passed=re.search(r'PASS uJ11 cold RT-11 boot \+ DIR: (\d+) clocks, (\d+) RK commands, (\d+) timer edges, (\d+) UART wire bytes, (\d+) SD reads/(\d+) writes',text)
    counts=re.search(r'BOARD COUNTS retired(\d+) reads(\d+) writes(\d+) FRAM-CS(\d+)',text)
    assert passed and counts,'CP56 cold boot did not pass'
    boot=dict(zip(['clocks','rk_commands','timer_edges','uart_bytes','sd_reads','sd_writes'],map(int,passed.groups())))
    boot.update(zip(['retired','reads','writes','fram_transactions'],map(int,counts.groups())))
    boot['image_sha256']=manifest['image_sha256']
    previous=json.loads((ROOT/'docs/verification-cp54.json').read_text())['cold_rt11']['dma-ack']
    for key in ('rk_commands','uart_bytes','sd_reads','sd_writes','image_sha256'): assert boot[key]==previous[key],key
    old_uart=(ROOT/'tb/reports/cp54/cp54-dma-ack-uart.txt').read_bytes()
    new_uart=(ROOT/'build/cp56-final-uart.txt').read_bytes()
    assert old_uart[old_uart.index(b'SWAP  .SYS'):]==new_uart[new_uart.index(b'SWAP  .SYS'):]
    old_text=(ROOT/'tb/reports/cp54/cp54-dma-ack-board-rt11.log').read_text()
    probes=lambda t: re.findall(r'BUS FAULT[^\n]*',t)
    assert probes(text)==probes(old_text),'RT-11 hardware probe fault sequence changed'
    assert digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')==manifest['image_sha256']
    boot.update(speedup=previous['clocks']/boot['clocks'],directory_identical=True,
        raw_uart_identical=old_uart==new_uart,probe_faults_identical=len(probes(text)))
    # Preserve production, retained MMU, ISA, microcode and non-FRAM peripherals.
    preserved=json.loads((ROOT/'docs/verification-cp50.json').read_text())['sources_sha256']
    merge({p:preserved[p] for p in CORE+BOARD+MMU})
    copies=['build/cp56-tests/result.json','build/cp56-spi/inputs.json',
        'build/cp56-transfer.json','build/cp56-transfer-manifest.json','build/cp56a/inputs.json',
        'build/cp56a/clock.lpf','build/cp56a/build.tcl']
    copies += [f'build/cp56-final-{s}' for s in ('board-inputs.json','board-build.log','board-rt11.log','uart.txt')]
    merge({p:digest(ROOT/p) for p in copies+['tools/record_spi_cp56.py','docs/verification-cp54.json',
        'tb/reports/cp54/cp54-dma-ack-uart.txt','tb/reports/cp54/cp54-dma-ack-board-rt11.log']})
    out=ROOT/'tb/reports/cp56';out.mkdir(parents=True,exist_ok=True)
    for p in set(copies)|set(logs):
        target=out/p.removeprefix('build/');target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/p,target)
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for p in sorted(inputs):
            if not p.startswith('tb/reports/') and not p.endswith('.log'): archive.add(ROOT/p,arcname=p)
    result=dict(checkpoint='CP56 local full-rate SPI validation',date='2026-09-11',reference='cp54b',
        production_changed=False,mmu_changed=False,physical_board='CP54b',new_synthesis=False,
        synthesis_pending_reason='Automatic approval review rejected the CP56 source export; exact payload approval required.',
        frequency_mhz=dict(cpu=29.56,spi=29.56),microcode_words=954,
        primitive_comparisons=3066,random_fram_operations=4096,directed_fram_operations=654,
        timed_beats=7600,timed_reset_offsets=4480,assumed_pin_delay_envelopes=tests['assumed_pin_delay_envelopes'],
        physical_timing_closed=False,negative_controls=tests['negative'],bus_side_effect_beats=43,
        portable_workloads=9,vendor_workloads=9,workloads=tests['workloads'],cold_rt11=boot,
        unmodified_vendor_model_diagnostics=tests['unmodified_vendor_model_diagnostics'],
        sources_sha256=inputs,logs_sha256=logs,
        archives_sha256={str(p.relative_to(ROOT)):digest(p) for p in out.rglob('*') if p.is_file()})
    (ROOT/'docs/verification-cp56.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS CP56 archived: vendor/timing/negative/memory/board and cold FB; pending physical synthesis/timing')


if __name__=='__main__': main()
