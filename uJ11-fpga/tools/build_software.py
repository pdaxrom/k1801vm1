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
from module_relocation import build_module
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
    binary, relocation = build_module(src, base, len(image), len(image), image)
    (out/'image.bin').write_bytes(image)
    (out/(source.stem+'.BIN')).write_bytes(binary)
    record = dict(format=decode(binary), symbols=symbols,
                  relocation_assembly=str(relocation.relative_to(ROOT)),
                  relocation_assembler=json.loads((relocation/'build-inputs.json').read_text()),
                  assembly=str(directory.relative_to(ROOT)), assembler=assembly,
                  sources={str(p.relative_to(ROOT)):sha(p) for p in (source, *inputs,
                      ROOT/'tools/module_relocation.py',ROOT/'tools/module_image.py')},
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
    binary, relocation = build_module(src, sym['INIT'], len(image), sym['MEMEND']-sym['INIT'], image)
    (out/'ODT.BIN').write_bytes(binary)
    (out/'image.bin').write_bytes(image)
    (out/'payload.bin').write_bytes(blob[sym['INIT']:sym['PAYEND']])
    paths+=tail+[ROOT/'tools/odt_tables.py',Path(__file__),ROOT/'tools/module_relocation.py',ROOT/'tools/module_image.py']
    record=dict(format=decode((out/'ODT.BIN').read_bytes()),symbols=sym,assembler=assembly,
        relocation_assembly=str(relocation.relative_to(ROOT)),
        relocation_assembler=json.loads((relocation/'build-inputs.json').read_text()),
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


def reboot():
    # Read the already generated ROM components; build_hardware owns the exact
    # low resident listing extraction. No new ROM content is generated here.
    hw=ROOT/'build/hardware';out=OUT/'reboot';out.mkdir(parents=True,exist_ok=True)
    walker,bs,_,_=native(ROOT/'firmware/boot/BOOT.MAC')
    data=walker[bs['MSTART']:bs['MEND']]
    data=(hw/'sdbase.bin').read_bytes()+(hw/'resid.bin').read_bytes()+data
    def words(blob):
        w=[int.from_bytes(blob[i:i+2],'little') for i in range(0,len(blob),2)]
        return ''.join('\t.WORD '+','.join(f'{v:o}' for v in w[i:i+8])+'\n' for i in range(0,len(w),8))
    source=ROOT/'firmware/modules/REBOOT.MAC.in'
    text=source.read_text().replace('@@PAYLOAD@@',words(data))
    text=text.replace('\t.TITLE REBOOT','\t.TITLE REBOOT\nBWORDS='+f"{(bs['MEND']-bs['MSTART'])//2:o}")
    src=out/'REBOOT.MAC';src.write_text(text)
    blob,sym,_,_=native(src);tramp=blob[0o1000:sym['IMEND']]
    assert sym['IMEND']<=0o4000,'trampoline overlaps bootstrap destination'
    template=ROOT/'firmware/modules/UJBOOT.MAC.in';src=out/'UJBOOT.MAC'
    src.write_text(template.read_text().replace('@@TRAMPOLINE@@',words(tramp)))
    blob,sym,assembly,directory=native(src);(out/'UJBOOT.SAV').write_bytes(blob)
    paths=(template,source,ROOT/'firmware/boot/BOOT.MAC',ROOT/'firmware/boot/RESID.MAC',
           ROOT/'firmware/boot/SDBASE.MAC',Path(__file__))
    record=dict(assembly=str(directory.relative_to(ROOT)),assembler=assembly,
                sources={str(p.relative_to(ROOT)):sha(p) for p in paths},
                outputs={str(p.relative_to(ROOT)):sha(p) for p in (src,out/'REBOOT.MAC',out/'UJBOOT.SAV')})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');return record


if __name__=='__main__':
    for name,fn in (('odt',odt),('sdboot',bootstrap),('loader',loader)):
        result=fn();fmt=result.get('format',{}).copy()
        if 'relocations' in fmt:fmt['relocations']=len(fmt['relocations'])
        print(name,fmt,flush=True)
