#!/usr/bin/env python3
"""Build the DCJ11 FPP software HALT module with native DEC MACRO/LINK."""
import hashlib
import json
from pathlib import Path
from board_common import ROOT
from build_software import native
from module_image import pack, decode
from module_relocation import build_module

OUT = ROOT / 'build/fpp'


def build():
    out = OUT / 'software'
    out.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'firmware/fpp/FP11.MAC'
    src = out / source.name
    src.write_bytes(source.read_bytes())
    blob, symbols, assembly, directory = native(src)
    assert symbols['INIT'] == 0o60000
    assert symbols['MEMEND'] <= 0o160000
    assert symbols['ACS'] >= symbols['IMMEND']
    assert symbols['REGS'] - symbols['ACS'] == 48
    image = blob[symbols['INIT']:symbols['IMMEND']]
    module, relocation = build_module(src, symbols['INIT'], len(image), symbols['MEMEND']-symbols['INIT'], image)
    (out / 'FP11.BIN').write_bytes(module)
    (out / 'image.bin').write_bytes(image)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    record = dict(checkpoint='CP80 DCJ11 FPP software', format=decode(module),
                  relocation_assembly=str(relocation.relative_to(ROOT)),
                  relocation_assembler=json.loads((relocation/'build-inputs.json').read_text()),
                  symbols=symbols, allocation_bytes=symbols['MEMEND']-symbols['INIT'],
                  free_halt_bytes=0o160000-symbols['MEMEND'],
                  assembler=assembly, assembly=str(directory.relative_to(ROOT)),
                  sources={str(p.relative_to(ROOT)): sha(p) for p in (
                      source, Path(__file__), ROOT/'tools/build_software.py',
                      ROOT/'tools/module_image.py', ROOT/'tools/module_relocation.py', ROOT/'tools/rt11_build.py')},
                  outputs={str(p.relative_to(ROOT)): sha(p) for p in (
                      src, out/'FP11.BIN', out/'image.bin')})
    (out / 'result.json').write_text(json.dumps(record, indent=2)+'\n')
    return record


if __name__ == '__main__':
    result = build()
    result['format']=result['format'].copy()
    result['format']['relocations']=len(result['format']['relocations'])
    print(json.dumps({k: result[k] for k in (
        'checkpoint', 'format', 'allocation_bytes', 'free_halt_bytes')}, indent=2))
