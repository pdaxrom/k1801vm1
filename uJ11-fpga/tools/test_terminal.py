#!/usr/bin/env python3
"""Validate the software parser and the real SERV/UART/PAL console mirror."""
import argparse
import json
import subprocess
from pathlib import Path
from board_common import ROOT
from build_mmu_board import build, CORE, BOARD, sha
from serv_test import GUARD, memory_args


def run(out):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    hw = build()
    assert hw['terminal'] and hw['video'] and hw['clock_mhz'] == 50
    host = ['tests/test_terminal.c', 'tests/terminal_test.h', 'firmware/storage/terminal.c',
            'firmware/storage/terminal.h', 'firmware/storage/terminal_font.h']
    inventory = CORE + BOARD + [GUARD, 'tests/mmu/tb_terminal.v', 'tests/models/async_sram_model.v']
    files = {p: sha(ROOT / p) for p in set(host + inventory + ['tools/test_terminal.py'])}
    with (out / 'host-build.log').open('w') as log:
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-DUJ11_TERMINAL',
                        '-DUJ11_TERMINAL_TEST', '-Itests', '-Ifirmware/storage', host[0], host[2],
                        '-o', str(out / 'parser')], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    host_log = subprocess.check_output([str(out / 'parser')], text=True)
    (out / 'host.log').write_text(host_log)
    with (out / 'build.log').open('w') as log:
        subprocess.run(['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD',
                        '--top-module', 'tb_terminal', '-j', '4', '--Mdir', str(out / 'obj')] + inventory,
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (out / 'simulation.log').open('w') as log:
        subprocess.run([str(out / 'obj/Vtb_terminal'), f'+OUT={out}'] + memory_args(hw),
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    result = (out / 'simulation.log').read_text()
    assert 'PASS PAL console mirror' in result
    assert all(sha(ROOT / p) == digest for p, digest in files.items())
    (out / 'result.json').write_text(json.dumps(dict(passed=True, hardware=hw, files=files), indent=2) + '\n')
    print(host_log + result)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    run(parser.parse_args().out)
