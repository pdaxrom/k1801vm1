#!/usr/bin/env python3
"""Exercise disk DMA read/write across the terminal SRAM reservation."""
import argparse
import json
import subprocess
from pathlib import Path
from board_common import ROOT
from build_mmu_board import build, CORE, BOARD, sha
from serv_test import GUARD, memory_args
from test_storage_rl_xp import fixture


def run(out):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    hw = build()
    assert hw['terminal'] and hw['video'] and hw['clock_mhz'] == 50
    inventory = CORE + BOARD + [GUARD, 'tests/mmu/tb_terminal_dma.v',
        'tests/models/async_sram_model.v', 'tests/models/spi_sd_model.v']
    files = {p: sha(ROOT / p) for p in inventory + ['tools/test_terminal_dma.py', 'tools/test_storage_rl_xp.py']}
    image = out / 'sd.img'
    fixture(image)
    before = sha(image)
    with (out / 'build.log').open('w') as log:
        subprocess.run(['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD',
                        '--top-module', 'tb_terminal_dma', f'-GKEYBOARD_ENABLE={int(hw["keyboard"])}',
                        '-j', '4', '--Mdir', str(out / 'obj')] + inventory,
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (out / 'simulation.log').open('w') as log:
        subprocess.run([str(out / 'obj/Vtb_terminal_dma'), f'+SD_IMAGE={image}'] + memory_args(hw),
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    text = (out / 'simulation.log').read_text()
    assert 'PASS terminal DMA' in text and sha(image) == before
    assert all(sha(ROOT / p) == digest for p, digest in files.items())
    (out / 'result.json').write_text(json.dumps(dict(passed=True, hardware=hw, files=files,
                                                   image_sha256=before, image_unchanged=True), indent=2) + '\n')
    print(text)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    run(p.parse_args().out)
