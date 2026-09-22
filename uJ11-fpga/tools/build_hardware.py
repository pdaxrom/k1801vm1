#!/usr/bin/env python3
"""Build microcode, decoder and board firmware from the production sources."""
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys
from board_common import ROOT, CORE, BOARD
from build_software import native
from firmware_rom import firmware_rom
from make_ebr import generate
OUT=ROOT/'build/hardware'
sys.path.insert(0,str(ROOT/'microasm'))
from uj11asm import assemble


def build():
    OUT.mkdir(parents=True,exist_ok=True)
    image,listing,labels,stats=assemble((ROOT/'microcode/uj11.uasm').read_text())
    (OUT/'m0.mem').write_text(''.join(f'{w:09x}\n' for w in image))
    (OUT/'m0.lst').write_text(listing)
    for name,obj in (('m0.labels.json',labels),('m0.stats.json',stats)):
        (OUT/name).write_text(json.dumps(obj,indent=2)+'\n')
    (OUT/'uj11_m0_ebr.v').write_text(generate(image))
    subprocess.run([sys.executable,str(ROOT/'tools/build_decode_rom.py')],check=True,cwd=ROOT)
    words=[0]*1024;records={}
    # Each program has its own logical origin. ROM placement is explicit here.
    for name,origin,offset,limit in (('SDBASE',0o4000,0,426),('START',0o10652,426,54),
                                   ('RKSERV',0o160000,512,348),('RESID',0o200,860,164)):
        src=ROOT/'firmware/boot'/f'{name}.MAC'
        blob,sym,meta,directory=native(src)
        data=blob[origin:sym['IMEND']]
        if origin < 512:
            # LINK writes its SAV header over low absolute memory. The MACRO
            # listing retains every assembled word of the ROM-only resident.
            values={}
            for line in (directory/(name+'.LST')).read_text(errors='replace').splitlines():
                fields=line.split('\t')
                if len(fields)<3 or not re.fullmatch(r'[0-7]{6}',fields[1].strip()):continue
                address=int(fields[1].strip(),8)
                for field in fields[2:]:
                    word=field.strip()
                    if not re.fullmatch(r'[0-7]{6}',word):break
                    if origin<=address<sym['IMEND']:values[address]=int(word,8)
                    address+=2
            if set(values)!=set(range(origin,sym['IMEND'],2)):
                raise ValueError('Incomplete absolute resident listing')
            data=b''.join(values[a].to_bytes(2,'little') for a in sorted(values))
        if len(data)>limit:raise ValueError(f'{name}: expected {limit} bytes, got {len(data)}')
        for i in range(0,len(data),2):words[(offset+i)//2]=int.from_bytes(data[i:i+2],'little')
        records[name]=meta
        (OUT/(name.lower()+'.bin')).write_bytes(data)
    blob,sym,meta,directory=native(ROOT/'firmware/boot/BOOT.MAC')
    for start,a,b in ((1024,'CSTART','CEND'),(1056,'MSTART','MEND')):
        data=blob[sym[a]:sym[b]]
        assert len(data)<=(32 if a=='CSTART' else 992)
        for i in range(0,len(data),2):words[(start+i)//2]=int.from_bytes(data[i:i+2],'little')
    records['BOOT']=meta
    helper,hs,hm,_=native(ROOT/'firmware/modules/HELPER.MAC')
    (OUT/'helper.bin').write_bytes(helper[0o1000:hs['IMEND']])
    records['HELPER']=hm
    (OUT/'firmware.mem').write_text(''.join(f'{w:04x}\n' for w in words))
    (OUT/'uj11_firmware_rom.v').write_text(firmware_rom(words))
    (OUT/'boot-symbols.json').write_text(json.dumps(sym,indent=2)+'\n')
    sources=CORE+BOARD+['microcode/uj11.uasm','microasm/uj11asm.py','tools/build_hardware.py',
        'tools/build_decode_rom.py','tools/firmware_rom.py','tools/make_ebr.py',
        'tools/build_software.py','tools/rt11_build.py','firmware/modules/HELPER.MAC']
    sources += [str(p.relative_to(ROOT)) for p in (ROOT/'firmware/boot').glob('*.MAC')]
    record=dict(mmu=False,microcode_words=stats['used_words'],assembler=records,
                files={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources})
    (OUT/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def adapt():
    build()
    return CORE.copy(),BOARD.copy()


if __name__=='__main__':
    print(json.dumps({k:v for k,v in build().items() if k not in ('assembler','files')},indent=2))
