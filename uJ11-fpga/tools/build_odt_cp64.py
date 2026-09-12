#!/usr/bin/env python3
"""Build the HALT-FRAM ODT with DEC MACRO/LINK; no RTL/microasm11 mutations."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from board_common import ROOT
from rt11_build import build as assemble
from service_image_cp62 import pack, decode


def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def tables():
    rows=[]
    def add(mask, value, form, name): rows.append((mask,value,form,name))
    for v,n in enumerate(('HALT','WAIT','RTI','BPT','IOT','RESET','RTT','MFPT')):add(0xffff,v,0,n)
    for v,n in [(0o240,'NOP'),(0o241,'CLC'),(0o242,'CLV'),(0o244,'CLZ'),(0o250,'CLN'),
                (0o257,'CCC'),(0o261,'SEC'),(0o262,'SEV'),(0o264,'SEZ'),(0o270,'SEN'),(0o277,'SCC')]:add(0xffff,v,0,n)
    # Unnamed flag combinations are .WORD (do not mislabel operand formats).
    for base,n in [(0o10000,'MOV'),(0o20000,'CMP'),(0o30000,'BIT'),(0o40000,'BIC'),(0o50000,'BIS'),(0o60000,'ADD'),
                   (0o110000,'MOVB'),(0o120000,'CMPB'),(0o130000,'BITB'),(0o140000,'BICB'),(0o150000,'BISB'),(0o160000,'SUB')]:add(0xf000,base,2,n)
    for base,n in [(0o400,'BR'),(0o1000,'BNE'),(0o1400,'BEQ'),(0o2000,'BGE'),(0o2400,'BLT'),(0o3000,'BGT'),(0o3400,'BLE'),
                   (0o100000,'BPL'),(0o100400,'BMI'),(0o101000,'BHI'),(0o101400,'BLOS'),(0o102000,'BVC'),(0o102400,'BVS'),
                   (0o103000,'BCC'),(0o103400,'BCS')]:add(0xff00,base,3,n)
    for i,n in enumerate(('CLR','COM','INC','DEC','NEG','ADC','SBC','TST','ROR','ROL','ASR','ASL')):
        add(0xffc0,0o5000+i*64,1,n);add(0xffc0,0o105000+i*64,1,n+'B')
    for base,n in [(0o100,'JMP'),(0o300,'SWAB'),(0o6700,'SXT'),(0o106400,'MTPS'),(0o106700,'MFPS')]:add(0xffc0,base,1,n)
    add(0xfff8,0o230,10,'SPL');add(0xfff8,0o200,4,'RTS');add(0xfe00,0o4000,5,'JSR');add(0xfe00,0o77000,7,'SOB')
    for base,n in [(0o70000,'MUL'),(0o71000,'DIV'),(0o72000,'ASH'),(0o73000,'ASHC')]:add(0xfe00,base,6,n)
    add(0xfe00,0o74000,5,'XOR');add(0xffc0,0o6400,8,'MARK')
    for base,n in [(0o104000,'EMT'),(0o104400,'TRAP')]:add(0xff00,base,9,n)
    for base,n in [(0o75000,'FADD'),(0o75010,'FSUB'),(0o75020,'FMUL'),(0o75030,'FDIV')]:add(0xfff8,base,4,n)
    # Table stores the complement of mask, for a single BIC in the decoder.
    text='DTAB:\n'+''.join(f'\t.WORD {mask^65535:o},{value:o},{form:o},DN{i}\n' for i,(mask,value,form,n) in enumerate(rows))
    # An all-ones sentinel distinguishes exact-match entries (complement zero).
    text+='\t.WORD 177777\n'
    text+=''.join(f'DN{i}:\t.ASCIZ /{n}/\n' for i,(_,_,_,n) in enumerate(rows))+'\t.EVEN\n'
    return text


def build(out):
    out.mkdir(parents=True,exist_ok=True)
    files=[ROOT/'firmware/odt'/n for n in ('ODT.MAC','DISASM.MAC','PANEL.MAC','DATA.MAC')]
    driver=ROOT/'demos/rt11/panel/PNLDRV.MAC'
    pnl=driver.read_text()
    pnl=pnl[pnl.index('PANEL\t='):pnl.rindex('\t.END')]
    pnl=re.sub(r'^\s*\.PSECT.*$', '\t.ASECT', pnl, flags=re.M)
    pnl=pnl.replace('\tSOB\tR1,10$\n\tMOV\t(SP)+,R1', '\tJSR\tPC,RXPOLL\n\tSOB\tR1,10$\n\tMOV\t(SP)+,R1')
    # Proven glyphs and shift order; mutable driver data belongs outside IMMEND.
    scanner=ROOT/'firmware/odt/PNKEY.MAC'
    pnl=pnl[:pnl.index('PNKEY:')]+scanner.read_text()+pnl[pnl.index('SHBYTE:'):]
    code,data=pnl.split('SHADOW:',1)
    text=''.join(p.read_text() for p in files[:3])+code+tables()+'IMMEND:\nSHADOW:'+data
    text+=files[3].read_text().replace('IMMEND:','')
    files.extend((driver,scanner))
    src=out/'UJMON.MAC';src.write_text(text)
    asm=out/'asm'
    record=assemble([src],asm,ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    listing=(asm/'UJMON.LST').read_text(errors='replace')
    symbols={n:int(v,8) for n,v in re.findall(r'\b([A-Z][A-Z0-9]{0,5})\s+([0-7]{6})(?![0-9R])',listing)}
    for n in ('ODT','ACTV','ENTER','ODERR','IMMEND','PAYEND','MEMEND','REGS','RESULT','MAIN'):
        assert n in symbols,n
    assert symbols['ODT']==0o10000 and symbols['ACTV']==0o10004
    assert symbols['MEMEND']<=0o40000
    blob=(asm/'UJMON.SAV').read_bytes()
    payload=blob[0o10000:symbols['PAYEND']]
    result=pack(payload,1,symbols['MEMEND']-0o10000,symbols['ODT'],symbols['ODERR'])
    (out/'ODT.BIN').write_bytes(result)
    (out/'payload.bin').write_bytes(payload)
    immutable=payload[:symbols['IMMEND']-0o10000]
    checksum=sum(struct.unpack('<'+'H'*(len(immutable)//2),immutable))&65535
    result=dict(format=decode(result),immutable_bytes=len(immutable),immutable_checksum=checksum,
                symbols=symbols,sources={str(p.relative_to(ROOT)):digest(p) for p in files},assembler=record)
    template=ROOT/'demos/rt11/service/cp64/UJON.MAC.in'
    loader=ROOT/'demos/rt11/service/cp62/UJLOAD.MAC'
    old=loader.read_text()
    api=old[old.index('GETST:'):old.index('; Header and exact')]
    helper=old[old.index('\nHELPER:\n')+1:old.index('; END GENERATED HELPER')]
    constants=f"IMMCNT={len(immutable)//2:o}\nIMMSUM={checksum:o}\nODFAIL={symbols['ODERR']:o}"
    activation=out/'UJON.MAC'
    activation.write_text(template.read_text().replace('@@CONSTANTS@@',constants).replace('@@API@@',api).replace('@@HELPER@@',helper))
    result['activation']=assemble([activation],out/'activation',ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    result['sources'].update({str(p.relative_to(ROOT)):digest(p) for p in (template,loader,Path(__file__))})
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('symbols','assembler')},indent=2))
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    build(p.parse_args().out.resolve())
