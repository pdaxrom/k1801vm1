#!/usr/bin/env python3
"""Emit explicit board source lists; MMU is absent unless --mmu is requested."""
import argparse
import hashlib
import json
from board_common import ROOT, CORE, BOARD, MMU, profile_name, profile_flags


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mmu', action='store_true')
    args = parser.parse_args()
    sources = CORE + BOARD + (MMU if args.mmu else [])
    out = ROOT/'build'/('board-' + profile_name(args.mmu))
    out.mkdir(parents=True, exist_ok=True)
    # Command files use +define+ (Icarus -f does not accept command-line -D).
    defines = ['+define+UJ11_MMU'] if args.mmu else []
    lists = {
        'simulation.f': defines + sources + ['rtl/uj11_rom.v'],
        'synthesis.f': defines + ['+define+SYNTHESIS'] + sources + [
            'boards/hc1200/uj11_microcomp.v', 'microcode/generated/uj11_m0_ebr.v'],
    }
    for name, lines in lists.items():
        (out/name).write_text('\n'.join(lines) + '\n')
    inputs = set(sources + ['boards/hc1200/uj11_microcomp.v', 'rtl/uj11_rom.v',
        'microcode/generated/uj11_m0_ebr.v', 'microcode/generated/m0.mem',
        'microcode/generated/firmware.mem', 'microcode/generated/decode.mem',
        'microcode/generated/m0.stats.json', 'tools/board_common.py',
        'tools/build_board_profile.py'])
    report = dict(profile=profile_name(args.mmu), defines=profile_flags(args.mmu),
        sources=sources, address_bits=22 if args.mmu else 16,
        microcode_words=json.loads((ROOT/'microcode/generated/m0.stats.json').read_text())['used_words'],
        experimental=args.mmu, complete_mmu=False,
        files={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(inputs)})
    (out/'inputs.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f"PASS board profile {report['profile']}: {report['address_bits']}-bit bus, "
          f"{len(MMU) if args.mmu else 0} MMU sources, {report['microcode_words']} microcode words")


if __name__ == '__main__':
    main()
