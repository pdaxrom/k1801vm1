#!/usr/bin/env python3
"""Validate the requested boot matrix on current HC7000 storage RTL.

Unsupported boot controllers are recorded as limitations, never as OS passes.
Only fresh regular SD files are created; source disks are opened read-only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from board_common import ROOT
from build_mmu_board import build, CORE, BOARD
from sdcard import Label, MENU, add_partition, publish, copy_bytes, read_label
from storage_menu import unpack
from test_os_boot_simh import CASES, sha


def make_card(case, out):
    out.mkdir(parents=True, exist_ok=False)
    label = Label(1048576, features=MENU)
    source = ROOT.parent / 'lsi11/disks'
    hashes = {name: sha(source / name) for _, name in CASES[case]}
    for index, (unit, name) in enumerate(CASES[case]):
        media = {'hk': 'rk07', 'rk': 'rk05', 'rq': 'mscp'}[unit[:2]]
        kwargs = {'blocks': (source / name).stat().st_size // 512} if media == 'mscp' else {}
        label = add_partition(label, media, int(unit[2:]), boot=index == 0, name=unit, **kwargs)
    menu = ROOT / 'releases/hc7000-bsd/menu/menu.img'
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


def run(out):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    hardware = build()
    template = ROOT / 'tests/mmu/tb_mmu_boot.v'
    text = template.read_text().replace('module tb_mmu_boot;', 'module tb_mmu_boot_matrix;')
    text = text.replace('    reg clk=0,', '''    bit unsupported;
    integer absent_rk=0;
    initial unsupported=$test$plusargs("UNSUPPORTED");
    always @(posedge clk)
        if(dut.request && dut.ready && dut.bus.error && dut.address>=22'o17777400 && dut.address<=22'o17777416)
            absent_rk++;
    reg clk=0,''')
    text = text.replace('if(stopped)$fatal', 'if(stopped && !unsupported)$fatal')
    start = text.index('        settled_prompt();')
    end = text.index('    end\nendmodule', start)
    text = text[:start] + '''        if(unsupported)begin
            wait(dut.bus.storage_status[15]);
            check(dut.bus.storage_status==16'hc007,"unsupported boot reports startup error 7");
            wait(stopped);
            check(!boot_complete && card.writes==0,"unsupported boot stops before guest disk writes");
            $display("PASS MMU matrix limitation: unsupported boot, status=c007, ODT, no disk writes");
            $finish;
        end
        settled_prompt();contains("RT-11XM");check(boot_complete,"RH0 bootstrap released");
        check(dut.bus.disk.registers.present==8'h03,"both RH disks attached");
        shell("SET SL OFF");
        phase=1;shell("SHOW CONFIGURATION");contains("Booted from DM0:RT11XM");
        contains("2048KB of memory");contains("22 bit addressing is on");
        phase=2;shell("SHOW MEMORY");contains("10000000  MEMTOP");
        phase=3;shell("DIR DM1:");contains("ADDER");
        phase=4;shell("DIR RK0:BASIC.SAV");contains("?DIR");
        check(absent_rk>0,"RK05 access returns a physical bus error");
        check(mmu_fetches>1000 && dma_words>1000,"MMU and autonomous RH DMA active");
        $display("PASS MMU matrix RH0: %0d checks, %0d clocks, %0d DMA words; RK05 unavailable",checks,clocks,dma_words);
        $finish;
''' + text[end:]
    testbench = out / 'tb_mmu_boot_matrix.v'
    testbench.write_text(text)
    inventory = CORE + BOARD + [str(testbench.relative_to(ROOT)),
                               'tests/models/async_sram_model.v', 'tests/models/spi_sd_model.v']
    files = {p: sha(ROOT / p) for p in inventory + ['tests/mmu/tb_mmu_boot.v',
             'tools/test_os_boot_rtl.py', 'tools/test_os_boot_simh.py']}
    with (out / 'build.log').open('w') as log:
        subprocess.run(['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD',
                        '--top-module', 'tb_mmu_boot_matrix', '-j', '4', '--Mdir', str(out / 'obj')]
                       + inventory, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    records = []
    for case in CASES:
        image, media = make_card(case, out / case)
        args = [str(out / 'obj/Vtb_mmu_boot_matrix'), f'+SD_IMAGE={image}',
                f'+UART_LOG={out / case}/uart.txt', '+MONITOR=xm', '+BOOT_MENU']
        if case != 'rt11xm':
            args.append('+UNSUPPORTED')
        with (out / case / 'simulation.log').open('w') as log:
            subprocess.run(args, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        log = (out / case / 'simulation.log').read_text()
        assert 'PASS MMU matrix' in log
        assert sha(image) == media['image_sha256']
        assert all(sha(ROOT.parent / 'lsi11/disks' / name) == digest for name, digest in media['sources'].items())
        result = dict(case=case, validation_completed=True, boot_passed=case == 'rt11xm',
                      all_devices_supported=False, unsupported=['rk0', 'rk1', 'rk2'] if case == 'rt11xm'
                      else ['rk0'] if case == 'rt11v4' else ['rq0', 'rq1'],
                      media=media, originals_unchanged=True)
        (out / case / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        records.append(result)
        print(case + ': ' + log[log.index('PASS MMU matrix'):].splitlines()[0], flush=True)
    assert all(sha(ROOT / p) == digest for p, digest in files.items())
    record = dict(validation_completed=True, all_configurations_passed=False,
                  hardware=hardware, files=files, cases=records)
    (out / 'result.json').write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    run(parser.parse_args().out)
