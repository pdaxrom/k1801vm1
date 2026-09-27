#!/usr/bin/env python3
"""Run BASIC numerical/exception recovery tests on the full MMU board RTL.

Use an XM image prepared from FB by test_mmu_basic_simh.py. The image is read
only; the real boot loader and peripherals perform every guest transaction.
"""
import argparse
import json
from pathlib import Path
import subprocess
from board_common import ROOT
from build_mmu_board import build, CORE, BOARD, sha


def run(out, image):
    out.mkdir(parents=True, exist_ok=False)
    record = build()
    digest = sha(image)
    template = ROOT / 'tests/mmu/tb_mmu_boot.v'
    cases = ROOT / 'tests/mmu/basic_rt11.vh'
    text = template.read_text().replace('    reg clk=0,', cases.read_text() + '\n    reg clk=0,')
    start = text.index('        phase=1;shell(')
    end = text.index('        $display("PASS MMU RT11:', start)
    text = text[:start] + '''        phase=1;shell("SHOW CONFIGURATION");
        contains("2048KB of memory");contains("22 bit addressing is on");
        phase=2;test_basic("B81FPU",0);
        phase=3;test_basic("B81FPD",1);
        phase=4;shell("SHOW CONFIGURATION");contains("Booted from DM0:RT11XM");
''' + text[end:]
    text = text.replace('PASS MMU RT11:', 'PASS MMU BASIC:')
    text = text.replace('clocks>600000000', 'clocks>1200000000')
    test = out / 'tb.v'
    test.write_text(text)
    inventory = CORE + BOARD + [str(test.relative_to(ROOT)),
        'tests/models/async_sram_model.v', 'tests/models/spi_sd_model.v']
    hashes = {p: sha(ROOT / p) for p in inventory + ['tests/mmu/tb_mmu_boot.v',
        'tests/mmu/basic_rt11.vh', 'tools/test_mmu_basic.py']}
    cmd = ['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD',
           '--top-module', 'tb_mmu_boot', '-j', '4', '--Mdir', str(out / 'obj')] + inventory
    with (out / 'build.log').open('w') as log:
        subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (out / 'simulation.log').open('w') as log:
        result = subprocess.run([str(out / 'obj/Vtb_mmu_boot'), '+MONITOR=xm',
            f'+SD_IMAGE={image}', f'+UART_LOG={out}/uart.txt'], cwd=ROOT,
            stdout=log, stderr=subprocess.STDOUT)
    print((out / 'simulation.log').read_text()[-3000:])
    result.check_returncode()
    assert 'PASS MMU BASIC' in (out / 'simulation.log').read_text()
    assert sha(image) == digest
    assert all(sha(ROOT / p) == h for p, h in hashes.items())
    (out / 'result.json').write_text(json.dumps(dict(passed=True, hardware=record,
        files=hashes, sd_image=str(image), sd_image_sha256=digest), indent=2) + '\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--image', type=Path, required=True)
    a = p.parse_args()
    run(a.out.resolve(), a.image.resolve())
