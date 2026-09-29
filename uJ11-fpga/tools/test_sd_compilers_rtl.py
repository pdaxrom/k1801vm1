#!/usr/bin/env python3
"""Boot the remapped compiler suite on real HC7000 RTL with stock disk drivers."""
import argparse
import contextlib
import io
import json
from pathlib import Path
import subprocess

from board_common import ROOT
from build_mmu_board import build, CORE, BOARD, sha
from build_sd_compilers import build as compiler_media
from sdcard import Label, MENU, add_partition, publish, copy_bytes
from serv_test import GUARD, memory_args
from storage_menu import unpack


def run(out, menu):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    hardware = build()
    assert hardware['fpp'] == 'off' and hardware['iop']['profile'] == 'storage'
    with contextlib.redirect_stdout(io.StringIO()):
        media = compiler_media(out / 'media')
    # The sole fixture addition is a FORTRAN source on the private work disk.
    source = out / 'CTFORT.FOR'
    source.write_bytes(b"      PROGRAM CTFORT\r\n      WRITE(7,10)\r\n"
                       b"   10 FORMAT(' FORTRAN OK')\r\n      END\r\n")
    subprocess.run([str(ROOT.parent / 'lsi11/rt11tool'), 'add',
                    str(out / 'media/storage.dsk'), str(source), 'CTFORT.FOR'], check=True)
    label = Label(262144, features=MENU)
    for disk in media['disks']:
        label = add_partition(label, disk['media'], disk['unit'],
                              boot=disk['name'] == 'system', name=disk['name'])
    unpack(menu.read_bytes())
    image = out / 'sd.img'
    with image.open('x+b') as stream:
        stream.truncate(label.blocks * 512)
        stream.seek(1024)
        stream.write(menu.read_bytes())
        for partition, disk in zip(label.partitions, media['disks']):
            stream.seek(partition.start * 512)
            with (out / 'media' / disk['file']).open('rb') as src:
                copy_bytes(src, stream, disk['bytes'])
        publish(stream, label)
    before = sha(image)
    bench = 'tests/mmu/tb_mmu_boot_matrix.v'
    inventory = CORE + BOARD + [GUARD, bench, 'tests/models/async_sram_model.v', 'tests/models/spi_sd_model.v']
    inputs = {p: sha(ROOT / p) for p in inventory + ['tools/test_sd_compilers_rtl.py', 'tools/build_sd_compilers.py']}
    with (out / 'build.log').open('w') as log:
        subprocess.run(['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD',
                        '--top-module', 'tb_mmu_boot_matrix', f'-GCLOCK_HZ={hardware["clock_mhz"]*1000000}',
                        '-j', '4', '--Mdir', str(out / 'obj')] + inventory,
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (out / 'simulation.log').open('w') as log:
        subprocess.run([str(out / 'obj/Vtb_mmu_boot_matrix')] + memory_args(hardware) +
                       [f'+SD_IMAGE={image}', f'+UART_LOG={out}/uart.txt', '+CASE=compilers', '+BOOT_MENU', '+ENTER_BOOT'],
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    assert 'PASS MMU matrix compilers' in (out / 'simulation.log').read_text()
    assert sha(image) == before
    assert all(sha(ROOT / p) == digest for p, digest in inputs.items())
    assert all(sha(ROOT.parent / d['source']) == d['source_sha256'] for d in media['disks'])
    result = dict(passed=True, hardware=hardware, inputs=inputs, media=media,
                  sd_sha256=before, menu_sha256=sha(menu), fpp=False, stock_drivers=True,
                  programs=['BASIC: PRINT 2+3', 'Pascal: ADDER with LIBEIS', 'FORTRAN: CTFORT with FORLIB'])
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('PASS HC7000 RTL: stock drivers, BASIC/Pascal/FORTRAN, FPP disabled')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--menu', type=Path, required=True)
    args = parser.parse_args()
    run(args.out, args.menu.resolve())
