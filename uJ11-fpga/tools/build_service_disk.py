#!/usr/bin/env python3
"""Create a private RT-11 disk with UJLOAD and optional validated ABI1 files."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
from board_common import ROOT
from service_image import decode


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, default=ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    parser.add_argument('--output', type=Path, default=ROOT/'build/rt11-ujload.dsk')
    parser.add_argument('--module', type=Path, action='append', default=[],
                        help='Optional ABI1 .BIN file, repeat for ODT and FP11')
    args = parser.parse_args()
    base, out = args.base.resolve(), args.output.resolve()
    assert base != out and not out.exists(), 'Choose a fresh output path'
    programs = [ROOT/'demos/rt11/service/UJLOAD.SAV'] + [p.resolve() for p in args.module]
    modules = {}
    for program in programs[1:]:
        assert re.fullmatch('[A-Z0-9]{1,6}\\.BIN', program.name), program.name
        assert program.name not in modules, 'duplicate output filename'
        modules[program.name] = decode(program.read_bytes())
    digests = {p.name: sha(p) for p in programs}
    base_digest = sha(base)
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(base, out)
    tool = ROOT/'../lsi11/rt11tool'
    with tempfile.TemporaryDirectory(prefix='uj11-service-disk-') as temp:
        for program in programs:
            subprocess.run([str(tool), 'add', str(out), str(program), program.name], check=True, capture_output=True)
            subprocess.run([str(tool), 'extract', str(out), temp, program.name], check=True, capture_output=True)
            assert sha(Path(temp)/program.name) == digests[program.name], program.name
    assert sha(base) == base_digest, 'base image changed'
    record = dict(base_sha256=base_digest, image_sha256=sha(out), programs=digests,
                  modules=modules, driver_sha256=sha(Path(__file__)))
    out.with_suffix('.json').write_text(json.dumps(record, indent=2)+'\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
