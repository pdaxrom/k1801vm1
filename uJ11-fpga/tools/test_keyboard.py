#!/usr/bin/env python3
"""Validate PS/2 transport, key translation and real SERV/DL11 console input."""
import argparse
import json
import subprocess
from pathlib import Path
from build_mmu_board import build, CORE, BOARD, ROOT, sha
from serv_test import GUARD, memory_args


def run(out):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    hw = build()
    assert hw['keyboard'] and hw['terminal'] and hw['clock_mhz'] == 50
    host = ['tests/test_keyboard.c', 'tests/keyboard_test.h', 'firmware/storage/keyboard.c',
            'firmware/storage/keyboard.h', 'firmware/storage/terminal.h', 'firmware/storage/storage.h']
    inventory = CORE + BOARD + [GUARD, 'tests/mmu/tb_keyboard.v', 'tests/models/async_sram_model.v']
    unit = ['tests/mmu/tb_ps2_keyboard.v', 'boards/hc7000/uj11_ps2_keyboard.v']
    files = {p: sha(ROOT / p) for p in set(host + inventory + unit + ['tools/test_keyboard.py'])}
    with (out / 'host-build.log').open('w') as log:
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-DUJ11_TERMINAL', '-DUJ11_KEYBOARD',
                        '-DUJ11_KEYBOARD_TEST', '-Itests', '-Ifirmware/storage', host[0], host[2],
                        '-o', str(out / 'decoder')], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    (out / 'host.log').write_text(subprocess.check_output([str(out / 'decoder')], text=True))
    with (out / 'unit-build.log').open('w') as log:
        subprocess.run(['iverilog', '-g2012', '-s', 'tb_ps2_keyboard', '-o', str(out / 'receiver')] + unit,
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (out / 'unit.log').open('w') as log:
        subprocess.run(['vvp', str(out / 'receiver')], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (out / 'build.log').open('w') as log:
        subprocess.run(['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD',
                        '--top-module', 'tb_keyboard', '-j', '4', '--Mdir', str(out / 'obj')] + inventory,
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (out / 'simulation.log').open('w') as log:
        subprocess.run([str(out / 'obj/Vtb_keyboard')] + memory_args(hw), cwd=ROOT,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    assert 'PASS SERV PS2/DL11/UART' in (out / 'simulation.log').read_text()
    assert all(sha(ROOT / p) == digest for p, digest in files.items())
    (out / 'result.json').write_text(json.dumps(dict(passed=True, hardware=hw, files=files), indent=2) + '\n')
    for name in ('host.log', 'unit.log', 'simulation.log'):
        print((out / name).read_text())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    run(parser.parse_args().out)
