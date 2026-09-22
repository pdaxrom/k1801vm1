#!/usr/bin/env python3
"""Assemble current ABI3 modules with DEC MACRO/LINK in RT-11."""
import hashlib
import json
import re
from pathlib import Path
from board_common import ROOT
from rt11_build import build as assemble
from module_image import pack, decode
from odt_tables import tables
OUT = ROOT/'build/software'

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def native(src):
    out = ROOT/'build/assembly'/(src.stem.lower()+'-'+sha(src)[:12])
    if not (out/'build-inputs.json').exists():
        assemble([src], out, ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    meta=json.loads((out/'build-inputs.json').read_text())
    assert sha(src) in meta['source_sha256'].values()
    for name, digest in meta['outputs'].items():
        assert sha(out/name)==digest, name
    listing=(out/(src.stem+'.LST')).read_text(errors='replace')
    symbols={n:int(v,8) for n,v in re.findall(r'\b([A-Z][A-Z0-9]{0,5})\s*=?\s+([0-7]{6})(?![0-9R])',listing)}
    return (out/(src.stem+'.SAV')).read_bytes(),symbols,meta,out


def module(source, out, base, end, inputs=()):
    out.mkdir(parents=True, exist_ok=True)
    src = out/source.name
    src.write_bytes(source.read_bytes())
    blob, symbols, assembly, directory = native(src)
    image = blob[base:symbols[end]]
    (out/'image.bin').write_bytes(image)
    (out/(source.stem+'.BIN')).write_bytes(pack(base, image))
    record = dict(format=decode(pack(base,image)), symbols=symbols,
                  assembly=str(directory.relative_to(ROOT)), assembler=assembly,
                  sources={str(p.relative_to(ROOT)):sha(p) for p in (source, *inputs)},
                  outputs={str(p.relative_to(ROOT)):sha(p) for p in
                           (src,out/'image.bin',out/(source.stem+'.BIN'))})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def odt(out=None):
    out=out or OUT/'odt';out.mkdir(parents=True,exist_ok=True)
    names=('ODT','DISASM','PANEL','NAV','OVER','BREAK','FPDEBUG','DRIVER')
    paths=[ROOT/'firmware/odt'/f'{n}.MAC' for n in names]
    tail=[ROOT/'firmware/odt'/f'{n}.MAC' for n in ('ODINIT','CONSTS','STATE','DATA')]
    text=''.join(p.read_text() for p in paths)+tables()+''.join(p.read_text() for p in tail)
    src=out/'UJMON.MAC';src.write_text(text)
    blob,sym,assembly,directory=native(src)
    assert sym['INIT']==0o10000 and sym['MEMEND']<=0o60000
    image=blob[sym['INIT']:sym['IMMEND']]
    (out/'ODT.BIN').write_bytes(pack(sym['INIT'],image))
    (out/'image.bin').write_bytes(image)
    (out/'payload.bin').write_bytes(blob[sym['INIT']:sym['PAYEND']])
    paths+=tail+[ROOT/'tools/odt_tables.py',Path(__file__)]
    record=dict(format=decode((out/'ODT.BIN').read_bytes()),symbols=sym,assembler=assembly,
        assembly=str(directory.relative_to(ROOT)),allocation_bytes=sym['MEMEND']-sym['INIT'],
        free_bytes=0o60000-sym['MEMEND'],sources={str(p.relative_to(ROOT)):sha(p) for p in paths},
        outputs={str(p.relative_to(ROOT)):sha(p) for p in (src,out/'ODT.BIN',out/'image.bin',out/'payload.bin')})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');return record


def bootstrap():
    out=OUT/'sdboot-source';out.mkdir(parents=True,exist_ok=True)
    raw,sym,_,_=native(ROOT/'firmware/boot/SDBASE.MAC')
    data=raw[0o4000:sym['IMEND']]
    assert len(data)==426
    words=[int.from_bytes(data[i:i+2],'little') for i in range(0,len(data),2)]
    template=ROOT/'firmware/boot/SDBOOT.MAC.in'
    text=template.read_text().replace('@@BOOTSTRAP@@',''.join(
        '\t.WORD '+','.join(f'{w:o}' for w in words[i:i+8])+'\n' for i in range(0,len(words),8)))
    source=out/'SDBOOT.MAC';source.write_text(text)
    return module(source,OUT/'sdboot',0o6000,'DEND',
                  (template,ROOT/'firmware/boot/SDBASE.MAC',Path(__file__)))


def loader():
    out=OUT/'loader';out.mkdir(parents=True,exist_ok=True)
    original=ROOT/'firmware/modules/UJMOD.MAC.in';src=out/'UJMOD.MAC'
    raw,hs,_,_=native(ROOT/'firmware/modules/HELPER.MAC')
    words=[int.from_bytes(raw[i:i+2],'little') for i in range(0o1000,hs['IMEND'],2)]
    helper=''.join('\t.WORD '+','.join(f'{w:o}' for w in words[i:i+8])+'\n' for i in range(0,len(words),8))
    src.write_text(original.read_text().replace('@@HELPER@@',helper))
    blob,symbols,assembly,directory=native(src);(out/'UJMOD.SAV').write_bytes(blob)
    record=dict(symbols=symbols,assembler=assembly,assembly=str(directory.relative_to(ROOT)),
        sources={str(p.relative_to(ROOT)):sha(p) for p in
                 (original,ROOT/'firmware/modules/HELPER.MAC',Path(__file__))},
        outputs={str(p.relative_to(ROOT)):sha(p) for p in (src,out/'UJMOD.SAV')})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');return record


if __name__=='__main__':
    for name,fn in (('odt',odt),('sdboot',bootstrap),('loader',loader)):
        result=fn();print(name, result.get('format',{}),flush=True)
