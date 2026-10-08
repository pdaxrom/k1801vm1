#!/usr/bin/env python3
"""Real SERV/SD qualification of the TERAK RLV11/RLV12 build; regular images only."""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

from board_common import ROOT
from build_terak import build, CORE, BOARD, GENERATED, OUT
from serv_test import GUARD, memory_args
from terak_mbr import create, ALIGN, FAT_BLOCKS


def run(out):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out/'result.json').unlink(missing_ok=True)
    hardware = build()
    if hardware['hard_disk'] != 'rl':
        raise ValueError('Set UJ11_TERAK_BOOT=bios UJ11_TERAK_RAM_KIB=256 UJ11_TERAK_HD=rl')
    inputs = set(hardware['files']) | {'tools/test_terak_rl.py', 'tools/terak_mbr.py',
             'tests/terak/tb_terak_rl.v', GUARD, 'tests/models/async_sram_model.v',
             'tests/models/spi_sd_model.v'}
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}
    image = out/'card.img'
    image.unlink(missing_ok=True)
    floppy = out/'floppy.raw'
    floppy.write_bytes(bytes(77*26*128))
    rl01, rl02 = out/'rl01.raw', out/'rl02.raw'
    with rl01.open('wb') as stream:
        for block in range(10240):
            stream.write(bytes(((block//20*17)^(block%20*11)^(j*3)^(j>>8))&255 for j in range(512)))
    rl02.write_bytes(bytes(20480*512))
    card = create([('sssd', floppy)], image, [0], hard_disks=[('rl01', rl01), ('rl02', rl02)], hd_readonly=[1])
    sys.path.insert(0, str(ROOT.parent/'bk0010-01-fpga/tools'))
    from prepare_disk_sd import Fat16
    data = bytearray(image.read_bytes())
    volume = Fat16(data[ALIGN*512:(ALIGN+FAT_BLOCKS)*512])
    volume.put('TERAK5.CFG', card['config'].encode())
    volume.put('TBIOS.BIN', (OUT/'bios/TBIOS.BIN').read_bytes())
    data[ALIGN*512:(ALIGN+FAT_BLOCKS)*512] = volume.data
    image.write_bytes(data)
    before = hashlib.sha256(data).hexdigest()
    extent = card['disks'][1]
    top = 'tb_terak_rl'
    commands = [
        ('build', ['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD',
                   '-Wno-PINMISSING', '--top-module', top, '-j', '4', '--Mdir', str(out/'obj')]+
                  [f"-GRAM_BYTES={hardware['guest_ram_bytes']}"]+
                  CORE+BOARD+GENERATED+['tests/terak/'+top+'.v', GUARD,
                                      'tests/models/async_sram_model.v', 'tests/models/spi_sd_model.v']),
        ('controller', [str(out/'obj'/('V'+top))]+memory_args(hardware)+[f'+SD_IMAGE={image}',
                       f'+DATA_BASE={extent["start"]}', f'+DATA_BLOCKS={extent["blocks"]}'])]
    for name, command in commands:
        with (out/(name+'.log')).open('w') as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        text = (out/(name+'.log')).read_text()
        if result.returncode:
            raise RuntimeError(text[-8000:])
    if 'PASS TERAK RL01/RL02' not in text or hashlib.sha256(image.read_bytes()).hexdigest() != before:
        raise AssertionError('RL qualification failed or changed its source image')
    if any(hashlib.sha256((ROOT/p).read_bytes()).hexdigest() != digest for p, digest in hashes.items()):
        raise AssertionError('Source changed during qualification')
    record = dict(passed=True, hardware=hardware, files=hashes, image_sha256=before, card=card,
                  log_sha256=hashlib.sha256((out/'controller.log').read_bytes()).hexdigest())
    (out/'result.json').write_text(json.dumps(record, indent=2)+'\n')
    print(text[-3000:])
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    run(parser.parse_args().out)
