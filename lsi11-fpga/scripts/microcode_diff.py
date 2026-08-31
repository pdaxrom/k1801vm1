#!/usr/bin/env python3
"""List and validate patched AM4 MicROM words against the upstream image."""

import argparse
from pathlib import Path


EXPECTED_PATCHES = {
    0x001, 0x002, 0x0B2, 0x0B4, 0x290, 0x29F, 0x308, 0x309,
    0x30D, 0x30E, 0x30F, 0x319, 0x31A, 0x31B, 0x31C, 0x31D,
    0x31E, 0x31F,
}


def read_rom(path: Path):
    words = [int(line.split("#", 1)[0].strip(), 16)
             for line in path.read_text().splitlines()
             if line.split("#", 1)[0].strip()]
    if len(words) != 1024:
        raise ValueError(f"{path}: expected 1024 words, found {len(words)}")
    return words


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("upstream", type=Path)
    parser.add_argument("patched", type=Path)
    args = parser.parse_args()
    upstream = read_rom(args.upstream)
    patched = read_rom(args.patched)
    changed = {index for index in range(1024)
               if upstream[index] != patched[index]}
    for index in sorted(changed):
        print(f"{index:03X}: {upstream[index]:014X} -> {patched[index]:014X}")
    if changed != EXPECTED_PATCHES:
        missing = EXPECTED_PATCHES - changed
        extra = changed - EXPECTED_PATCHES
        raise SystemExit(
            f"unexpected MicROM patch set; missing={sorted(missing)}, extra={sorted(extra)}"
        )
    print(f"PASS: patched MicROM differs at exactly {len(changed)} documented words")


if __name__ == "__main__":
    main()
