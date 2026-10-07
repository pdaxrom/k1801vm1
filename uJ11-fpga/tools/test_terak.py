#!/usr/bin/env python3
"""Terak unit/PIO/media checks followed by a cold UCSD boot on the real RTL."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from board_common import ROOT
from build_terak import build, CORE, BOARD, GENERATED, CPU_OUT
from build_terak_card import build as build_card
from serv_test import GUARD, memory_args
from terak_image import card
from terak_mbr import create as mbr_card
from render_terak_screen import render


def run(out, image=None, skip_boot=False, vendor=None, boot_only=False, cases_filter=None):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out/'result.json').unlink(missing_ok=True)
    hardware = build()
    if hardware['boot_mode'] == 'bios':
        if image or skip_boot or vendor or boot_only or cases_filter:
            raise ValueError('BIOS uses its dedicated menu/reset qualification; use tools/test_terak_bios.py')
        from test_terak_bios import run as bios_run
        return bios_run(out)
    rtl_parameters = [f"-GRAM_BYTES={hardware['guest_ram_bytes']}"]
    if hardware['display_storage'] == 'ebr':
        rtl_parameters.append('-GDISPLAY_EBR=1')
    tests = ['tests/terak/tb_terak_'+n+'.v' for n in ('qx', 'console', 'timing', 'video', 'display_ram', 'memory', 'bevnt', 'disk', 'qb', 'boot', 'button')]
    inputs = set(hardware['files']) | set(tests) | {GUARD, 'tests/models/async_sram_model.v',
              'tests/models/spi_sd_model.v', 'tests/terak/test_keyboard.c', 'tests/keyboard_test.h',
              'tests/test_terak_image.py', 'tests/test_terak_mbr.py', 'tests/terak/test_media.c',
              'tests/test_terak_bios.py', 'tests/terak/test_bios_media.c',
              'tests/terak/qb_boot_fixture.asm', 'tests/terak/qb_boot_fixture.mem',
              'tools/test_terak.py', 'tools/serv_test.py',
              'tools/render_terak_screen.py'}
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}

    def execute(name, command):
        with (out/(name+'.log')).open('w') as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        text = (out/(name+'.log')).read_text()
        if result.returncode:
            print(text[-8000:])
            raise RuntimeError(name+' failed; preserved '+str(out/(name+'.log')))
        return text

    execute('image', [sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_terak_image.py', '-v'])
    execute('mbr', [sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_terak_mbr.py', '-v'])
    execute('bios-media', [sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_terak_bios.py', '-v'])
    execute('keyboard-build', ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-DUJ11_TERAK',
            '-DUJ11_KEYBOARD', '-DUJ11_KEYBOARD_TEST', '-Itests', '-Ifirmware/storage',
            'tests/terak/test_keyboard.c', 'firmware/storage/keyboard.c', '-o', str(out/'keyboard')])
    print(execute('keyboard', [str(out/'keyboard')]).strip())
    for name in ('qx', 'console', 'timing', 'video', 'display_ram', 'button'):
        top = 'tb_terak_'+name
        inventory = ['tests/terak/'+top+'.v', 'boards/hc7000/terak/uj11_terak_'+name+'.v']
        if name == 'video':
            inventory += ['boards/hc7000/terak/uj11_terak_timing.v', 'boards/hc7000/video/uj11_pal_ram.v']
        execute(name+'-build', ['iverilog', '-g2012', '-s', top, '-o', str(out/name)]+inventory)
        print(execute(name, ['vvp', str(out/name)]).strip())
    vendor_hashes = {}
    if vendor:
        files = [vendor/(n+'.v') for n in ('DP8KC', 'GSR', 'PUR')]
        vendor_hashes = {str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
        execute('video-vendor-build', ['iverilog', '-g2012', '-DUJ11_VIDEO_VENDOR_RAM', '-s', 'tb_terak_video',
                '-o', str(out/'video-vendor'), 'tests/terak/tb_terak_video.v',
                'boards/hc7000/terak/uj11_terak_video.v', 'boards/hc7000/terak/uj11_terak_timing.v',
                'boards/hc7000/video/uj11_pal_ram.v']+[str(p) for p in files])
        print(execute('video-vendor', ['vvp', str(out/'video-vendor')]).strip())
        execute('display-vendor-build', ['iverilog', '-g2012', '-DUJ11_VIDEO_VENDOR_RAM',
                '-s', 'tb_terak_display_ram', '-o', str(out/'display-vendor'),
                'tests/terak/tb_terak_display_ram.v', 'boards/hc7000/terak/uj11_terak_display_ram.v']+
                [str(p) for p in files])
        print(execute('display-vendor', ['vvp', str(out/'display-vendor')]).strip())
    execute('bevnt-build', ['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD',
            '-Wno-PINMISSING', '--top-module', 'tb_terak_bevnt', '-j', '4', '--Mdir', str(out/'bevnt-obj')]+
            rtl_parameters+CORE+BOARD+GENERATED+['tests/terak/tb_terak_bevnt.v', 'tests/models/async_sram_model.v'])
    print(execute('bevnt', [str(out/'bevnt-obj/Vtb_terak_bevnt')]+memory_args(hardware)).strip())
    execute('memory-build', ['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD',
            '-Wno-PINMISSING', '--top-module', 'tb_terak_memory', '-j', '4', '--Mdir', str(out/'memory-obj')]+
            rtl_parameters+CORE+BOARD+GENERATED+['tests/terak/tb_terak_memory.v', 'tests/models/async_sram_model.v'])
    print(execute('memory', [str(out/'memory-obj/Vtb_terak_memory')]).strip())
    sys.path.insert(0, str(ROOT/'tests'))
    from test_terak_image import fixture, double_fixture, boot_fixture
    (out/'fixture.IMD').write_bytes(fixture())
    card([out/'fixture.IMD'], out/'fixture.img')
    backup = bytearray((out/'fixture.img').read_bytes())
    backup[100] ^= 1
    (out/'backup.img').write_bytes(backup)
    card([out/'fixture.IMD'], out/'readonly.img', [0])
    (out/'double.IMD').write_bytes(double_fixture())
    card([out/'double.IMD'], out/'double.img')
    card([out/'double.IMD'], out/'double-ro.img', [0])
    cases = [('qb', out/'double.img', []), ('qb-readonly', out/'double-ro.img', ['+READONLY']), ('disk', out/'fixture.img', []), ('disk-backup', out/'backup.img', []),
             ('disk-readonly', out/'readonly.img', ['+READONLY'])]

    def mbr(name, disks, ro=(), logical=False, discard=False):
        target = out/(name+'.img')
        target.unlink(missing_ok=True)
        mbr_card(disks, target, ro, logical, discard_status=discard)
        return target

    cases += [('qb-mbr-fd', mbr('qb-mbr-fd', [('fd', out/'double.IMD')]), []),
              ('disk-mbr-fd', mbr('disk-mbr-fd', [('fd', out/'fixture.IMD')]), []),
              ('qb-raw', mbr('qb-raw', [('terak-dsdd', out/'double.IMD')], logical=True, discard=True), ['+RAW']),
              ('qb-raw-readonly', mbr('qb-raw-ro', [('terak-dsdd', out/'double.IMD')], [0], True, True), ['+RAW', '+READONLY']),
              ('disk-raw', mbr('disk-raw', [('sssd', out/'fixture.IMD')], discard=True), ['+RAW']),
              ('disk-raw-readonly', mbr('disk-raw-ro', [('sssd', out/'fixture.IMD')], [0], discard=True), ['+RAW', '+READONLY'])]
    if boot_only:
        cases = []
    if not skip_boot:
        (out/'dd-boot.IMD').write_bytes(boot_fixture())
        card([out/'dd-boot.IMD'], out/'dd-boot.img')
        cases.append(('dd-boot', out/'dd-boot.img', ['+DDBOOT']))
        cases.append(('dd-boot-raw', mbr('dd-boot-raw', [('terak-dsdd', out/'dd-boot.IMD')], logical=True), ['+DDBOOT']))
        image = image.resolve() if image else out/'system.img'
        if not image.exists():
            build_card(image)
        cases.append(('boot', image, [f'+SCREEN_MEMORY={out}/screen.hex']))
        system = [ROOT/'build/terak-media'/name for name in ('II0TERAK.IMD', 'NASADEM1.IMD')]
        if any(not disk.exists() for disk in system):
            build_card(out/'download-card.img')
        # This UCSD setup accesses unit 1 during startup; keep its complete media set.
        cases.append(('boot-raw', mbr('boot-raw', [('sssd', disk) for disk in system], [0], True),
                      [f'+SCREEN_MEMORY={out}/screen-raw.hex']))
    if cases_filter:
        missing = set(cases_filter)-{name for name, _, _ in cases}
        if missing:
            raise ValueError('Unavailable disk cases: '+', '.join(sorted(missing)))
        cases = [case for case in cases if case[0] in cases_filter]
    built, results = set(), []
    for name, disk, arguments in cases:
        before = hashlib.sha256(disk.read_bytes()).hexdigest()
        kind = 'boot' if name.startswith(('boot', 'dd-boot')) else 'qb' if name.startswith('qb') else 'disk'
        top = 'tb_terak_'+kind
        if kind not in built:
            execute(kind+'-build', ['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD',
                    '-Wno-PINMISSING', '--top-module', top, '-j', '4', '--Mdir', str(out/(kind+'-obj'))]+
                    rtl_parameters+([f'-GMICROCODE="{CPU_OUT.relative_to(ROOT)}/microcode.mem"'] if kind == 'boot' else [])+
                    CORE+BOARD+GENERATED+['tests/terak/'+top+'.v', GUARD,
                                        'tests/models/async_sram_model.v', 'tests/models/spi_sd_model.v'])
            built.add(kind)
        bounds = []
        if 'mbr' in name or 'raw' in name:
            extent = json.loads(disk.with_suffix('.json').read_text())['disks'][0]
            bounds = [f"+DATA_BASE={extent['start']}", f"+DATA_BLOCKS={extent['blocks']}"]
        text = execute(name, [str(out/(kind+'-obj')/('V'+top))]+memory_args(hardware)+
                       [f'+SD_IMAGE={disk}']+arguments+bounds)
        if 'PASS' not in text or '%Fatal' in text or hashlib.sha256(disk.read_bytes()).hexdigest() != before:
            raise AssertionError('Simulation changed source image or missed its acceptance condition')
        print(text[-3000:])
        results.append(dict(name=name, image=str(disk), image_sha256=before,
                            log_sha256=hashlib.sha256((out/(name+'.log')).read_bytes()).hexdigest()))
        if name in ('boot', 'boot-raw'):
            registers = re.search(r'SCREEN GAR=(\d+) VIR=(\d+) VCR=(\d+)', text)
            if not registers:
                raise AssertionError('Missing native display snapshot')
            suffix = '-raw' if name == 'boot-raw' else ''
            render(out/('screen'+suffix+'.hex'), *map(int, registers.groups()), out/('screen'+suffix+'.png'))
    if any(hashlib.sha256((ROOT/p).read_bytes()).hexdigest() != digest for p, digest in hashes.items()):
        raise AssertionError('Source changed during qualification')
    (out/'result.json').write_text(json.dumps(dict(passed=True, boot=any(c['name'].startswith('boot') for c in results),
                                                boot_only=boot_only, selected_cases=cases_filter,
                                                hardware=hardware, files=hashes, vendor_video=vendor_hashes,
                                                cases=results), indent=2)+'\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--card', type=Path)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--skip-boot', action='store_true')
    group.add_argument('--boot-only', action='store_true', help='Only the four cold disk/CPU scenarios; retain unit checks')
    parser.add_argument('--vendor-library', type=Path)
    parser.add_argument('--case', action='append', help='Select named disk scenarios; unit and memory checks always run')
    args = parser.parse_args()
    run(args.out, args.card, args.skip_boot, args.vendor_library, args.boot_only, args.case)
