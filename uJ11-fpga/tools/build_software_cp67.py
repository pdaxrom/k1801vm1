#!/usr/bin/env python3
"""Native DEC assembly of retained ODT and a replaceable bootstrap module."""
import hashlib
import json
import re
import tarfile
from pathlib import Path
from board_common import ROOT
from build_odt_cp64 import tables
from rt11_build import build as assemble
from module_image_cp67 import pack, decode

OUT = ROOT/'build/cp67-software'


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def native(src):
    out = src.parent/('asm-'+sha(src)[:12])
    if not (out/'build-inputs.json').exists():
        assemble([src], out, ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    meta=json.loads((out/'build-inputs.json').read_text())
    assert sha(src) in meta['source_sha256'].values()
    for name, digest in meta['outputs'].items():
        assert sha(out/name)==digest, name
    listing=(out/(src.stem+'.LST')).read_text(errors='replace')
    symbols={n:int(v,8) for n,v in re.findall(r'\b([A-Z][A-Z0-9]{0,5})\s*=?\s+([0-7]{6})(?![0-9R])',listing)}
    return (out/(src.stem+'.SAV')).read_bytes(),symbols,meta,out


def odt(out=None):
    out=out or OUT/'odt';out.mkdir(parents=True,exist_ok=True)
    files=[ROOT/'firmware/odt'/n for n in ('ODT.MAC','DISASM.MAC','PANEL.MAC','NAV.MAC','OVER.MAC','BREAK.MAC','FPDEBUG.MAC','DATA.MAC')]
    driver=ROOT/'demos/rt11/panel/PNLDRV.MAC'
    scanner=ROOT/'firmware/odt/PNKEY.MAC'
    cold=ROOT/'firmware/cp67/ODINIT.MAC'
    pnl=driver.read_text()
    pnl=pnl[pnl.index('PANEL\t='):pnl.rindex('\t.END')]
    pnl=re.sub(r'^\s*\.PSECT.*$', '\t.ASECT', pnl, flags=re.M)
    pnl=pnl.replace('\tSOB\tR1,10$\n\tMOV\t(SP)+,R1', '\tJSR\tPC,RXPOLL\n\tSOB\tR1,10$\n\tMOV\t(SP)+,R1')
    pnl=pnl[:pnl.index('PNKEY:')]+scanner.read_text()+pnl[pnl.index('SHBYTE:'):]
    code,data=pnl.split('SHADOW:',1)
    mutable,constant=data.split('BLANKS:',1)
    main=files[0].read_text()
    main=main.replace('ODT:\tJMP @#ENTER\nACTV:\tJMP @#ENABLE','INIT:\tJMP @#COLD\nODT:\tJMP @#ENTER')
    start=main.index('; Invoked ONLY after UJON')
    main=main[:start]+main[main.index('; Both the normal HALT wrapper',start):]
    main=main.replace('; Absolute HALT-FRAM module, loaded with the unchanged ABI2 UJLOAD.',
                      '; CP67 ABI3 retained module: load with UJMOD; cold entry initializes itself.')
    text=main+''.join(p.read_text() for p in files[1:-1])+code+tables()+cold.read_text()
    text+='BLANKS:'+constant+'IMMEND:\nSHADOW:'+mutable
    text+=files[-1].read_text().replace('IMMEND:','').replace('\t.END ODT','\t.END INIT')
    src=out/'UJMON.MAC';src.write_text(text)
    blob,sym,meta,asm=native(src)
    assert sym['INIT']==0o10000 and sym['MEMEND']<=0o60000
    assert sym['FONT']<sym['IMMEND'] and sym['KEYMAP']<sym['IMMEND']
    image=blob[0o10000:sym['IMMEND']]
    result=pack(0o10000,image)
    (out/'ODT.BIN').write_bytes(result)
    # Full initialized payload also supports existing ODT command regressions.
    (out/'payload.bin').write_bytes(blob[0o10000:sym['PAYEND']])
    (out/'image.bin').write_bytes(image)
    record=dict(format=decode(result),symbols=sym,assembler=meta,assembly=str(asm.relative_to(ROOT)),
                allocation_bytes=sym['MEMEND']-0o10000,free_bytes=0o60000-sym['MEMEND'],
                sources={str(p.relative_to(ROOT)):sha(p) for p in files+[driver,scanner,cold,Path(__file__),ROOT/'tools/module_image_cp67.py']},
                outputs={str(p.relative_to(ROOT)):sha(p) for p in (src,out/'ODT.BIN',out/'payload.bin',out/'image.bin')})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def bootstrap():
    out=OUT/'sdboot';out.mkdir(parents=True,exist_ok=True)
    reference=ROOT/'synth/reports/cp63b'
    meta=json.loads((reference/'inputs.json').read_text())
    name='build/cp63-debug/firmware.mem'
    with tarfile.open(reference/'source.tgz') as t:
        data=t.extractfile(name).read()
    assert hashlib.sha256(data).hexdigest()==meta['files'][name]
    words=[int(w,16) for w in data.decode().split()][:213]
    template=ROOT/'firmware/cp67/SDBOOT.MAC.in'
    src=out/'SDBOOT.MAC'
    src.write_text(template.read_text().replace('@@BOOTSTRAP@@',''.join('\t.WORD '+','.join(f'{w:o}' for w in words[i:i+8])+'\n' for i in range(0,len(words),8))))
    blob,sym,assembly,asm=native(src)
    assert sym['INIT']==0o6000 and sym['DEND']<=0o7000
    data=blob[0o6000:sym['DEND']]
    assert blob[sym['DATA']:sym['DEND']]==b''.join(w.to_bytes(2,'little') for w in words)
    (out/'SDBOOT.BIN').write_bytes(pack(0o6000,data))
    (out/'image.bin').write_bytes(data)
    record=dict(format=decode((out/'SDBOOT.BIN').read_bytes()),symbols=sym,assembler=assembly,
                assembly=str(asm.relative_to(ROOT)),sources={str(p.relative_to(ROOT)):sha(p) for p in (template,reference/'inputs.json',reference/'source.tgz',Path(__file__))},
                outputs={str(p.relative_to(ROOT)):sha(p) for p in (src,out/'SDBOOT.BIN',out/'image.bin')})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def loader():
    out=OUT/'loader';out.mkdir(parents=True,exist_ok=True)
    old=ROOT/'demos/rt11/service/cp62/UJLOAD.MAC'
    template=ROOT/'firmware/cp67/UJMOD.MAC.in'
    previous=old.read_text()
    macros=previous[previous.index('\t.MACRO'):previous.index('HDBYT=')]
    macros+='\t.MACRO JCC DST,?OK\n\tBCS OK\n\tJMP DST\nOK:\n\t.ENDM\n'
    name=previous[previous.index('NAME50:'):previous.index('\nLOAD:')]
    api=previous[previous.index('RDWORD:'):previous.index('; Header and exact')]
    helper=previous[previous.index('\nHELPER:\n')+1:previous.index('; END GENERATED HELPER')]
    text=template.read_text()
    for key,value in (('MACROS',macros),('NAME50',name),('API',api),('HELPER',helper)):
        text=text.replace('@@'+key+'@@',value)
    src=out/'UJMOD.MAC';src.write_text(text)
    blob,sym,assembly,asm=native(src)
    (out/'UJMOD.SAV').write_bytes(blob)
    record=dict(symbols=sym,assembler=assembly,assembly=str(asm.relative_to(ROOT)),
                sources={str(p.relative_to(ROOT)):sha(p) for p in (old,template,Path(__file__))},
                outputs={str(p.relative_to(ROOT)):sha(p) for p in (src,out/'UJMOD.SAV')})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


if __name__=='__main__':
    for name,fn in (('odt',odt),('sdboot',bootstrap),('loader',loader)):
        r=fn();print(json.dumps(dict(name=name,**{k:v for k,v in r.items() if k in ('format','allocation_bytes','free_bytes')}),indent=2))
