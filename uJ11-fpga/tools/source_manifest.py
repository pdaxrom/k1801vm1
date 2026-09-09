#!/usr/bin/env python3
"""Hash the complete CP1 source/build inputs, including assembled ROM lanes."""
import argparse
import hashlib
import json
from pathlib import Path

FILES = [
    'rtl/uj11_microseq.v', 'microcode/checkpoint_seq.uasm',
    'microasm/uj11asm.py', 'tools/make_ebr.py',
    'microcode/generated/checkpoint_seq.mem', 'microcode/generated/uj11_rom_ebr.v',
    'synth/machxo2/uj11_probe_seq.v', 'synth/machxo2/uj11-seq.ldf',
    'synth/machxo2/uj11.sty', 'synth/machxo2/uj11.lpf', 'synth/machxo2/build.tcl',
    'Makefile', 'tools/source_manifest.py', 'tools/report_synthesis.py',
]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    files = {name: hashlib.sha256((root/name).read_bytes()).hexdigest() for name in sorted(FILES)}
    revision = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
    encoding=json.loads((root/'microcode/generated/checkpoint_seq.stats.json').read_text())['encoding_version']
    result = {'checkpoint': f'cp1-format-v{encoding}', 'input_revision_sha256': revision, 'files': files}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2)+'\n')
    print('CP1 input revision:', revision)


if __name__ == '__main__':
    main()
