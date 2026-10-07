#!/usr/bin/env python3
"""Qualify the optional primary BIOS with real UCSD media and reset buttons."""
import argparse
import subprocess
import sys
from pathlib import Path
from board_common import ROOT
from build_terak_card import build as build_card
from test_terak_rt11 import run as boot


def run(out):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    for pattern in ('test_terak_bios.py', 'test_terak_profiles.py'):
        subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', pattern, '-v'],
                       cwd=ROOT, check=True)
    subprocess.run(['iverilog', '-g2012', '-s', 'tb_terak_button', '-o', str(out/'button'),
                    'tests/terak/tb_terak_button.v', 'boards/hc7000/terak/uj11_terak_button.v'],
                   cwd=ROOT, check=True)
    subprocess.run(['vvp', str(out/'button')], cwd=ROOT, check=True)
    disks = [ROOT/'build/terak-media'/name for name in ('II0TERAK.IMD', 'NASADEM1.IMD')]
    if any(not p.exists() for p in disks):
        build_card(out/'download-card.img')
    boot(disks, out/'ucsd', 'ucsd', profile='sssd')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    run(parser.parse_args().out)
