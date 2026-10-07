#!/usr/bin/env python3
"""Isolated HC7000 Terak profile: J11 EIS/FIS, QX PIO, native display and PS/2."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path
from board_common import ROOT, sources
from build_mmu import CORE, build as build_cpu
from build_iop_mmu import ram
from iop_ebr import sector_ram

OUT = ROOT/'build/hc7000-terak'
CPU_OUT = OUT/'cpu'
IOP_OUT = OUT/'iop'
TOP = str((OUT/'uj11_terak_microcomp.v').relative_to(ROOT))
FIRMWARE = ['firmware/storage/start.S', 'firmware/terak/main.c', 'firmware/terak/qb.c', 'firmware/terak/media.c', 'firmware/terak/mbr.c', 'firmware/storage/label.c', 'firmware/storage/keyboard.c']
HEADERS = ['firmware/terak/terak.h', 'firmware/storage/storage.h', 'firmware/storage/label.h', 'firmware/storage/keyboard.h',
           'firmware/storage/terminal.h', 'firmware/storage/link.ld', 'firmware/terak/boot.mem',
           'firmware/terak/boot.json', 'firmware/terak/qb_boot.asm', 'firmware/terak/qb_boot.mem',
           'firmware/terak/qb_boot.json']
BOARD = ['boards/hc7000/terak/'+p+'.v' for p in
         ('uj11_terak_board', 'uj11_terak_bus', 'uj11_terak_qx', 'uj11_terak_console', 'uj11_terak_video', 'uj11_terak_timing')]
BOARD += ['boards/hc7000/mmu/'+p+'.v' for p in
          ('uj11_mmu_disk', 'uj11_mmu_rk611', 'uj11_mmu_iop_bus', 'uj11_mmu_sector_engine')]
BOARD += ['boards/hc7000/uj11_sram.v', 'boards/hc7000/uj11_ps2_keyboard.v',
          'boards/hc7000/uj11_diagnostics.v', 'boards/hc1200/uj11_button.v', 'boards/hc1200/uj11_tick.v',
          'boards/hc7000/video/uj11_video_arbiter.v',
          'boards/hc7000/video/uj11_pal_ram.v', 'boards/hc7000/video/uj11_console_fifo.v',
          'boards/hc7000/video/uj11_serv_sram.v', 'rtl/peripherals/spi_byte_service.v']
BOARD += [p for p in sources('hc7000-lcd-sram')[1] if p.startswith('vendor/serv/')]
GENERATED = [str(p.relative_to(ROOT)) for p in
             (CPU_OUT/'uj11_mmu_rom.v', IOP_OUT/'uj11_mmu_iop_ram.v',
              IOP_OUT/'uj11_sector_ram.v', OUT/'uj11_terak_boot_rom.v')]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def firmware(rebuild=False):
    IOP_OUT.mkdir(parents=True, exist_ok=True)
    inputs = {p: sha(ROOT/p) for p in FIRMWARE+HEADERS+['tools/build_terak.py', 'tools/build_iop_mmu.py', 'tools/iop_ebr.py']}
    base_flags = ['-march=rv32ic', '-mabi=ilp32', '-Os', '-ffreestanding', '-fno-builtin',
                  '-ffunction-sections', '-fdata-sections', '-msmall-data-limit=0',
                  '-Wall', '-Wextra', '-Werror', '-DUJ11_TERAK', '-DUJ11_KEYBOARD']
    record_path = IOP_OUT/'build.json'
    if record_path.exists() and not rebuild and not os.environ.get('RISCV_CC'):
        record = json.loads(record_path.read_text())
        saved_flags = [f for f in record['flags'] if not f.startswith('--target=')]
        if record['inputs'] == inputs and saved_flags == base_flags and all(
                (IOP_OUT/p).exists() and sha(IOP_OUT/p) == digest for p, digest in record['outputs'].items()):
            return record
    cc = os.environ.get('RISCV_CC') or shutil.which('riscv64-unknown-elf-gcc') or shutil.which('riscv-none-elf-gcc')
    clang = False
    if not cc:
        cc = shutil.which('clang')
        # Homebrew LLVM includes the RISC-V target; Apple clang may not.
        if Path('/opt/homebrew/opt/llvm/bin/clang').exists():
            cc = '/opt/homebrew/opt/llvm/bin/clang'
        clang = True
    else:
        clang = 'clang' in Path(cc).name
    if not cc:
        raise RuntimeError('Terak firmware needs RISC-V GCC or LLVM clang/lld')
    flags = base_flags[:]
    if clang:
        flags = ['--target=riscv32-unknown-elf']+flags
    record_path = IOP_OUT/'build.json'
    version = subprocess.check_output([cc, '--version'], text=True).splitlines()[0]
    if record_path.exists() and not rebuild:
        record = json.loads(record_path.read_text())
        if record['inputs'] == inputs and record['flags'] == flags and record['compiler'] == version and all(
                (IOP_OUT/p).exists() and sha(IOP_OUT/p) == digest for p, digest in record['outputs'].items()):
            return record
    objects = []
    for source in FIRMWARE:
        obj = IOP_OUT/(Path(source).stem+'.o')
        subprocess.run([cc]+flags+['-c', str(ROOT/source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    elf = IOP_OUT/'firmware.elf'
    ldflags = ['-fuse-ld=lld'] if clang else ['-lgcc']
    subprocess.run([cc]+flags+['-nostdlib', '-nostartfiles', '-Wl,--gc-sections', '-Wl,--relax',
                   '-T', str(ROOT/'firmware/storage/link.ld'), '-Wl,-Map='+str(IOP_OUT/'firmware.map')]+
                   objects+ldflags+['-o', str(elf)], check=True)
    prefix = os.environ.get('RISCV_PREFIX', str(cc).removesuffix('gcc'))
    def binary_tool(name):
        if not clang:
            return prefix+name
        path = Path(cc).with_name('llvm-'+name)
        return str(path) if path.exists() else shutil.which('llvm-'+name)
    subprocess.run([binary_tool('objcopy'), '-O', 'binary', str(elf), str(IOP_OUT/'firmware.bin')], check=True)
    data = (IOP_OUT/'firmware.bin').read_bytes()
    symbols = {line.split()[2]: int(line.split()[0], 16) for line in
               subprocess.check_output([binary_tool('nm'), '--defined-only', str(elf)], text=True).splitlines()
               if len(line.split()) == 3}
    memory = {name: symbols['__'+name] for name in
              ('bss_start', 'bss_end', 'stack_bottom', 'stack_top', 'cold_start', 'cold_end',
               'cold_limit', 'sector_start', 'sector_end')}
    if len(data) > memory['stack_bottom'] or memory['bss_end'] > memory['stack_bottom']:
        raise ValueError('Terak firmware overlaps its reserved stack')
    words = [int.from_bytes(data.ljust(19456, b'\0')[i:i+4], 'little') for i in range(0, 19456, 4)]
    (IOP_OUT/'firmware.mem').write_text(''.join(f'{w:08x}\n' for w in words))
    (IOP_OUT/'uj11_mmu_iop_ram.v').write_text(ram(words).replace(
        'build/hc7000-mmu-iop/firmware.mem', str((IOP_OUT/'firmware.mem').relative_to(ROOT))))
    (IOP_OUT/'uj11_sector_ram.v').write_text(sector_ram())
    (IOP_OUT/'firmware.lst').write_bytes(subprocess.check_output([binary_tool('objdump'), '-d', str(elf)]))
    record = dict(inputs=inputs, flags=flags, compiler=version, program_bytes=len(data), ram_bytes=19456,
                  ebr=19, profile='terak', memory=memory,
                  outputs={p.name: sha(p) for p in IOP_OUT.iterdir() if p.suffix in ('.elf', '.bin', '.mem', '.map', '.lst', '.v')})
    record_path.write_text(json.dumps(record, indent=2)+'\n')
    return record


def build(rebuild=False):
    os.environ['UJ11_MMU_FPP'] = 'off'
    os.environ['UJ11_MMU_CLOCK_MHZ'] = '50'
    os.environ['UJ11_MMU_IOP'] = 'storage'
    OUT.mkdir(parents=True, exist_ok=True)
    cpu = build_cpu('off', CPU_OUT)
    iop = firmware(rebuild)
    words = [int(line, 16) for line in (ROOT/'firmware/terak/boot.mem').read_text().splitlines()]
    if len(words) != 64:
        raise ValueError('Terak bootstrap must be 64 words')
    qb_words = [int(line, 16) for line in (ROOT/'firmware/terak/qb_boot.mem').read_text().splitlines()]
    if len(qb_words) != 256:
        raise ValueError('QB bootstrap must be 256 words')
    qb_record = json.loads((ROOT/'firmware/terak/qb_boot.json').read_text())
    qb_bytes = b''.join(word.to_bytes(2, 'little') for word in qb_words)
    if qb_record['asm_sha256'] != sha(ROOT/'firmware/terak/qb_boot.asm') or \
            qb_record['image_sha256'] != hashlib.sha256(qb_bytes).hexdigest():
        raise ValueError('QB bootstrap source/image mismatch; run tools/build_terak_boot.py')
    (OUT/'uj11_terak_boot_rom.v').write_text(
        '// QX reconstruction / uJ11 QB bootstrap: see docs/terak.md.\n'
        'module uj11_terak_boot_rom(input wire [7:0] address,input wire qb,\n'
        'output reg [15:0] data /* synthesis syn_romstyle="logic" */);\n'
        'always @*case({qb,address})\n'+''.join(f"9'd{i}:data=16'h{word:04x};\n" for i, word in enumerate(words))+
        ''.join(f"9'd{i+256}:data=16'h{word:04x};\n" for i, word in enumerate(qb_words))+
        "default:data=0;endcase\nendmodule\n")
    top = (ROOT/'boards/hc7000/mmu/uj11_mmu_microcomp.v').read_text()
    top = top.replace('uj11_mmu_microcomp', 'uj11_terak_microcomp').replace('uj11_mmu_board #', 'uj11_terak_board #')
    top = top.replace('CLOCK_HZ=24000000', 'CLOCK_HZ=50000000').replace('VIDEO_ENABLE=0', 'VIDEO_ENABLE=1')
    top = top.replace('KEYBOARD_ENABLE=0', 'KEYBOARD_ENABLE=1').replace('.tvout(tvout),', '.tvout(tvout),.audio(audio),')
    diagnostics = int(os.environ.get('UJ11_HC7000_DIAGNOSTICS', '0'))
    if diagnostics not in (0, 1):
        raise ValueError('UJ11_HC7000_DIAGNOSTICS must be 0 or 1')
    top = top.replace('DIAGNOSTICS_ENABLE=0', f'DIAGNOSTICS_ENABLE={diagnostics}')
    top = top.replace('    assign audio=2\'b0;\n', '')
    (ROOT/TOP).write_text(top)
    inventory = CORE+BOARD+GENERATED+[TOP]
    inputs = set(inventory+FIRMWARE+HEADERS+['tools/build_terak.py', 'tools/terak_image.py', 'tools/terak_mbr.py',
                                          'boards/hc7000/mmu/uj11_mmu_microcomp.v'])
    record = dict(machine='terak', board='hc7000-lcd-sram', cpu=cpu, iop=iop, fpp='off', clock_mhz=50,
                  boot_pc_octal='173000', guest_ram_bytes=57344, expected_ebr=26, diagnostics=diagnostics,
                  video='native Terak compositor; monochrome PAL-60 525/60 presentation', bevent_hz=60,
                  video_field_hz=60,video_lines=525,bevent_source='video field start, toggle CDC',
                  inventory=inventory, top=TOP, files={p: sha(ROOT/p) for p in sorted(inputs)},
                  scope='simulation/build inputs; FPGA qualification requires MAP/PAR/TRACE')
    (OUT/'board-inputs.json').write_text(json.dumps(record, indent=2)+'\n')
    print(f"Terak: SERV {iop['program_bytes']} bytes, BSS through {iop['memory']['bss_end']}, 56 KiB guest RAM")
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rebuild', action='store_true')
    build(parser.parse_args().rebuild)
