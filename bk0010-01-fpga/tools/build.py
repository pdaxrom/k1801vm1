#!/usr/bin/env python3
"""Reproducible local build and HDL checks; all generated files stay in build/."""
import argparse
from concurrent.futures import ThreadPoolExecutor
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
from prepare_pal import prepare as prepare_pal
from models import MODELS, bundle as model_bundle, generate_layout, parse_bundle, settings

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
CPU = ['rtl/bk_cpu.v', 'vendor/cpu11/vm1_wb.v', 'build/vm1_plm_masked.v',
       'build/vm1_reg_block.v', 'vendor/cpu11/vm1_tve.v']
SD = ['rtl/bk_cpu_clock.v', 'rtl/bk_wb_cdc.v', 'rtl/bk_system_bus.v', 'rtl/bk_boot_rom.v',
      'rtl/bk_audio.v', 'rtl/bk_ay.v',
      'rtl/bk_keymap.v', 'vendor/uj11/uj11_ps2_keyboard.v',
      'rtl/bk_memory_map.v', 'rtl/bk_sd_partitions.v', 'rtl/bk_fdd.v', 'rtl/bk_fdd_track.v', 'rtl/bk_crc16_word.v',
      'rtl/bk_sd_host.v', 'vendor/uj11/uj11_tick.v',
      'vendor/pyldin/boot_spi_registers.v', 'vendor/pyldin/spi_byte_master.v']
SOC = sorted(str(p.relative_to(ROOT)) for p in (ROOT/'rtl').glob('*.v')) + CPU[1:] + [
    str(p.relative_to(ROOT)) for p in (ROOT/'vendor/uj11').glob('*.v')] + [
    str(p.relative_to(ROOT)) for p in (ROOT/'vendor/pyldin').glob('*.v')]
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
    record = {'name': name, 'passed': passed, 'log_sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'command': [str(x) for x in command],
              'seconds': round(time.monotonic()-started, 3), 'log': str(path.relative_to(ROOT))}
    results.append(record)
    if not passed:
        raise RuntimeError(f'{name} failed ({process.returncode}):\n{path.read_text()[-5000:]}')
    print(f'{name}: PASS ({record["seconds"]} s)', flush=True)
    return path


def source_hashes():
    paths = []
    for folder in ['rtl', 'vendor', 'firmware', 'boards', 'tools', 'tests', 'roms', 'config']:
        paths += [p for p in (ROOT/folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    paths += [ROOT/'Makefile']
    paths += [ROOT.parent/p for p in ['bk0010-01/bk_hw.c', 'bk0010-01/bk_hw.h',
                                     'bk0010-01/bk_model.h', 'bk0010-01/bk_model_layout.h',
                                     'bk0010-01/bk_roms.c', 'bk0010-01/bk_roms.h', 'bk0010-01/bk_rom_contract.h',
                                     'bk0010-01/bk_palettes.h', 'bk0010-01/bk_fdd.c', 'bk0010-01/bk_fdd.h', 'bk0010-01/main.c', 'bk0010-01/x11_gui.c',
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
    prepare_pal()
    generate_layout()
    model_status = []
    for ident in range(5):
        try:
            data = model_bundle(ident)
            parse_bundle(data, ident)
            atomic(BUILD / f'BK{ident:02}.ROM', data)
            model_status.append({'model': ident, 'available': True, 'bytes': len(data)})
        except FileNotFoundError as error:
            model_status.append({'model': ident, 'available': False, 'reason': str(error)})
    atomic(BUILD / 'models.json', json.dumps(model_status, ensure_ascii=False, indent=2) + '\n')
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
             'BK0010.ROM': (BUILD/'BK0010.ROM').read_bytes(), 'BK.SET': settings()}
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


def test(group, dcca_model=None, disk=None, disk_speedup=1, disk_a16m=False, second_disk=None, disk_stop=False):
    if group in ['audio', 'fast', 'all']:
        run('ay-oracle', ['python3', 'tests/test_audio.py'])
        icarus('ay', ['rtl/bk_ay.v'])
        icarus('audio', ['rtl/bk_audio.v', 'rtl/bk_ay.v', 'rtl/bk_cpu_clock.v'])
    if group in ['models', 'all']:
        run('models-format', ['python3', 'tests/test_models.py'])
        run('compile-models-c', [os.environ.get('CC', 'cc'), '-O2', '-Wall', '-Wextra', '-I..',
                                 'tests/models.c', '-o', 'build/models-c'])
        run('models-c', ['build/models-c'])
        icarus('models', ['rtl/bk_memory_map.v'])
    if group in ['reference', 'boot', 'all']:
        run('compile-reference-c', [os.environ.get('CC', 'cc'), '-O2', '-Wall', '-I..',
                                   '-DENABLE_MMU=0', '-DMMU_STUB_REGS_WHEN_DISABLED=0',
                                   'tests/reference.c', '../bk0010-01/bk_hw.c', '../bk0010-01/bk_tape.c',
                                   '../bk0010-01/bk_timing.c', '../bk0010-01/bk_fdd.c', '../core/core.c', '../core/hardware.c',
                                   '../core/disas.c', '-o', 'build/reference-c'])
        run('reference-c', ['build/reference-c'])
        for phase in ['boot', 'print']:
            compare(f'c-{phase}-vram', (BUILD/f'reference-{phase}-vram.bin').read_bytes(),
                    (ROOT/f'tests/golden-{phase}-vram.bin').read_bytes())
    if group in ['fdd', 'all']:
        run('compile-fdd-reference', ['cc', '-std=c11', '-O2', '-I..', '-I../bk0010-01',
            '-DENABLE_MMU=0', '-DMMU_STUB_REGS_WHEN_DISABLED=0',
            'tests/fdd.c', '../bk0010-01/bk_roms.c', '../bk0010-01/bk_hw.c',
            '../bk0010-01/bk_fdd.c', '../bk0010-01/bk_tape.c', '../bk0010-01/bk_timing.c',
            '../core/core.c', '../core/hardware.c', '../core/disas.c', '-o', 'build/fdd-reference'])
        run('fdd-reference', ['build/fdd-reference'])
        run('fdd-reference-a16m', ['build/fdd-reference', 'a16m'])
        run('assemble-fdd-test', [str(ROOT.parent/'microasm11/microasm11'), '--cpu', 'vm1', '-binary',
                                 '--list', 'build/fdd.lst', 'tests/fdd.asm', 'build/fdd-program.bin'])
        program = (BUILD/'fdd-program.bin').read_bytes()
        atomic(BUILD/'fdd-program.hex', ''.join(f'{b:02x}\n' for b in program))
        atomic(BUILD/'disk327.hex', ''.join(f'{b:02x}\n' for b in (ROOT/'roms/DISK_327.ROM').read_bytes()))
        atomic(BUILD/'diska16m.hex', ''.join(f'{b:02x}\n' for b in (ROOT/'roms/DISK_A16M_v2.41.rom').read_bytes()))
        executable = verilate('fdd_native', CPU+['rtl/bk_cpu_clock.v', 'rtl/bk_wb_cdc.v',
                                               'rtl/bk_memory_map.v', 'rtl/bk_fdd.v', 'rtl/bk_fdd_track.v', 'rtl/bk_crc16_word.v'])
        for mode, mhz in enumerate([3,4,6,8,12,16,24]):
            run(f'fdd-native-{mhz}MHz', [executable, f'+mode={mode}'])
            run(f'fdd-native-a16m-{mhz}MHz', [executable, f'+mode={mode}', '+a16m'])
    if group in ['profiles', 'all']:
        run('compile-model-boot-c', ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-I..',
            '-DENABLE_MMU=0', '-DMMU_STUB_REGS_WHEN_DISABLED=0',
            'tests/model_boot.c', '../bk0010-01/bk_roms.c', '../bk0010-01/bk_hw.c',
            '../bk0010-01/bk_fdd.c', '../bk0010-01/bk_tape.c', '../bk0010-01/bk_timing.c',
            '../core/core.c', '../core/hardware.c', '../core/disas.c', '-o', 'build/model-boot-c'])
        run('model-boot-c', ['build/model-boot-c'])
        for model in range(5):
            memory = bytearray(262144)
            _, segments = parse_bundle((BUILD/f'BK{model:02}.ROM').read_bytes(), model)
            for address, data in segments:
                memory[address:address+len(data)] = data
            atomic(BUILD/f'model-{model}-preloaded.hex',
                   ''.join(f'{word:04x}\n' for (word,) in struct.iter_unpack('<H',memory)))
            screen = (BUILD/f'model-{model}-vram.bin').read_bytes()
            assert len(screen) == 16384 and any(screen), 'Empty profile oracle'
            atomic(BUILD/f'model-{model}-screen.hex',
                   ''.join(f'{word:04x}\n' for (word,) in struct.iter_unpack('<H',screen)))
            image = bytearray((BUILD/'bk0010-sd.img').read_bytes())
            image[2048*512:] = fat16({'LOADER.BIN': bytes(481), 'P601.SET': b'P601 test sentinel',
                'LOADERBK.BIN': (BUILD/'LOADERBK.BIN').read_bytes(),
                f'BK{model:02}.ROM': (BUILD/f'BK{model:02}.ROM').read_bytes(),
                'BK.SET': settings(model=model,drives=(0,0,0,0))})
            atomic(BUILD/f'model-{model}-sd.img', image)
        executable = verilate('sd_boot', CPU+SD+['tests/sd_spi_model.v'])
        def boot_profile(model):
            run(f'native-profile-{model}', [executable, '+preloaded', '+no-dump',
                f'+preload=build/model-{model}-preloaded.hex', f'+expected-model={model}',
                f'+screen=build/model-{model}-screen.hex', *(['+native-rate'] if model in (0,3) else [])], timeout=1800)
        with ThreadPoolExecutor(max_workers=2) as workers:
            for task in [workers.submit(boot_profile, model) for model in range(5)]:
                task.result()
        def cold_profile(model):
            run(f'native-profile-{model}-sd', [executable, '+no-dump',
                f'+image=build/model-{model}-sd.img', f'+expected-model={model}',
                f'+screen=build/model-{model}-screen.hex', *(['+native-runtime'] if model in (0,3) else [])], timeout=1800)
        with ThreadPoolExecutor(max_workers=2) as workers:
            for task in [workers.submit(cold_profile, model) for model in range(5)]:
                task.result()
    if group in ['fast', 'all']:
        icarus('reset', SD)
        host = verilate('sd_host', ['rtl/bk_sd_host.v', 'vendor/pyldin/spi_byte_master.v',
                                    'tests/sd_spi_model.v'])
        original = bytes((sector*31+byte*13)&255 for sector in range(32) for byte in range(512))
        for name, args in [('sdhc', []), ('sdsc', ['+sdhc=0']),
                           ('write-error', ['+write-fail-at=1']), ('read-crc', ['+corrupt-read-at=1'])]:
            atomic(BUILD/'sd-host-test.img', original)
            run(f'sd-host-{name}', [host, *args])
        icarus('sd_partitions', ['rtl/bk_sd_partitions.v'])
        icarus('fdd_track', ['rtl/bk_fdd_track.v', 'rtl/bk_crc16_word.v'])
        icarus('clock_stall', ['rtl/bk_cpu_clock.v'])
        icarus('clock_mailbox', ['rtl/bk_cpu_clock.v', 'rtl/bk_clock_mailbox.v'])
        icarus('panel', ['rtl/bk_cpu_clock.v', 'rtl/bk_panel.v', 'rtl/bk_frequency_display.v',
                          'vendor/uj11/uj11_button.v', 'vendor/uj11/uj11_tick.v'])
        icarus('cdc', ['rtl/bk_wb_cdc.v'])
        icarus('keyboard', ['rtl/bk_keymap.v', 'vendor/uj11/uj11_ps2_keyboard.v'])
        icarus('keyboard_guest', SD)
        stop = verilate('stop', CPU+['rtl/bk_cpu_clock.v'])
        run('stop-native-all-rates', [stop], timeout=180)
        icarus('uart', ['rtl/bk_boot_uart.v'])
        icarus('pal_encoder', ['rtl/bk_pal_encoder.v', 'vendor/uj11/uj11_pal_encoder.v'])
        icarus('memory', ['rtl/bk_sram_arbiter.v', 'vendor/uj11/uj11_sram.v', 'tests/sram_model.v'])
        run('lint-soc', ['verilator', '--lint-only', '--timing', '-Wno-fatal', '--top-module', 'bk_computer', *SOC])
        video = verilate('video', ['rtl/bk_pal_video.v', 'rtl/bk_pal_encoder.v', 'rtl/bk_sram_arbiter.v',
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
            compare(f'{name}-rom', memory[0x20000:0x28000], (ROOT/'roms/MONIT10.ROM').read_bytes() +
                    (ROOT/'roms/BASIC10.ROM').read_bytes())
            compare(f'{name}-vram', memory[16384:32768], (ROOT/'tests/golden-boot-vram.bin').read_bytes())
    if group == 'disk-os':
        if disk is None:
            raise ValueError('Pass --disk /absolute/path/to/BK0011M-System.img')
        import sys
        sys.path.insert(0,str(ROOT/'tests'))
        from test_disk_os import run_checks
        run('compile-disk-os-c',['cc','-std=c11','-O2','-Wall','-Wextra','-I..',
            '-DENABLE_MMU=0','-DMMU_STUB_REGS_WHEN_DISABLED=0','tests/disk_os.c',
            '../bk0010-01/bk_roms.c','../bk0010-01/bk_hw.c','../bk0010-01/bk_fdd.c',
            '../bk0010-01/bk_tape.c','../bk0010-01/bk_timing.c','../core/core.c',
            '../core/hardware.c','../core/disas.c','-o','build/disk-os-c'])
        sources = list(SD)
        if disk_speedup != 1:
            original = (ROOT/'rtl/bk_system_bus.v').read_text()
            needle = 'bk_fdd #(.CLOCK_HZ(CLOCK_HZ)) disk_controller'
            assert original.count(needle) == 1
            derived = BUILD/'bk_system_bus-fast-disk-test.v'
            atomic(derived,original.replace(needle,
                f'bk_fdd #(.CLOCK_HZ({29411764//disk_speedup})) disk_controller'))
            sources[sources.index('rtl/bk_system_bus.v')] = str(derived.relative_to(ROOT))
            tested_sources[str(derived.relative_to(ROOT))] = hashlib.sha256(derived.read_bytes()).hexdigest()
        executable = verilate('sd_boot',CPU+sources+['tests/sd_spi_model.v'])
        run_checks(disk,executable,run,compare,disk_a16m,second_disk,disk_stop)
    if group in ['storage', 'all']:
        import sys
        sys.path.insert(0, str(ROOT/'tests'))
        from test_sd_fdd import run_checks
        run('assemble-fdd-test', [str(ROOT.parent/'microasm11/microasm11'), '--cpu', 'vm1', '-binary',
                                 '--list', 'build/fdd.lst', 'tests/fdd.asm', 'build/fdd-program.bin'])
        atomic(BUILD/'fdd-program.hex', ''.join(f'{b:02x}\n' for b in (BUILD/'fdd-program.bin').read_bytes()))
        atomic(BUILD/'disk327.hex', ''.join(f'{b:02x}\n' for b in (ROOT/'roms/DISK_327.ROM').read_bytes()))
        atomic(BUILD/'diska16m.hex', ''.join(f'{b:02x}\n' for b in (ROOT/'roms/DISK_A16M_v2.41.rom').read_bytes()))
        executable = verilate('fdd_sd', CPU+SD+['tests/sd_spi_model.v'])
        run_checks(executable, run)
    if group in ['bios', 'bios-logical', 'all']:
        run('compile-bios-reference', ['cc', '-std=c11', '-O2', '-I..',
            '-DENABLE_MMU=0', '-DMMU_STUB_REGS_WHEN_DISABLED=0',
            'tests/model_boot.c', '../bk0010-01/bk_roms.c', '../bk0010-01/bk_hw.c',
            '../bk0010-01/bk_fdd.c', '../bk0010-01/bk_tape.c', '../bk0010-01/bk_timing.c',
            '../core/core.c', '../core/hardware.c', '../core/disas.c', '-o', 'build/bios-reference'])
        run('bios-reference', ['build/bios-reference'])
        screen = (BUILD/'model-3-vram.bin').read_bytes()
        atomic(BUILD/'model-3-screen.hex', ''.join(f'{word:04x}\n' for (word,) in struct.iter_unpack('<H',screen)))
        import sys
        sys.path.insert(0, str(ROOT/'tests'))
        from test_bios import run_checks
        executable = verilate('sd_boot', CPU+SD+['tests/sd_spi_model.v'])
        run_checks(executable, run,only=['logical-default','logical-edit'] if group == 'bios-logical' else None)
    if group == 'ebr-bios':
        import sys
        sys.path.insert(0,str(ROOT/'tests'))
        from test_ebr_boot import run_checks
        executable = verilate('sd_boot',CPU+SD+['tests/sd_spi_model.v'])
        run_checks(executable,run)
    if group in ['bios-ps2', 'all']:
        media = bytearray((BUILD/'bk0010-sd.img').read_bytes())
        files = {'LOADER.BIN': bytes((i*17+3)&255 for i in range(481)),
                 'P601.SET': b'P601 BIOS PS2 sentinel', 'LOADERBK.BIN': (BUILD/'LOADERBK.BIN').read_bytes(),
                 'BK.SET': settings(model=3,drives=(0,0,0,0))}
        files.update({f'BK{model:02}.ROM':(BUILD/f'BK{model:02}.ROM').read_bytes() for model in range(5)})
        media[2048*512:] = fat16(files)
        path = BUILD/'bios-ps2.img'
        atomic(path,media)
        executable = verilate('bios_keyboard', SOC+['tests/sd_spi_model.v','tests/sram_model.v'])
        for name, flags in [('bios-ps2-backspace-stock',['+native-smoke']),
                            ('bios-ps2-backspace-navigation',['+turbo-bios']),
                            ('bios-ps2-arrow-entry',['+turbo-bios','+countdown-arrow','+native-smoke']),
                            ('bios-ps2-arrow-stock',['+countdown-arrow','+native-smoke'])]:
            run(name, [executable,f'+image={path}',*flags], timeout=1800)
        compare('bios-physical-ps2-sd-unchanged',path.read_bytes(),media)
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
        text = text.replace('module vm1a_pli', 'module vm1a_pli_masked')
        text = text.replace('module vm1g_pli', 'module vm1g_pli_masked')
        (BUILD/'vm1_plm_equivalence.v').write_text(text)
        run('compile-plm', ['iverilog', '-g2012', '-s', 'tb_plm_equivalence', '-o', 'build/plm.vvp',
                           'tests/tb_plm_equivalence.v', 'vendor/cpu11/vm1_plm.v', 'build/vm1_plm_equivalence.v'])
        run('plm-equivalence', ['vvp', 'build/plm.vvp'], timeout=3600)


def main():
    global tested_sources
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'test'], nargs='?', default='build')
    parser.add_argument('--group', choices=['audio', 'fast', 'boot', 'soc', 'all', 'plm', 'reference', 'models', 'fdd', 'profiles', 'bios', 'bios-logical', 'bios-ps2', 'ebr-bios', 'storage', 'disk-os'], default='fast')
    parser.add_argument('--disk',type=Path,help='800 KiB source disk used only by the disk-os simulation')
    parser.add_argument('--second-disk',type=Path,help='Second 800 KiB disk; test both logical partitions 5/6')
    parser.add_argument('--disk-stop',action='store_true',help='Press native STOP to exit the system disk music intro')
    parser.add_argument('--disk-speedup',type=int,choices=[1,4],default=1,
                        help='Test-only disk mechanics acceleration; CPU, frame timer and SPI stay unchanged')
    parser.add_argument('--disk-a16m',action='store_true',help='Also boot the disk with A16M in the C reference')
    parser.add_argument('--assembler', default=os.environ.get('MICROASM11', str(ROOT.parent/'microasm11/microasm11')))
    parser.add_argument('--dcca-model', type=Path,
                        help='Official Diamond DCCA.v: simulate the SYNTHESIS branch of the SoC')
    args = parser.parse_args()
    completed = False
    disk_source = None
    try:
        build(args.assembler)
        tested_sources = source_hashes()
        if args.disk is not None:
            data = args.disk.read_bytes()
            disk_source = {'path':str(args.disk.resolve()), 'bytes':len(data),
                           'sha256':hashlib.sha256(data).hexdigest()}
        if args.action == 'test':
            test(args.group, args.dcca_model, args.disk, args.disk_speedup, args.disk_a16m,args.second_disk,args.disk_stop)
        completed = True
    finally:
        if BUILD.exists():
            suffix = '-diamond' if args.dcca_model else ''
            (BUILD/f'results-{args.group+suffix if args.action == "test" else "build"}.json').write_text(json.dumps({
                'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'passed': completed,
                'group': args.group if args.action == 'test' else 'build',
                'boot_hex_sha256':hashlib.sha256((BUILD/'boot.hex').read_bytes()).hexdigest() if (BUILD/'boot.hex').exists() else None,
                'results': results, 'sources': tested_sources or source_hashes(),
                'disk_source':disk_source,
                'disk_stop':args.disk_stop if args.group == 'disk-os' else None,
                'second_disk_source':{'path':str(args.second_disk.resolve()),
                    'bytes':args.second_disk.stat().st_size,
                    'sha256':hashlib.sha256(args.second_disk.read_bytes()).hexdigest()} if args.second_disk else None,
                'disk_mechanics_speedup':args.disk_speedup if args.group == 'disk-os' else None,
                'dcca_model': {'sha256': hashlib.sha256(args.dcca_model.read_bytes()).hexdigest(),
                               'path': str(args.dcca_model)} if args.dcca_model else None}, indent=2)+'\n')


if __name__ == '__main__':
    main()
