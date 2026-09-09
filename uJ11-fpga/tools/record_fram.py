#!/usr/bin/env python3
"""Record CP6 verification and independently compare portable/vendor counts."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def main():
    logs={name:(ROOT/'tb/reports'/name).read_text() for name in [
        'cp6-tests.log','cp6-vendor.log','cp6-benchmarks-portable.log','cp6-benchmarks-vendor.log']}
    for name,text in logs.items():
        if any(mark in text for mark in ['FATAL','FAILED','%Error','%Warning']):
            raise ValueError(f'{name}: verification failure')
    for marker in ['PASS FRAM differential: 6272','PASS stream engine: opcode+3']:
        for name in ['cp6-tests.log','cp6-vendor.log']:
            if marker not in logs[name]: raise ValueError(f'{name}: missing {marker}')
    for marker in ['Ran 8 tests','PASS prefetch: 499','PASS lsi11 peripherals: 19']:
        if marker not in logs['cp6-tests.log']: raise ValueError(f'missing {marker}')
    results=[]
    for mode in range(3):
        portable=json.loads((ROOT/f'build/fram-benchmarks-{mode}-portable.json').read_text())
        vendor=json.loads((ROOT/f'build/fram-benchmarks-{mode}.json').read_text())
        if portable!=vendor or len(portable)!=5: raise ValueError('ROM model counts differ')
        for row in portable:
            if row['instructions']!=512 or row['memory_cycles']!=512:
                raise ValueError('unexpected retirement/beat count')
            cpi=row['microclocks']/row['instructions']
            results.append(dict(memory_mode=mode,**row,microclocks_per_instruction=cpi,
                                calculated_ips_at_29_56_mhz=29560000/cpi))
    (ROOT/'docs/benchmarks-fram.json').write_text(json.dumps({
        'clock_hz_nominal':29560000,'spi_divider':1,'warmup_retirements':64,
        'method':'functional RTL simulation; portable ROM equals vendor DP8KC; no board benchmark',
        'results':results},indent=2)+'\n')
    files=set()
    for glob in ['rtl/*.v','microcode/*.uasm','microasm/*.py','tb/*.v','tb/*.c','tb/test_*.py',
                 'reference/lsi11/*.v','tools/record_fram.py','tb/reports/cp6*.log']:
        files.update(ROOT.glob(glob))
    files.update([ROOT/'Makefile',ROOT/'../core/core.c',ROOT/'../core/core.h',
                  ROOT/'../core/hardware.c',ROOT/'../core/hardware.h',
                  ROOT/'docs/benchmarks-fram.json'])
    hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)}
    (ROOT/'docs/verification-cp6.json').write_text(json.dumps({
        'date':'2026-09-08','isa':'M0 only; no MMU',
        'checks':{'dcj11_cases_each_rom_model':6272,'prefetch_protocol_beats':499,
                  'legacy_peripheral_beats':19,'stream_words':4,'benchmark_runs_each_rom_model':15},
        'files':hashes},indent=2)+'\n')
    print('Recorded CP6: both ROM models agree, 15 FRAM benchmark cases, source/log hashes.')


if __name__=='__main__': main()
