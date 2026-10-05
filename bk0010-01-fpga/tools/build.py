#!/usr/bin/env python3
"""Reproducible local build and HDL checks; all generated files stay in build/."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time

from make_sd import build as sd_image, bundle, fat16, validate_bundle
from prepare_cpu11 import prepare

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
CPU = ['rtl/bk_cpu.v', 'vendor/cpu11/vm1_wb.v', 'build/vm1_plm_masked.v',
       'vendor/cpu11/vm1_reg.v', 'vendor/cpu11/vm1_tve.v']
SD = ['rtl/bk_wb_cdc.v', 'rtl/bk_system_bus.v', 'rtl/bk_boot_rom.v',
      'vendor/pyldin/boot_spi_registers.v', 'vendor/pyldin/spi_byte_master.v']
SOC = sorted(str(p.relative_to(ROOT)) for p in (ROOT/'rtl').glob('*.v')) + CPU[1:] + [
    str(p.relative_to(ROOT)) for p in (ROOT/'vendor/uj11').glob('*.v')] + SD[-2:]
results = []
tested_sources = None


def atomic(path, data):
    temporary = path.with_name(path.name + f'.{os.getpid()}.new')
    temporary.write_bytes(data.encode() if isinstance(data, str) else data)
    temporary.replace(path)


def run(name, command, timeout=600):
    print(f'{name} ...', flush=True)
    started = time.monotonic()
    path = BUILD / f'{name}.log'
    with path.open('w') as log:
        process = subprocess.run([str(x) for x in command], cwd=ROOT,
                                 stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
    log_text = path.read_text()
    passed = process.returncode == 0 and '%Fatal:' not in log_text and 'FATAL:' not in log_text
    record = {'name': name, 'passed': passed, 'command': [str(x) for x in command],
              'seconds': round(time.monotonic()-started, 3), 'log': str(path.relative_to(ROOT))}
    results.append(record)
    if not passed:
        raise RuntimeError(f'{name} failed ({process.returncode}):\n{path.read_text()[-5000:]}')
    print(f'{name}: PASS ({record["seconds"]} s)', flush=True)
    return path


def source_hashes():
    paths = []
    for folder in ['rtl', 'vendor', 'firmware', 'boards', 'tools', 'tests', 'roms']:
        paths += [p for p in (ROOT/folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    paths += [ROOT.parent/p for p in ['bk0010-01/bk_hw.c', 'bk0010-01/bk_hw.h',
                                     'bk0010-01/bk_tape.c', 'bk0010-01/bk_tape.h',
                                     'bk0010-01/bk_timing.c', 'bk0010-01/bk_timing.h',
                                     'core/core.c', 'core/core.h', 'core/hardware.c',
                                     'core/hardware.h', 'core/disas.c']]
    return {os.path.relpath(p, ROOT): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}


def build(assembler):
    BUILD.mkdir(exist_ok=True)
    manifest = json.loads((ROOT/'vendor/manifest.json').read_text())
    for item in manifest['files']:
        assert hashlib.sha256((ROOT/item['path']).read_bytes()).hexdigest() == item['sha256'], item['path']
    prepare()
    for name in ['boot', 'loader']:
        binary = 'boot.bin' if name == 'boot' else 'LOADERBK.BIN'
        run(f'assemble-{name}', [assembler, '--cpu', 'vm1', '-binary', '--list',
                                f'build/{name}.lst', f'firmware/{name}/{name}.asm', f'build/{binary}'])
    primary = (BUILD/'boot.bin').read_bytes()
    if len(primary) > 8192 or len(primary) % 2:
        raise ValueError('Resident bootstrap must be even-sized and fit the 8 KiB overlay')
    padded = primary.ljust(8192, b'\0')
    atomic(BUILD/'boot.hex', ''.join(f'{w:04x}\n' for (w,) in struct.iter_unpack('<H', padded)))
    monitor = (ROOT/'roms/MONIT10.ROM').read_bytes()
    basic = (ROOT/'roms/BASIC10.ROM').read_bytes()
    image = sd_image((BUILD/'LOADERBK.BIN').read_bytes(), monitor, basic)
    atomic(BUILD/'bk0010-sd.img', image)
    rom = bundle(monitor, basic)
    assert validate_bundle(rom) == monitor + basic
    atomic(BUILD/'BK0010.ROM', rom)
    reference = bytes(32768) + monitor + basic
    atomic(BUILD/'reference-memory.hex', ''.join(f'{b:02x}\n' for b in reference))
    (BUILD/'artifacts.json').write_text(json.dumps({
        'files': {name: {'bytes': (BUILD/name).stat().st_size,
                        'sha256': hashlib.sha256((BUILD/name).read_bytes()).hexdigest()}
                  for name in ['boot.bin', 'LOADERBK.BIN', 'boot.hex', 'BK0010.ROM', 'bk0010-sd.img']},
        'sources': source_hashes()}, indent=2)+'\n')
    print(f'Image ready: {BUILD/"bk0010-sd.img"}', flush=True)


def verilate(name, sources, defines=()):
    run(f'compile-{name}', ['verilator', '--binary', '--timing', '-j', '2', '-Wno-fatal', '-CFLAGS', '-O3',
                          '--top-module', f'tb_{name}', '--Mdir', f'build/verilated-{name}',
                          *defines, f'tests/tb_{name}.v', *sources])
    return f'build/verilated-{name}/Vtb_{name}'


def icarus(name, sources):
    run(f'compile-{name}', ['iverilog', '-g2012', '-s', f'tb_{name}', '-o',
                          f'build/{name}.vvp', f'tests/tb_{name}.v', *sources])
    run(name, ['vvp', f'build/{name}.vvp'])


def hex_bytes(path, word=False):
    items = [int(s, 16) for line in Path(path).read_text().splitlines() if not line.startswith('//')
             for s in line.split()]
    return b''.join(struct.pack('<H', w) for w in items) if word else bytes(items)


def compare(name, actual, expected):
    if actual != expected:
        mismatch = sum(a != b for a, b in zip(actual, expected))
        raise RuntimeError(f'{name}: byte mismatch ({mismatch}, sizes {len(actual)}/{len(expected)})')
    results.append({'name': name, 'passed': True, 'bytes': len(actual),
                    'sha256': hashlib.sha256(actual).hexdigest()})
    print(f'{name}: PASS, {len(actual)} bytes identical', flush=True)


def damaged_images():
    original = (BUILD/'bk0010-sd.img').read_bytes()
    base = 2048*512
    fat = base + 512
    root = base + 257*512
    data = base + 289*512
    first = struct.unpack_from('<H', original, root+32+26)[0]
    offset = data+(first-2)*512
    cases = [('bad-mbr', 510, 0x11), ('bad-bpb', base+11, 0x12),
             ('bad-header', offset+64, 0x41), ('bad-payload', offset+512+100, 0x42)]
    images = []
    for name, position, code in cases:
        changed = bytearray(original)
        changed[position] ^= 1
        path = BUILD/f'{name}.img'
        path.write_bytes(changed)
        images.append((name, path, code))
    truncated = bytearray(original)
    for table in [fat, fat+128*512]:
        struct.pack_into('<H', truncated, table+first*2, 0xffff)
    path = BUILD/'bad-chain.img'
    path.write_bytes(truncated)
    images.append(('bad-chain', path, 0x31))
    # Reverse the physical cluster order while keeping the logical file bytes.
    fragmented = bytearray(original)
    chain = list(range(first, first+65))
    shuffled = chain[::-1]
    for i, cluster in enumerate(shuffled):
        destination = data+(cluster-2)*512
        fragmented[destination:destination+512] = original[offset+i*512:offset+(i+1)*512]
        for table in [fat, fat+128*512]:
            struct.pack_into('<H', fragmented, table+cluster*2, shuffled[i+1] if i+1 < 65 else 0xffff)
    struct.pack_into('<H', fragmented, root+32+26, shuffled[0])
    (BUILD/'fragmented.img').write_bytes(fragmented)
    # A shared P601 card may have a different CPU's LOADER.BIN first in root.
    # It must neither shadow LOADERBK.BIN nor serve as a fallback when absent.
    files = {'LOADER.BIN': bytes(481), 'LOADERBK.BIN': (BUILD/'LOADERBK.BIN').read_bytes(),
             'BK0010.ROM': (BUILD/'BK0010.ROM').read_bytes()}
    coexist = bytearray(original)
    coexist[base:] = fat16(files)
    (BUILD/'coexist-loader.img').write_bytes(coexist)
    del files['LOADERBK.BIN']
    missing = bytearray(original)
    missing[base:] = fat16(files)
    path = BUILD/'missing-bk-loader.img'
    path.write_bytes(missing)
    images.append(('missing-bk-loader', path, 0x31))
    return images


def test(group, dcca_model=None):
    if group in ['reference', 'boot', 'all']:
        run('compile-reference-c', [os.environ.get('CC', 'cc'), '-O2', '-Wall', '-I..',
                                   '-DENABLE_MMU=0', '-DMMU_STUB_REGS_WHEN_DISABLED=0',
                                   'tests/reference.c', '../bk0010-01/bk_hw.c', '../bk0010-01/bk_tape.c',
                                   '../bk0010-01/bk_timing.c', '../core/core.c', '../core/hardware.c',
                                   '../core/disas.c', '-o', 'build/reference-c'])
        run('reference-c', ['build/reference-c'])
        for phase in ['boot', 'print']:
            compare(f'c-{phase}-vram', (BUILD/f'reference-{phase}-vram.bin').read_bytes(),
                    (ROOT/f'tests/golden-{phase}-vram.bin').read_bytes())
    if group in ['fast', 'all']:
        icarus('panel', ['rtl/bk_cpu_clock.v', 'rtl/bk_panel.v', 'rtl/bk_frequency_display.v',
                          'vendor/uj11/uj11_button.v', 'vendor/uj11/uj11_tick.v'])
        icarus('cdc', ['rtl/bk_wb_cdc.v'])
        icarus('keyboard', ['rtl/bk_keymap.v', 'vendor/uj11/uj11_ps2_keyboard.v'])
        icarus('uart', ['rtl/bk_boot_uart.v'])
        icarus('memory', ['rtl/bk_sram_arbiter.v', 'vendor/uj11/uj11_sram.v', 'tests/sram_model.v'])
        run('lint-soc', ['verilator', '--lint-only', '--timing', '-Wno-fatal', '--top-module', 'bk_computer', *SOC])
        video = verilate('video', ['rtl/bk_pal_video.v', 'rtl/bk_sram_arbiter.v',
                                  'vendor/uj11/uj11_sram.v', 'vendor/uj11/uj11_pal_timing.v',
                                  'vendor/uj11/uj11_pal_encoder.v', 'tests/sram_model.v'])
        run('video-mono-colour', [video])
    if group in ['boot', 'all']:
        executable = verilate('reference_boot', CPU)
        run('reference-boot', [executable, '+keyboard'])
        for phase in ['boot', 'print']:
            compare(f'reference-{phase}-vram', hex_bytes(BUILD/f'rtl-{phase}-vram.hex'),
                    (ROOT/f'tests/golden-{phase}-vram.bin').read_bytes())
        executable = verilate('sd_boot', CPU+SD+['tests/sd_spi_model.v'])
        for name, path, error in damaged_images():
            run(name, [executable, f'+image={path}', f'+error={error:02x}'])
        run('bad-sram', [executable, '+corrupt-sram', '+error=43'])
        for name, args in [('sdhc', []), ('sdsc-v1', ['+sdhc=0', '+version2=0']),
                           ('fragmented', ['+image=build/fragmented.img']),
                           ('coexist-loader', ['+image=build/coexist-loader.img'])]:
            run(f'boot-{name}', [executable, *args])
            memory = hex_bytes(BUILD/'sd-boot-memory.hex', word=True)
            compare(f'{name}-rom', memory[32768:], (ROOT/'roms/MONIT10.ROM').read_bytes() +
                    (ROOT/'roms/BASIC10.ROM').read_bytes())
            compare(f'{name}-vram', memory[16384:32768], (ROOT/'tests/golden-boot-vram.bin').read_bytes())
    if group in ['soc', 'all']:
        sources = SOC+['tests/sd_spi_model.v', 'tests/sram_model.v']
        if dcca_model:
            sources.append(str(dcca_model))
        executable = verilate('computer', sources, ['-DSYNTHESIS'] if dcca_model else [])
        run('computer', [executable], timeout=1800)
        compare('computer-boot-vram', hex_bytes(BUILD/'computer-boot-vram.hex', word=True),
                (ROOT/'tests/golden-boot-vram.bin').read_bytes())
        compare('computer-print-vram', hex_bytes(BUILD/'computer-print-vram.hex', word=True),
                (ROOT/'tests/golden-print-vram.bin').read_bytes())
        compare('computer-rom', hex_bytes(BUILD/'computer-rom.hex', word=True),
                (ROOT/'roms/MONIT10.ROM').read_bytes() + (ROOT/'roms/BASIC10.ROM').read_bytes())
    if group == 'plm':
        text = (BUILD/'vm1_plm_masked.v').read_text().replace('module vm1a_plm', 'module vm1a_plm_masked')
        text = text.replace('module vm1g_plm', 'module vm1g_plm_masked')
        (BUILD/'vm1_plm_equivalence.v').write_text(text)
        run('compile-plm', ['iverilog', '-g2012', '-s', 'tb_plm_equivalence', '-o', 'build/plm.vvp',
                           'tests/tb_plm_equivalence.v', 'vendor/cpu11/vm1_plm.v', 'build/vm1_plm_equivalence.v'])
        run('plm-equivalence', ['vvp', 'build/plm.vvp'], timeout=3600)


def main():
    global tested_sources
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'test'], nargs='?', default='build')
    parser.add_argument('--group', choices=['fast', 'boot', 'soc', 'all', 'plm', 'reference'], default='fast')
    parser.add_argument('--assembler', default=os.environ.get('MICROASM11', str(ROOT.parent/'microasm11/microasm11')))
    parser.add_argument('--dcca-model', type=Path,
                        help='Official Diamond DCCA.v: simulate the SYNTHESIS branch of the SoC')
    args = parser.parse_args()
    completed = False
    try:
        build(args.assembler)
        tested_sources = source_hashes()
        if args.action == 'test':
            test(args.group, args.dcca_model)
        completed = True
    finally:
        if BUILD.exists():
            suffix = '-diamond' if args.dcca_model else ''
            (BUILD/f'results-{args.group+suffix if args.action == "test" else "build"}.json').write_text(json.dumps({
                'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'passed': completed,
                'group': args.group if args.action == 'test' else 'build',
                'results': results, 'sources': tested_sources or source_hashes(),
                'dcca_model': {'sha256': hashlib.sha256(args.dcca_model.read_bytes()).hexdigest(),
                               'path': str(args.dcca_model)} if args.dcca_model else None}, indent=2)+'\n')


if __name__ == '__main__':
    main()
