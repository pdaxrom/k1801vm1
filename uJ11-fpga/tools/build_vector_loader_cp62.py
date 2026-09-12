#!/usr/bin/env python3
"""Cold HALT boot experiment over the exact CP60b measured board."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tarfile
from board_common import ROOT, CORE, BOARD
from build_fram_cp52 import replace_once as rep
from build_firmware import generate as firmware_rom
from make_ebr import generate as microstore
sys.path.insert(0, str(ROOT/'microasm'))
from uj11asm import assemble

OUT = ROOT/'build/cp62-boot'
FW = ROOT/'firmware/cp62'


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def firmware(assembler=None):
    if assembler is not None:
        for name in ('resident', 'start'):
            with (FW/(name+'.log')).open('w') as log:
                subprocess.run([str(assembler), '-binary', '--cpu', 'dcj-11', '--list',
                                str(FW/(name+'.lst')), str(FW/(name+'.asm')), str(FW/(name+'.bin'))],
                               stdout=log, stderr=subprocess.STDOUT, check=True)
        (FW/'inputs.json').write_text(json.dumps(dict(assembler_sha256=sha(assembler), files={
            str(p.relative_to(ROOT)):sha(p) for name in ('resident','start')
            for p in [FW/(name+ext) for ext in ('.asm','.bin','.lst','.log')]}), indent=2)+'\n')
    record = json.loads((FW/'inputs.json').read_text())
    for name,digest in record['files'].items(): assert sha(ROOT/name)==digest,name
    resident = (FW/'resident.bin').read_bytes()
    start = (FW/'start.bin').read_bytes()
    assert len(resident)==164 and len(start)<=54
    for name in ('resident','start'):
        assert 'Errors: No error' in (FW/(name+'.lst')).read_text()
    labels = {name:int(value,8) for name,value in re.findall(
        r'^\[([^]]+)\]\s+([0-7]+)$', (FW/'resident.lst').read_text(), re.M)}
    assert labels['resident_entry']==0o200 and labels['initialize']==0o322 and labels['fault']==0o312
    assert labels['copy_user']==0o422 and len(start)==50
    return resident,start,record


def build():
    manifest = json.loads((ROOT/'synth/reports/cp60b/inputs.json').read_text())
    frozen = {}
    with tarfile.open(ROOT/'synth/reports/cp60b/source.tgz') as archive:
        for name,digest in manifest['files'].items():
            if name.startswith('generated:'):continue
            data=archive.extractfile(name).read()
            assert hashlib.sha256(data).hexdigest()==digest,name
            frozen[name]=data
    resident,start,fw_inputs=firmware()
    prefix='build/cp60-rk/'
    original=frozen[prefix+'service.uasm'].decode()
    old,_,old_labels,_=assemble(original)
    # T4 was cleared by reset. Reuse the existing two-word vector reader.
    source=rep(original,'    JUMP, target=FETCH\n.org $020',
               '    JUMP, target=S_VECTOR_READ, prefetch=0\n.org $020')
    image,listing,labels,stats=assemble(source)
    assert [i for i in range(1024) if old[i]!=image[i]]==[16]
    assert labels==old_labels and stats['used_words']==1002
    words=[int(w,16) for w in frozen[prefix+'firmware.mem'].decode().split()]
    assert firmware_rom(words).replace('microcode/generated/firmware.mem',prefix+'firmware.mem').encode()==frozen[prefix+'src/microcode/generated/uj11_firmware_rom.v']
    # Exact free holes: preserve every SD/RK instruction and all RK CSR cells.
    for offset,data in ((426,start),(512+348,resident)):
        assert all(w==0 for w in words[offset//2:(offset+len(data))//2])
        for i in range(0,len(data),2):words[(offset+i)//2]=int.from_bytes(data[i:i+2],'little')
    outputs=[]
    def put(name,text):
        p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)
        outputs.append(str(p.relative_to(ROOT)))
    put('service.uasm',source);put('m0.mem',''.join(f'{w:09x}\n' for w in image))
    put('m0.lst',listing+'\n');put('m0.labels.json',json.dumps(labels,indent=2)+'\n')
    put('m0.stats.json',json.dumps(dict(used_words=1002,free_words=22),indent=2)+'\n')
    put('uj11_m0_ebr.v',microstore(image))
    put('decode.mem',frozen[prefix+'decode.mem'].decode())
    put('firmware.mem',''.join(f'{w:04x}\n' for w in words))
    for name in CORE+BOARD+['boards/hc1200/uj11_microcomp.v']:
        text=frozen[prefix+'src/'+name].decode().replace(prefix,'build/cp62-boot/')
        if name=='rtl/uj11_engine.v':
            text=rep(text,'            service_mode<=0;','            service_mode<=1;')
            text=rep(text,'            frame_active <= 0;','            frame_active <= 1;')
            text=rep(text,"service_ready[0] ? S_ODT : 10'h018",'S_ODT')
            # ABI-1 UJLOAD probes UJSTAT before touching HALT RAM. Its reserved
            # handler now rejects CP62 safely; installation is a HALT service.
            text=rep(text,"service_ir[5] ? service_ir[4:1]==2 : |service_ir[4:3]",
                     "service_ir[5] ? ~|service_ir[4:3] : |service_ir[4:3]")
        if name=='boards/hc1200/uj11_board_bus.v':
            start_index=text.index('\twire local_boot_selected =')
            end_index=text.index('\twire boot_program_selected =',start_index)
            text=text[:start_index]+'''\t// Only the cold HALT PC/PSW pair is supplied by the small overlay.
\t// USER bootstrap and legacy USER ODT words now reside in FRAM.
\twire local_boot_selected = bank && !physical && BOOT_ROM_ENABLE &&
\t\tboot_overlay_active && !write && word_address[15:2]==0;

'''+text[end_index:]
            text=rep(text,'wire boot_program_selected = !bank && !physical','wire boot_program_selected = bank && !physical')
            text=rep(text,'word_address >= BOOT_BASE &&\n\t\tword_address <= BOOT_LAST;',
                     "word_address[15:10]==6'd4;")
            start_index=text.index('\t// Cold uJ11 starts at PC=0.')
            end_index=text.index('\n\tend',start_index)+len('\n\tend')
            text=text[:start_index]+'''\t// Fixed initial HALT vector. Every USER address reads ordinary RAM.
\talways @(*) local_rdata = word_address[1] ? 16'o000340 : 16'o010652;'''+text[end_index:]
            text=rep(text,'{service_program_selected,word_address[8:1]};',
                     '{service_program_selected || (bank && word_address[9]),word_address[8:1]};')
        if name=='microcode/generated/uj11_firmware_rom.v':
            text=firmware_rom(words).replace('microcode/generated/firmware.mem','build/cp62-boot/firmware.mem')
        put('src/'+name,text)
    inputs=['tools/build_vector_loader_cp62.py','tools/build_firmware.py','tools/build_fram_cp52.py',
            'tools/board_common.py','tools/make_ebr.py','microasm/uj11asm.py',
            'synth/reports/cp60b/inputs.json','synth/reports/cp60b/source.tgz','firmware/cp62/inputs.json']+list(fw_inputs['files'])
    record=dict(reference='cp60b',mmu=False,used_words=1002,cold_halt=True,
                resident_bytes=len(resident),start_bytes=len(start),
                inputs={p:sha(ROOT/p) for p in inputs},outputs={p:sha(ROOT/p) for p in outputs})
    (OUT/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def adapt():
    build()
    return ([str((OUT/'src'/p).relative_to(ROOT)) for p in CORE],
            [str((OUT/'src'/p).relative_to(ROOT)) for p in BOARD])


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assemble',action='store_true')
    parser.add_argument('--assembler',type=Path,default=ROOT/'../microasm11/microasm11')
    args=parser.parse_args()
    if args.assemble:firmware(args.assembler)
    print(json.dumps(build(),indent=2))
