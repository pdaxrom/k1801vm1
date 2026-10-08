#!/usr/bin/env python3
"""Cold boot an original TERAK RT-11 disk, then enter DIR through PS/2."""
import argparse
import hashlib
import json
import subprocess
import shutil
from pathlib import Path

from board_common import ROOT
from build_terak import build, CORE, BOARD, GENERATED, CPU_OUT, OUT
from serv_test import GUARD, memory_args
from terak_mbr import create, ALIGN, FAT_BLOCKS
from render_terak_screen import render


def run(disks, out, system='rt11', selection_only=False, mechanical_speedup=1,
        profile='fd', discard_status=False, menu_only=False, build_fb=False, setup_case=None, uart_echo=None):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    hardware = build()
    echo_sha = None
    if uart_echo:
        if system != 'rt11xm' or len(disks) != 2:
            raise ValueError('UART echo requires the XM RL system and kit disks')
        uart_echo = uart_echo.resolve()
        echo_sha = hashlib.sha256(uart_echo.read_bytes()).hexdigest()
        # Install the diagnostic only in a private clone of the source disk.
        target = out/'uart-system.rl01'
        shutil.copyfile(disks[0], target)
        subprocess.run([str(ROOT.parent/'lsi11/rt11tool'), 'add', str(target),
                        str(uart_echo), 'SERECO.SAV'], check=True, capture_output=True)
        disks = [target, disks[1]]
    inputs = set(hardware['files']) | {'tools/test_terak_rt11.py', 'tools/serv_test.py',
             'tests/terak/tb_terak_boot.v', GUARD, 'tests/models/async_sram_model.v',
             'tests/models/spi_sd_model.v', 'tools/terak_mbr.py', 'tools/terak_image.py',
             'tools/render_terak_screen.py'}
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}
    (out/'result.json').unlink(missing_ok=True)
    image = out/'card.img'
    image.unlink(missing_ok=True)
    if build_fb and len(disks) != 2:
        raise ValueError('FB preparation requires a source and a writable clone')
    if system == 'rt11xm':
        if hardware['hard_disk'] != 'rl' or len(disks) != 2:
            raise ValueError('XM qualification requires the RL build, an RL01 system and an RL02 source disk')
        card = create([], image, hard_disks=[('rl01', disks[0]), ('rl02', disks[1])], hd_readonly=[1])
    else:
        card = create([(profile, p) for p in disks], image,
                      [0] if build_fb else list(range(len(disks))), discard_status=discard_status)
    bios_args = []
    if hardware['boot_mode'] == 'bios':
        # Reuse the shared-card FAT16 writer also used by the BK preparation.
        import sys
        sys.path.insert(0, str(ROOT.parent/'bk0010-01-fpga/tools'))
        from prepare_disk_sd import Fat16
        data = bytearray(image.read_bytes())
        volume = Fat16(data[ALIGN*512:(ALIGN+FAT_BLOCKS)*512])
        choice = 1 if system == 'ucsd' else 3 if system == 'rt11fb' else 5 if system == 'rt11xm' else 2
        volume.put(f'TERAK{choice}.CFG', card['config'].encode())
        volume.put('TERAK.CFG', card['config'].encode())
        if setup_case != 'missing':
            settings = bytes(1024)
            if setup_case == 'saved':
                import struct
                def record(sequence, profile):
                    words = [0x4b54, 0x5453, 1, sequence, profile, profile ^ 65535, 0]+[0]*249
                    words[6] = -sum(words) & 65535
                    return struct.pack('<256H', *words)
                settings = record(65535, 0)+record(0, choice)
            volume.put('TERAK.SET', settings)
            if setup_case == 'readonly':
                volume.data[volume.files()['TERAK.SET'][0]+11] |= 1
        volume.put('TBIOS.BIN', (OUT/'bios/TBIOS.BIN').read_bytes())
        data[ALIGN*512:(ALIGN+FAT_BLOCKS)*512] = volume.data
        image.write_bytes(data)
        card['image_sha256'] = hashlib.sha256(data).hexdigest()
        import re
        glyphs = re.findall(r'\{\s*((?:0x[0-9a-f]{2}[,\s]*){8})\}',
                            (ROOT/'firmware/storage/terminal_font.h').read_text())
        expected_font = bytes(v for g in glyphs for v in
                              [0]+[int(b, 16) for b in re.findall(r'0x[0-9a-f]{2}', g)]+[0])
        assert len(expected_font) == 960
        (out/'bios-font.hex').write_text(''.join(f'{b:02x}\n' for b in expected_font))
        bios_args = ([] if choice == 3 else ['+BIOS_BAD_CHOICE']) + [f'+BIOS_CHOICE={choice}', f'+BIOS_FONT={out}/bios-font.hex',
                     f'+BIOS_SCREEN_MEMORY={out}/bios-screen.hex']
        if 'TERAK.SET' in volume.files():
            import struct
            chain = volume.chain(struct.unpack_from('<H', volume.files()['TERAK.SET'][1], 26)[0])
            sectors = [ALIGN+(volume.first+(c-2)*volume.cluster_size)//512+b
                       for c in chain for b in range(volume.spc)][:2]
            bios_args += [f'+SET0={sectors[0]}', f'+SET1={sectors[1]}']
        bios_args += {'save': ['+BIOS_SAVE_OK'], 'saved': ['+BIOS_SAVED'], 'timeout': ['+BIOS_TIMEOUT'],
                      'missing': ['+BIOS_SAVE_ERROR'], 'readonly': ['+BIOS_SAVE_ERROR'],
                      'failed': ['+BIOS_SAVE_ERROR', '+BIOS_FAIL_SAVE']}.get(setup_case, [])
    elif selection_only or menu_only:
        raise ValueError('BIOS menu/selection checks require UJ11_TERAK_BOOT=bios')
    boot_args = ([] if system == 'ucsd' else ['+RT11'])
    if system == 'rt11fb':
        boot_args.append('+RT11_FB')
    if system == 'rt11xm':
        boot_args.append('+RT11_XM')
        if uart_echo:
            boot_args.append('+UART_ECHO')
        target = card['disks'][0]
        boot_args += [f'+DATA_BASE={target["start"]}', f'+DATA_BLOCKS={target["blocks"]}']
    if build_fb:
        target = card['disks'][1]
        boot_args += ['+RT11_BUILD_FB', f'+SD_OVERLAY={out}/overlay.hex',
                      f'+DATA_BASE={target["start"]}', f'+DATA_BLOCKS={target["blocks"]}']
    if selection_only:
        boot_args.append('+BIOS_ONLY')
    if menu_only:
        boot_args.append('+BIOS_MENU_ONLY')
    commands = [
        ('build', ['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD',
                   '-Wno-PINMISSING', '--top-module', 'tb_terak_boot', '-j', '4', '--Mdir', str(out/'obj'),
                   f"-GRAM_BYTES={hardware['guest_ram_bytes']}",
                   f"-GDISPLAY_EBR={int(hardware['display_storage'] == 'ebr')}",
                   f"-GBOOT_BIOS={int(hardware['boot_mode'] == 'bios')}",
                   f"-GRL_ENABLE={int(hardware['hard_disk'] == 'rl')}",
                   f"-GUART_BAUD={hardware['uart']['baud']}",
                   f'-GMECHANICAL_SPEEDUP={mechanical_speedup}',
                   f'-GMICROCODE="{CPU_OUT.relative_to(ROOT)}/microcode.mem"']+
                   CORE+BOARD+GENERATED+['tests/terak/tb_terak_boot.v', GUARD,
                                        'tests/models/async_sram_model.v', 'tests/models/spi_sd_model.v']),
        ('boot', [str(out/'obj/Vtb_terak_boot')]+memory_args(hardware)+
                 [f'+SD_IMAGE={image}', f'+SCREEN_MEMORY={out}/screen.hex']+boot_args+bios_args)]
    before = hashlib.sha256(image.read_bytes()).hexdigest()
    for name, command in commands:
        with (out/(name+'.log')).open('w') as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        text = (out/(name+'.log')).read_text()
        if result.returncode:
            raise RuntimeError(text[-8000:])
    expected = ('PASS BIOS ten-second automatic default boot' if setup_case == 'timeout' else
                'PASS native RT-11 FB disk preparation' if build_fb else
                'PASS primary BIOS font and menu' if menu_only else 'PASS BIOS selection' if selection_only
                else 'PASS Terak cold ROM/SD UCSD' if system == 'ucsd'
                else 'PASS native TERAK RT-11')
    if expected not in text or hashlib.sha256(image.read_bytes()).hexdigest() != before:
        raise AssertionError('Boot did not pass or changed the original card file')
    if any(hashlib.sha256((ROOT/p).read_bytes()).hexdigest() != digest for p, digest in hashes.items()):
        raise AssertionError('Source changed during qualification')
    if bios_args and setup_case != 'timeout':
        import re
        registers = re.search(r'BIOS_SCREEN GAR=(\d+) VIR=(\d+) VCR=(\d+)', text)
        if not registers:
            raise AssertionError('Missing BIOS display snapshot')
        render(out/'bios-screen.hex', *map(int, registers.groups()), out/'bios-screen.png')
    if not selection_only and not menu_only and not build_fb and setup_case != 'timeout':
        import re
        registers = re.search(r'SCREEN GAR=(\d+) VIR=(\d+) VCR=(\d+)', text)
        if not registers:
            raise AssertionError('Missing native display snapshot')
        render(out/'screen.hex', *map(int, registers.groups()), out/'screen.png')
    record = dict(passed=True, system=system, selection_only=selection_only, menu_only=menu_only,
                  mechanical_speedup=mechanical_speedup, hardware=hardware,
                  media_profile=profile, discard_status=discard_status, build_fb=build_fb,
                  files=hashes, image_sha256=before, card=card, uart_echo_sha256=echo_sha,
                  log_sha256=hashlib.sha256((out/'boot.log').read_bytes()).hexdigest())
    (out/'result.json').write_text(json.dumps(record, indent=2)+'\n')
    print(text[-5000:])
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disk', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--system', choices=('rt11', 'rt11fb', 'rt11xm', 'ucsd'), default='rt11')
    parser.add_argument('--build-fb', action='store_true', help='Run native PIP to prepare a separate FB clone')
    parser.add_argument('--selection-only', action='store_true', help='Stop after verified native ROM handoff')
    parser.add_argument('--menu-only', action='store_true', help='Verify and render all 960 native BIOS font bytes')
    parser.add_argument('--mechanical-speedup', type=int, choices=(1, 100), default=1,
                        help='Accelerate QX mechanical waits only; CPU/SPI/video remain at native rates')
    parser.add_argument('--profile', choices=('fd', 'sssd'), default='fd')
    parser.add_argument('--discard-status', action='store_true', help='Explicitly permit IMD status loss in raw media')
    parser.add_argument('--setup-case', choices=('save', 'saved', 'timeout', 'missing', 'readonly', 'failed'))
    parser.add_argument('--uart-echo', type=Path, help='Install SERECO.SAV in a private XM disk clone and test the serial pins')
    args = parser.parse_args()
    run(args.disk, args.out, args.system, args.selection_only, args.mechanical_speedup, args.profile,
        args.discard_status, args.menu_only, args.build_fb, args.setup_case, args.uart_echo)
