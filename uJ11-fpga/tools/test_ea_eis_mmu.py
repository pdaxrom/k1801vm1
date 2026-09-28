#!/usr/bin/env python3
"""Compare addressing and EIS guest programs with core/core.c using private ROMs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from board_common import ROOT
from build_mmu import CORE, MICROCODE, assemble, rom


def run(out, mhz, microcode, combined_abort=False):
    out.mkdir(parents=True, exist_ok=False)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    microcode = microcode or ROOT / MICROCODE[0]
    sources = [ROOT / p for p in CORE + [
        'tests/reference_ea_eis.c', 'tests/mmu/tb_integer.v', 'tools/test_ea_eis_mmu.py',
        'tools/build_mmu.py', 'microasm/uj11mmuasm.py', MICROCODE[1],
        '../core/core.c', '../core/core.h', '../core/hardware.c', '../core/hardware.h', '../core/pdp11_fp.c']]
    sources.append(microcode)
    hashes = {str(p): sha(p) for p in sources}
    image, _, _, stats = assemble(microcode.read_text() + '\n' + (ROOT / MICROCODE[1]).read_text())
    mem = out / 'microcode.mem'
    mem.write_text(''.join(f'{w:014x}\n' for w in image))
    rtl = out / 'rom.v'
    rtl.write_text(rom(image, 'off', mhz == 50))
    bench = out / 'tb_integer.v'
    original = (ROOT / 'tests/mmu/tb_integer.v').read_text()
    assert original.count('uj11_mmu_cpu dut(') == 1
    bench.write_text(original.replace('uj11_mmu_cpu dut(', f'uj11_mmu_cpu #(.MICROCODE("{mem}")) dut('))
    subprocess.run(['cc', '-O2', '-DENABLE_MMU=1', '-I..', 'tests/reference_ea_eis.c',
                    '../core/core.c', '../core/hardware.c', '-o', str(out / 'reference')], cwd=ROOT, check=True)
    with (out / 'fixtures.txt').open('w') as f:
        subprocess.run([str(out / 'reference')] + (['--combined-abort'] if combined_abort else []),
                       stdout=f, check=True)
    command = ['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD', '-Wno-PINMISSING',
               '--top-module', 'tb_integer', '-j', '4', '--Mdir', str(out / 'obj'), str(bench)] + CORE + [str(rtl)]
    with (out / 'build.log').open('w') as f:
        subprocess.run(command, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, check=True)
    with (out / 'simulation.log').open('w') as f:
        rc = subprocess.run([str(out / 'obj/Vtb_integer'), '+fixtures=' + str(out / 'fixtures.txt')],
                            cwd=ROOT, stdout=f, stderr=subprocess.STDOUT).returncode
    log = (out / 'simulation.log').read_text()
    print(log[-6000:])
    counts = re.search(r'PASS MMU integer differential: (\d+) checks, (\d+) programs', log)
    assert all(sha(p) == hashes[str(p)] for p in sources)
    record = dict(passed=rc == 0 and counts is not None, clock_mhz=mhz, fpp='off',
                  combined_abort=combined_abort,
                  microcode_words=stats['used_words'], inputs=hashes, compile_command=command,
                  fixtures_sha256=sha(out / 'fixtures.txt'), log_sha256=sha(out / 'simulation.log'))
    if counts:
        record.update(checks=int(counts[1]), programs=int(counts[2]))
    (out / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
    if not record['passed']:
        raise SystemExit(rc or 1)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--clock-mhz', type=int, choices=(24, 50), default=50)
    p.add_argument('--microcode-source', type=Path, help='optional baseline microcode for the same guest fixtures')
    p.add_argument('--combined-abort', action='store_true',
                   help='reproduce the known C-core/RTL difference for simultaneous MMU abort causes (fails)')
    a = p.parse_args()
    run(a.out.resolve(), a.clock_mhz, a.microcode_source.resolve() if a.microcode_source else None, a.combined_abort)
