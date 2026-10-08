#!/usr/bin/env python3
"""Exercise Setup records/actions with the real J11, SERV, PS/2 and SPI card.

Compile one testbench, then reuse it for bounded read-only image fixtures.
The expensive ten-second test retains the real 50 MHz clock and SERV timer.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

from board_common import ROOT
from build_terak import build
from serv_test import memory_args
from test_terak_rt11 import run as boot
from terak_mbr import ALIGN, FAT_BLOCKS

sys.path.insert(0, str(ROOT.parent/'bk0010-01-fpga/tools'))
from prepare_disk_sd import Fat16


def record(generation, profile):
    words = [0x4b54, 0x5453, 1, generation, profile, profile ^ 65535, 0]+[0]*249
    words[6] = -sum(words) & 65535
    return struct.pack('<256H', *words)


def run(disks, out, compiled=None, skip_timeout=False, jobs=4):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    hardware = build()
    if hardware['hard_disk'] != 'rl' or len(disks) != 2:
        raise ValueError('Use the 256 KiB BIOS/RL profile')
    if compiled is None:
        boot(disks, out/'fixture', 'rt11xm', menu_only=True)
        compiled = out/'fixture'
    compiled = compiled.resolve()
    manifest = json.loads((compiled/'result.json').read_text())
    if not manifest['passed'] or manifest['hardware'] != hardware:
        raise ValueError('Compiled testbench must match the current hardware manifest')
    for path, digest in manifest['files'].items():
        if hashlib.sha256((ROOT/path).read_bytes()).hexdigest() != digest:
            raise ValueError('Compiled testbench source changed: '+path)
    executable = compiled/'obj/Vtb_terak_boot'
    base = out/'rl-fixture.img'
    from terak_mbr import create
    base.unlink(missing_ok=True)
    card = create([], base, hard_disks=[('rl01', disks[0]), ('rl02', disks[1])], hd_readonly=[1])
    data = bytearray(base.read_bytes())
    volume = Fat16(data[ALIGN*512:(ALIGN+FAT_BLOCKS)*512])
    volume.put('TERAK5.CFG', card['config'].encode())
    from build_terak import OUT
    volume.put('TBIOS.BIN', (OUT/'bios/TBIOS.BIN').read_bytes())
    volume.put('TERAK.SET', bytes(1024))
    data[ALIGN*512:(ALIGN+FAT_BLOCKS)*512] = volume.data
    base.write_bytes(data)
    host_sources = ['tests/terak/test_track_metadata.c', 'tests/terak/test_keyboard.c',
                    'tests/test_terak_bios.py', 'tests/test_terak_setup_sd.py']
    for pattern in ('test_terak_bios.py', 'test_terak_setup_sd.py'):
        subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', pattern],
                       cwd=ROOT, check=True)
    for test, definitions in [('track_metadata', ['-DUJ11_TERAK_RL', '-DUJ11_TERAK_BIOS']),
                              ('keyboard', ['-DUJ11_TERAK', '-DUJ11_TERAK_BIOS', '-DUJ11_KEYBOARD',
                                            '-DUJ11_KEYBOARD_TEST', '-Itests', '-Ifirmware/storage'])]:
        target = out/test
        implementation = 'media.c' if test == 'track_metadata' else '../storage/keyboard.c'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        *definitions, 'tests/terak/test_'+test+'.c', 'firmware/terak/'+implementation,
                        '-o', str(target)], cwd=ROOT, check=True)
        subprocess.run([str(target)], cwd=ROOT, check=True)
    original = base.read_bytes()
    cases = ['actions', 'save', 'saved-wrap', 'fragmented', 'unavailable', 'missing', 'readonly', 'failed',
             'magic', 'version', 'inverse', 'checksum', 'profile', 'fat-copies', 'bad-default']
    if not skip_timeout:
        cases.append('timeout')
    results = {}
    sources = dict(manifest['files'])
    sources.update({p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in host_sources})
    sources['tools/test_terak_setup.py'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    def check_case(case):
        work = out/case
        work.mkdir(exist_ok=True)
        data = bytearray(original)
        volume = Fat16(data[ALIGN*512:(ALIGN+FAT_BLOCKS)*512])
        flags = ['+BIOS_ONLY', '+BIOS_CHOICE=5']
        settings = bytes(1024)
        if case in ('saved-wrap', 'fragmented'):
            settings = record(65535, 0)+record(0, 5)
            flags += ['+BIOS_SAVED']
        elif case in ('magic', 'version', 'inverse', 'checksum', 'profile'):
            newer = bytearray(record(6, 5))
            offset = dict(magic=0, version=4, inverse=10, checksum=200, profile=8)[case]
            newer[offset] ^= 1
            if case in ('version', 'inverse', 'profile'):
                if case == 'profile':
                    struct.pack_into('<HH', newer, 8, 6, 6 ^ 65535)
                struct.pack_into('<H', newer, 12, 0)
                struct.pack_into('<H', newer, 12, -sum(struct.unpack('<256H', newer)) & 65535)
            settings = record(5, 0)+newer
            flags += ['+BIOS_SAVED', '+BIOS_EXPECT_DEFAULT']
        elif case == 'unavailable':
            settings = record(1, 3)+bytes(512)
        elif case in ('actions', 'save'):
            flags += ['+BIOS_ACTIONS']
            if case == 'save':
                flags += ['+BIOS_SAVE_OK']
        elif case == 'timeout':
            flags += ['+BIOS_TIMEOUT']
        if case == 'bad-default':
            volume.put('TERAK.CFG', b'version=999\n')
        volume.put('TERAK.SET', settings)
        offset, entry, _ = volume.files()['TERAK.SET']
        chain = volume.chain(struct.unpack_from('<H', entry, 26)[0])
        if case == 'fragmented':
            assert volume.spc == 1
            assert struct.unpack_from('<H', volume.data, volume.fat+40*2)[0] == 0
            volume.data[volume.first+38*512:volume.first+39*512] = settings[512:]
            for copy in range(2):
                at = volume.fat+copy*volume.fat_size
                struct.pack_into('<H', volume.data, at+chain[0]*2, 40)
                struct.pack_into('<H', volume.data, at+40*2, 0xffff)
                struct.pack_into('<H', volume.data, at+chain[1]*2, 0)
            chain = [chain[0], 40]
        sectors = [ALIGN+(volume.first+(c-2)*volume.cluster_size)//512+b
                   for c in chain for b in range(volume.spc)][:2]
        if case == 'missing':
            volume.data[offset] = 0xe5
        elif case == 'readonly':
            volume.data[offset+11] |= 1
        elif case == 'fat-copies':
            struct.pack_into('<H', volume.data, volume.fat+volume.fat_size+chain[0]*2, 0xffff)
        if case in ('missing', 'readonly', 'failed', 'fat-copies'):
            flags += ['+BIOS_SAVE_ERROR']
        if case == 'failed':
            flags += ['+BIOS_FAIL_SAVE']
        data[ALIGN*512:(ALIGN+FAT_BLOCKS)*512] = volume.data
        image = work/'card.img'
        image.write_bytes(data)
        digest = hashlib.sha256(data).hexdigest()
        command = [str(executable), *memory_args(hardware), f'+SD_IMAGE={image}',
                   f'+SET0={sectors[0]}', f'+SET1={sectors[1]}',
                   f'+BIOS_FONT={compiled}/bios-font.hex', f'+BIOS_SCREEN_MEMORY={work}/screen.hex', *flags]
        with (work/'boot.log').open('w') as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        text = (work/'boot.log').read_text()
        expected = 'PASS BIOS ten-second' if case == 'timeout' else 'PASS BIOS selection into native ROM'
        if result.returncode or expected not in text or hashlib.sha256(image.read_bytes()).hexdigest() != digest:
            raise RuntimeError(case+' failed: '+text[-4000:])
        if case in ('unavailable', 'bad-default') and 'Saved/default configuration unavailable.' not in text:
            raise AssertionError('Unavailable saved profile did not open Setup with an error')
        return dict(passed=True, image_sha256=digest,
                    log_sha256=hashlib.sha256(text.encode()).hexdigest(), command=command)

    if jobs < 1 or jobs > 8:
        raise ValueError('Use 1 through 8 independent simulator processes')
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        pending = {pool.submit(check_case, case): case for case in cases}
        for future in as_completed(pending):
            case = pending[future]
            results[case] = future.result()
            print('PASS Setup '+case, flush=True)
    for path, digest in sources.items():
        if hashlib.sha256((ROOT/path).read_bytes()).hexdigest() != digest:
            raise AssertionError('Source changed during qualification: '+path)
    result = dict(passed=True, hardware=hardware, files=sources, cases=results,
                  executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
    (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disk', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--compiled', type=Path, help='Reuse an unchanged qualified testbench directory')
    parser.add_argument('--skip-timeout', action='store_true')
    parser.add_argument('--jobs', type=int, default=4, help='Independent simulations in parallel (1-8)')
    args = parser.parse_args()
    run(args.disk, args.out, args.compiled, args.skip_timeout, args.jobs)
