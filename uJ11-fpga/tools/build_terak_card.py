#!/usr/bin/env python3
"""Fetch Terak system/demo media into build cache and create a four-drive card."""
import argparse
import urllib.request
import tempfile
import os
from pathlib import Path
from board_common import ROOT
from terak_image import card, decode_imd

BASE = 'https://www.tiffe.de/robotron/PDP-VAX/PDP11/Terak-disks/'


def build(output, disks=None, mbr=False):
    if not disks:
        disks = []
        for name in ('II0TERAK.IMD', 'NASADEM1.IMD'):
            path = ROOT/'build/terak-media'/name
            if not path.exists():
                data = urllib.request.urlopen(BASE+name, timeout=60).read()
                decode_imd(data)  # Validate before publishing cache bytes.
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            disks.append(path)
    if mbr:
        from terak_mbr import create
        output = Path(output)
        if output.exists() and not output.is_file():
            raise ValueError('SD output must be a regular image file')
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=output.parent, prefix='terak-card-') as directory:
            temporary = Path(directory)/'card.img'
            result = create([('sssd', path) for path in disks], temporary)
            os.replace(temporary, output)
            os.replace(temporary.with_suffix('.json'), output.with_suffix('.json'))
        return result
    return card(disks, output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disk', type=Path, action='append')
    parser.add_argument('--out', type=Path, default=ROOT/'build/hc7000-terak/card.img')
    parser.add_argument('--mbr', action='store_true', help='MBR/FAT16 card with direct SSSD raw partitions')
    args = parser.parse_args()
    build(args.out, args.disk, args.mbr)
    print(args.out)
