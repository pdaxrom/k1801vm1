#!/usr/bin/env python3
"""Assemble the recovered AM4 MicROM source into a flat 56-bit ROM image."""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path


WORDS = 1024
WORD_BITS = 56


def read_mif(path):
    values = {}
    record = re.compile(r"\s*([0-9A-Fa-f]+)\s*:\s*([0-9A-Fa-f]+)\s*;")
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        match = record.fullmatch(line)
        if not match:
            continue
        address = int(match.group(1), 16)
        value = int(match.group(2), 16)
        if address in values:
            raise ValueError(f"{path}:{line_number}: duplicate address {address:03X}")
        if address >= WORDS or value >= 1 << WORD_BITS:
            raise ValueError(f"{path}:{line_number}: AM4 MicROM value is out of range")
        values[address] = value
    missing = sorted(set(range(WORDS)) - values.keys())
    if missing:
        raise ValueError(f"{path}: missing {len(missing)} MicROM words")
    return [values[address] for address in range(WORDS)]


def main():
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--listing", type=Path)
    parser.add_argument(
        "--definition",
        type=Path,
        default=script_dir / "tools/am29_m4.def",
    )
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="am4-microm-") as temporary:
        mif = Path(temporary) / "mc.mif"
        command = [
            sys.executable,
            str(script_dir / "tools/meta29.py"),
            str(args.definition),
            str(args.source),
            "-o",
            str(mif),
        ]
        if args.listing:
            command += ["-l", str(args.listing)]
        subprocess.run(command, check=True)
        image = read_mif(mif)

    args.output.write_text("".join(f"{value:014x}\n" for value in image))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(error)
