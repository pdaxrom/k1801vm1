#!/usr/bin/env python3
"""Assemble/verify the HALT helper and its embedded MACRO-11 data image."""
import argparse
import hashlib
import json
import struct
import subprocess
from board_common import ROOT


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def build(assemble=False):
    fw = ROOT/'firmware/cp62'
    source = ROOT/'demos/rt11/service/cp62/UJLOAD.MAC'
    assembler = ROOT/'../microasm11/microasm11'
    if assemble:
        with (fw/'loader.log').open('w') as log:
            subprocess.run([str(assembler), '-binary', '--cpu', 'dcj-11', '--list',
                            str(fw/'loader.lst'), str(fw/'loader.asm'), str(fw/'loader.bin')],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    blob = (fw/'loader.bin').read_bytes()
    assert len(blob) == 0o710 and 'Errors: No error' in (fw/'loader.lst').read_text()
    words = struct.unpack('<'+'H'*(len(blob)//2), blob)
    text = source.read_text()
    a = text.index('HELPER:\n')+len('HELPER:\n'); b = text.index('HCNT=', a)
    embedded = ''.join('\t.WORD\t'+','.join(f'{w:o}' for w in words[i:i+8])+'\n'
                       for i in range(0, len(words), 8))
    names = ['firmware/cp62/loader'+ext for ext in ('.asm','.bin','.lst','.log')]
    if assemble:
        source.write_text(text[:a]+embedded+text[b:])
        (fw/'loader-inputs.json').write_text(json.dumps(dict(assembler_sha256=sha(assembler),
            files={p:sha(ROOT/p) for p in names}), indent=2)+'\n')
    else:
        manifest = json.loads((fw/'loader-inputs.json').read_text())
        for p, digest in manifest['files'].items(): assert sha(ROOT/p)==digest, p
        assert text[a:b] == embedded, 'UJLOAD embeds a different HALT helper'
    return dict(helper_bytes=len(blob), sources={p:sha(ROOT/p) for p in names})


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assemble',action='store_true')
    print(json.dumps(build(parser.parse_args().assemble),indent=2))
