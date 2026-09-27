#!/usr/bin/env python3
"""Snapshot the unchanged CPU/firmware images for the separate HC7000 profile."""
import json
from board_common import ROOT
from build_hardware import build, OUT as BASE
OUT = ROOT/'build/hc7000-hardware'


def hardware():
    record = build()
    OUT.mkdir(parents=True, exist_ok=True)
    for p in BASE.iterdir():
        if p.is_file():
            data=p.read_bytes()
            if p.suffix=='.v':
                data=data.replace(b'build/hardware/', b'build/hc7000-hardware/')
            (OUT/p.name).write_bytes(data)
    portable=(ROOT/'rtl/uj11_rom.v').read_text().replace('build/hardware/', 'build/hc7000-hardware/')
    (OUT/'uj11_rom_model.v').write_text(portable)
    (OUT/'profile.json').write_text(json.dumps(dict(board='hc7000-lcd-sram',
        device='LCMXO2-7000HC-4TG144C',input_clock_hz=12000000,
        system_clock_hz=24000000,source_hardware=record),indent=2)+'\n')
    return record


if __name__=='__main__':
    hardware()
