#!/usr/bin/env python3
"""Isolate the RT-11 RK611 recovery fix on the exact frozen CP59d board."""
import hashlib
import json
import argparse
import subprocess
import tarfile
from pathlib import Path
from board_common import ROOT, CORE, BOARD
from build_firmware import generate
from build_fram_cp52 import replace_once as rep

OUT = ROOT/'build/cp60-rk'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build():
    reference = ROOT/'synth/reports/cp59d'
    manifest = json.loads((reference/'inputs.json').read_text())
    frozen = {}
    with tarfile.open(reference/'source.tgz') as archive:
        for name, digest in manifest['files'].items():
            if name.startswith('generated:'):
                continue
            data = archive.extractfile(name).read()
            assert hashlib.sha256(data).hexdigest() == digest, name
            frozen[name] = data
    firmware = ROOT/'firmware/cp60'
    assembled = json.loads((firmware/'inputs.json').read_text())
    for name, digest in assembled['files'].items():
        assert sha(ROOT/name) == digest, name
    data = (firmware/'rk_service.bin').read_bytes()
    assert len(data) <= 512 and len(data) % 2 == 0
    # Preserve every bootstrap/CSR word; replace only the private RK program.
    words = [int(x, 16) for x in (ROOT/'microcode/generated/firmware.mem').read_text().split()]
    # Verify the old portable firmware against the measured synthesized ROM.
    assert generate(words).encode() == frozen['build/cp59-service/src/microcode/generated/uj11_firmware_rom.v']
    words[256:] = [0]*256
    for offset in range(0, len(data), 2):
        words[256+offset//2] = int.from_bytes(data[offset:offset+2], 'little')
    assert words[256+0o476//2] == 2, 'fixed private RTI moved'
    OUT.mkdir(parents=True, exist_ok=True)
    outputs = []
    names = CORE+BOARD+['boards/hc1200/uj11_microcomp.v']
    for name in names:
        text = frozen['build/cp59-service/src/'+name].decode()
        text = text.replace('build/cp59-service/', 'build/cp60-rk/')
        if name == 'boards/hc1200/uj11_board_bus.v':
            text = rep(text, "if (wdata[5:2] == 4'o4)",
                       "if (wdata[5:2] == 4'o4 || wdata[5:1] == 5'o5)")
            text = rep(text, '\t\t\t\t\t\trk_service_pending <= 1;',
                       '// READ/WRITE and RECALIBRATE complete through firmware.\n\t\t\t\t\t\trk_service_pending <= 1;')
            text = rep(text, '''if (boot_rom_phase == 2 && rk_cs2_selected && write && byte_select[0] &&
				wdata[5]) begin''', '''// Guest CS1 bit 15 is CCLR, not a request for another IRQ.
			// The private service uses the same EBR word to publish CERR.
			if (boot_rom_phase == 2 && write &&
				((rk_cs2_selected && byte_select[0] && wdata[5]) ||
				 (rk_cs1_selected && byte_select[1] && wdata[15] && !rk_service_active))) begin''')
        if name == 'microcode/generated/uj11_firmware_rom.v':
            text = generate(words).replace('microcode/generated/firmware.mem', 'build/cp60-rk/firmware.mem')
        path = OUT/'src'/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        outputs.append(str(path.relative_to(ROOT)))
    for name in ('service.uasm','m0.mem','m0.lst','m0.labels.json','m0.stats.json','uj11_m0_ebr.v','decode.mem'):
        path = OUT/name
        path.write_bytes(frozen['build/cp59-service/'+name])
        outputs.append(str(path.relative_to(ROOT)))
    path = OUT/'firmware.mem'
    path.write_text(''.join(f'{word:04x}\n' for word in words))
    outputs.append(str(path.relative_to(ROOT)))
    inputs = ['tools/build_rk_recovery_cp60.py','tools/build_firmware.py','tools/build_fram_cp52.py',
              'tools/board_common.py','tools/make_ebr.py','microcode/generated/firmware.mem',
              'synth/reports/cp59d/inputs.json','synth/reports/cp59d/source.tgz',
              'firmware/cp60/inputs.json']+list(assembled['files'])
    record = dict(reference='cp59d', mmu=False, used_words=1002, cs_ioff_requested=True,
                  inputs={p: sha(ROOT/p) for p in inputs}, outputs={p: sha(ROOT/p) for p in outputs})
    (OUT/'inputs.json').write_text(json.dumps(record, indent=2)+'\n')
    return record


def adapt():
    build()
    return ([str((OUT/'src'/p).relative_to(ROOT)) for p in CORE],
            [str((OUT/'src'/p).relative_to(ROOT)) for p in BOARD])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assemble', action='store_true', help='Rebuild private firmware using the existing assembler executable')
    parser.add_argument('--assembler', type=Path, default=ROOT/'../microasm11/microasm11')
    args = parser.parse_args()
    if args.assemble:
        folder = ROOT/'firmware/cp60'
        source = folder/'rk_service.asm'
        with (folder/'rk_service.log').open('w') as log:
            subprocess.run([str(args.assembler), '-binary', '--cpu', 'dcj-11', '--list',
                            str(folder/'rk_service.lst'), str(source), str(folder/'rk_service.bin')],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        record = dict(assembler_sha256=sha(args.assembler),
                      files={str(p.relative_to(ROOT)): sha(p) for p in
                             (source, folder/'rk_service.bin', folder/'rk_service.lst', folder/'rk_service.log')})
        (folder/'inputs.json').write_text(json.dumps(record, indent=2)+'\n')
    print(json.dumps(build(), indent=2))
