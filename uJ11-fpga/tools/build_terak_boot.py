#!/usr/bin/env python3
"""Assemble the original uJ11 QB boot source and its independent guest fixture."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from board_common import ROOT


def build():
    assembler = os.environ.get('MICROASM11', str(ROOT.parent/'microasm11/microasm11'))
    for name in ('firmware/terak/qb_boot', 'tests/terak/qb_boot_fixture'):
        source = ROOT/(name+'.asm')
        with tempfile.TemporaryDirectory(prefix='uj11-qb-rom-') as directory:
            binary = Path(directory)/'boot.bin'
            subprocess.run([assembler, '-binary', str(source), str(binary)], check=True)
            data = binary.read_bytes()
        if len(data) % 2 or (name.endswith('/qb_boot') and len(data) != 512):
            raise ValueError('Invalid QB ROM size')
        (ROOT/(name+'.mem')).write_text(''.join(
            f'{int.from_bytes(data[i:i+2], "little"):04x}\n' for i in range(0, len(data), 2)))
        if name.endswith('/qb_boot'):
            record = dict(source='uJ11 QB bootstrap, original implementation of TERAK E5 protocol',
                          reference='https://www.threedee.com/jcm/terak/docs/50-0010-001%20Terak%20Variable%20Density%20Disk%20Drive%20Controller%20Ref%20Guide.pdf',
                          assembler='microasm11 -binary firmware/terak/qb_boot.asm qb_boot.bin',
                          asm_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                          image_sha256=hashlib.sha256(data).hexdigest(), words=256, vector_octal='234',
                          note='Not an original TERAK ROM dump; guest ROH and cold mixed-density boot are tested separately.')
            (ROOT/(name+'.json')).write_text(json.dumps(record, indent=2)+'\n')


if __name__ == '__main__':
    build()
