#!/usr/bin/env python3
"""Qualify external TERAK unit 1 on the actual native bus and serial pins."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from board_common import ROOT
from build_terak import build, CORE, BOARD, GENERATED
from serv_test import memory_args


def run(out):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out/'result.json').unlink(missing_ok=True)
    hardware = build()
    test = 'tests/terak/tb_terak_uart.v'
    model = 'tests/models/async_sram_model.v'
    inputs = set(hardware['files']) | {test, model, 'tools/test_terak_uart.py'}
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}
    command = ['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD',
               '-Wno-PINMISSING', '--top-module', 'tb_terak_uart', '-j', '4', '--Mdir', str(out/'obj'),
               f"-GRAM_BYTES={hardware['guest_ram_bytes']}",
               f"-GDISPLAY_EBR={int(hardware['display_storage'] == 'ebr')}",
               f"-GUART_BAUD={hardware['uart']['baud']}"]+CORE+BOARD+GENERATED+[test, model]
    for name, args in [('build', command), ('uart', [str(out/'obj/Vtb_terak_uart')]+memory_args(hardware))]:
        with (out/(name+'.log')).open('w') as log:
            result = subprocess.run(args, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        output = (out/(name+'.log')).read_text()
        if result.returncode:
            raise RuntimeError(output[-8000:])
    if 'PASS TERAK UART' not in output:
        raise AssertionError('UART qualification did not finish')
    if any(hashlib.sha256((ROOT/p).read_bytes()).hexdigest() != digest for p, digest in hashes.items()):
        raise AssertionError('Sources changed during UART qualification')
    record = dict(passed=True, hardware=hardware, files=hashes,
                  log_sha256=hashlib.sha256((out/'uart.log').read_bytes()).hexdigest())
    (out/'result.json').write_text(json.dumps(record, indent=2)+'\n')
    print(output.strip())
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    run(parser.parse_args().out)
