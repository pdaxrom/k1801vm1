#!/usr/bin/env python3
"""Validate the requested boot matrix on current HC7000 storage RTL.

The original RSX RQ0 placeholder is checked separately from bootable RQ1.
Only fresh regular SD files are created; source disks are opened read-only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from serv_test import GUARD,memory_args
from board_common import ROOT
from build_mmu_board import build, CORE, BOARD
from sdcard import Label, MENU, add_partition, publish, copy_bytes, read_label
from storage_menu import unpack
from test_os_boot_simh import CASES, sha


def make_card(case, out, menu, boot_unit=0):
    out.mkdir(parents=True, exist_ok=False)
    label = Label(1048576, features=MENU)
    source = ROOT.parent / 'lsi11/disks'
    hashes = {name: sha(source / name) for _, name in CASES[case]}
    for index, (unit, name) in enumerate(CASES[case]):
        media = {'hk': 'rk07', 'rk': 'rk05', 'rq': 'mscp'}[unit[:2]]
        kwargs = {'blocks': (source / name).stat().st_size // 512} if media == 'mscp' else {}
        label = add_partition(label, media, int(unit[2:]), boot=index == boot_unit, name=unit, **kwargs)
    unpack(menu.read_bytes())
    image = out / 'sd.img'
    with image.open('x+b') as stream:
        stream.truncate(label.blocks * 512)
        stream.seek(1024)
        stream.write(menu.read_bytes())
        for partition, (_, name) in zip(label.partitions, CASES[case]):
            size = (source / name).stat().st_size
            assert size % 512 == 0 and size <= partition.blocks * 512
            stream.seek(partition.start * 512)
            with (source / name).open('rb') as disk:
                copy_bytes(disk, stream, size)
        publish(stream, label)
        assert read_label(stream)[0] == label
        for partition, (_, name) in zip(label.partitions, CASES[case]):
            stream.seek(partition.start * 512)
            assert hashlib.sha256(stream.read((source / name).stat().st_size)).hexdigest() == hashes[name]
    record = dict(case=case, sources=hashes, image_sha256=sha(image),
                  partitions=[vars(p) for p in label.partitions], menu_sha256=sha(menu))
    (out / 'image.json').write_text(json.dumps(record, indent=2) + '\n')
    return image, record


def run(out, menu, selected):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    hardware = build()
    testbench = ROOT / 'tests/mmu/tb_mmu_boot_matrix.v'
    inventory = CORE + BOARD + [GUARD,str(testbench.relative_to(ROOT)),
                               'tests/models/async_sram_model.v', 'tests/models/spi_sd_model.v']
    files = {p: sha(ROOT / p) for p in inventory + ['tools/test_os_boot_rtl.py', 'tools/test_os_boot_simh.py','tools/serv_test.py']}
    with (out / 'build.log').open('w') as log:
        subprocess.run(['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD',
                        '--top-module', 'tb_mmu_boot_matrix', f'-GCLOCK_HZ={hardware["clock_mhz"]*1000000}', '-j', '4', '--Mdir', str(out / 'obj')]
                       + inventory, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    records = []
    for case in selected:
        source_case='rsx' if case.startswith('rsx') else case
        boot_unit=int(case[-1]) if case.startswith('rsx') else 0
        image, media = make_card(source_case, out / case, menu, boot_unit)
        args = [str(out / 'obj/Vtb_mmu_boot_matrix')]+memory_args(hardware)+[ f'+SD_IMAGE={image}',
                f'+UART_LOG={out / case}/uart.txt', f'+CASE={case}', '+BOOT_MENU']
        with (out / case / 'simulation.log').open('w') as log:
            subprocess.run(args, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        log = (out / case / 'simulation.log').read_text()
        assert 'PASS MMU matrix' in log
        assert sha(image) == media['image_sha256']
        assert all(sha(ROOT.parent / 'lsi11/disks' / name) == digest for name, digest in media['sources'].items())
        result = dict(case=case, validation_completed=True, boot_passed=case!='rsx-rq0',
                      expected_nonbootable=case=='rsx-rq0', all_devices_supported=True,
                      media=media, originals_unchanged=True)
        if case=='rsx-rq0':result['reason']='Original RQ0 contains a nonbootable placeholder, also confirmed in SIMH'
        (out / case / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        records.append(result)
        print(case + ': ' + log[log.index('PASS MMU matrix'):].splitlines()[0], flush=True)
    assert all(sha(ROOT / p) == digest for p, digest in files.items())
    record = dict(validation_completed=True, checks_passed=True, all_devices_supported=True,
                  hardware=hardware, files=files, cases=records)
    (out / 'result.json').write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--menu',type=Path,required=True)
    parser.add_argument('--case',action='append',choices=('rt11xm','rt11v4','rsx-rq0','rsx-rq1'))
    a=parser.parse_args()
    run(a.out,a.menu.resolve(),a.case or ['rt11xm','rt11v4','rsx-rq0','rsx-rq1'])
