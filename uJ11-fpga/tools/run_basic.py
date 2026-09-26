#!/usr/bin/env python3
"""Full production-board simulation of DEC BASIC FIS/FPU and software FPP."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
from board_common import ROOT
from build_hardware import adapt, OUT as HW, firmware_rom
from build_software import odt, bootstrap, loader
from build_fpp import build as build_fpp
from module_image import decode, entry


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def run(out, configuration=False, variant='all'):
    odt();bootstrap();loader();build_fpp()
    programs=ROOT/'demos/rt11/basic'
    base=ROOT/'../lsi11-fpga/images/rt11v503.dsk'
    out.mkdir(parents=True, exist_ok=True)
    image=out/'test.dsk'
    assert not image.exists(), 'fresh output directory required'
    core,board=adapt()
    sym=json.loads((HW/'boot-symbols.json').read_text())
    words=[int(w,16) for w in (HW/'firmware.mem').read_text().split()]
    overrides=[]
    for n in ('RLOOPS','RSPINS'):
        idx=(1056+sym[n]-sym['MSTART'])//2
        overrides.append(dict(symbol=n,index=idx,production=words[idx],test=1));words[idx]=1
    (out/'firmware.mem').write_text(''.join(f'{w:04x}\n' for w in words))
    (out/'firmware.v').write_text(firmware_rom(words).replace('build/hardware/firmware.mem',str((out/'firmware.mem').relative_to(ROOT))))
    board=[str((out/'firmware.v').relative_to(ROOT)) if p.endswith('/uj11_firmware_rom.v') else p for p in board]
    # Rebuilt production modules, independent of any saved simulator state.
    locations=[ROOT/'build/software/odt',ROOT/'build/software/sdboot',
               ROOT/'build/software/loader',ROOT/'build/fpp/software']
    modules=[p/n for p,n in zip(locations,['ODT.BIN','SDBOOT.BIN','UJMOD.SAV','FP11.BIN'])]
    shutil.copyfile(base,image)
    rt=ROOT/'../lsi11/rt11tool'
    basic_sources=[programs/'B81TST.BAS',programs/'B81DBL.BAS']
    basic_files=[]
    for source in basic_sources:
        target=out/source.name
        target.write_bytes(source.read_text().replace('\n','\r\n').encode('ascii'))
        basic_files.append(target)
    paths=modules+basic_files+sorted((ROOT/'releases/basic').glob('*.SAV'))
    for p in paths:
        subprocess.run([str(rt),'add',str(image),str(p),p.name],check=True,capture_output=True)
    paths += basic_sources
    original=ROOT/'tests/rt11_harness.vh';cases=ROOT/'tests/basic_rt11.vh'
    text=original.read_text()
    body=cases.read_text()
    if configuration:
        state=bytearray(131072)
        for slot,path in enumerate((modules[0],modules[1],modules[3])):
            from module_image import placed_image
            data=path.read_bytes();h=decode(data);payload=placed_image(data)
            state[65536+h['base']:65536+h['base']+len(payload)]=payload
            address=65536+0o7000+8*slot
            state[address:address+8]=entry(h['base'],payload)
        retained=out/'retained.mem';retained.write_text(''.join(f'{b:02x}\n' for b in state))
        body=body[:body.index('    initial begin\n')]+(ROOT/'tests/basic_modules.vh').read_text().replace('RETAINED',str(retained.relative_to(ROOT)))
        paths += [retained,ROOT/'tests/basic_modules.vh']
    else:
        body=body.replace('test_basic("B81FIS",0,1)','test_basic("B81FIJ",0,1)')
        variants={'fis':'phase=3;test_basic("B81FIJ",0,1);',
                  'single':'phase=4;test_basic("B81FPU",0,0);',
                  'double':'phase=5;test_basic("B81FPD",1,0);'}
        if variant!='all':
            for name,call in variants.items():
                if name!=variant:
                    assert call in body,call
                    body=body.replace(call,'')
        body+='\n    always @(posedge clk) if(counting && odt_prompts) $fatal(1,"unexpected ODT during BASIC");\nendmodule\n'
    text=text+body
    text=text.replace('integer clocks=0,prompts=0','longint clocks=0; integer prompts=0')
    text=text.replace('if(window=="UJLOAD> ")','if(window[55:0]=="UJMOD> ")')
    # The existing UART monitor shifts window with a blocking assignment.
    # Detect complete suffixes in that monitor, avoiding ordering races.
    text=text.replace('previous_char<=dut.data[7:0];',
        'if(window[47:0]==48\'h52454144590d)ready_count<=ready_count+1;\n'
        '            if(window[47:0]=="DUAL)?")option_count<=option_count+1;\n'
        '            previous_char<=dut.data[7:0];')
    a=text.index('        if(dut.acknowledge && dut.writing',text.index('integer ready_count'))
    b=text.index('        if(counting',a)
    text=text[:a]+text[b:]
    text=text.replace('clocks>500000000',"clocks>64'd10000000000").replace('clocks%20000000','clocks%100000000').replace('CP64 progress','CP81 progress')
    test=out/'tb.v';test.write_text(text)
    od=json.loads((locations[0]/'result.json').read_text())
    fp=json.loads((locations[3]/'result.json').read_text())
    (out/'odt_symbols.vh').write_text(''.join(f'localparam integer {prefix}_{n}={v};\n' for prefix,sym in (('O',od['symbols']),('F',fp['symbols'])) for n,v in sym.items()))
    sources=[str(test.relative_to(ROOT))]+core+board+['rtl/uj11_rom.v','tests/models/ODDRXE.v','tests/models/spi_fram_model.v','tests/models/spi_sd_model.v']
    paths += [ROOT/p for p in sources]+[original,cases,Path(__file__),out/'odt_symbols.vh',out/'firmware.mem',HW/'inputs.json',HW/'m0.mem',HW/'decode.mem']
    manifest=dict(files={str(p.relative_to(ROOT)):sha(p) for p in paths},base_sha256=sha(base),image_sha256=sha(image),
                  recovery_window_overrides=overrides,configuration=configuration,variant=variant)
    (out/'inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
    cmd=['verilator','--binary','--timing','-Wno-WIDTH','--top-module','tb_rt11','-j','4','--Mdir',str(out/'obj'),'-I'+str(out)]+sources
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:
        result=subprocess.run([str(out/'obj/Vtb_rt11'),f'+SD_IMAGE={image}',f'+UART_LOG={out}/uart.txt'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-3000:]);result.check_returncode()
    assert sha(image)==manifest['image_sha256']
    for p,h in manifest['files'].items():assert sha(ROOT/p)==h,p
    counts=re.search(r'PASS (?:CP81 BASIC|CP82 modules): (\d+) checks, (\d+) clocks, (\d+) UART bytes',(out/'simulation.log').read_text());assert counts
    (out/'result.json').write_text(json.dumps(dict(passed=True,checks=int(counts[1]),clocks=int(counts[2]),uart_bytes=int(counts[3]),
        files={p:sha(out/p) for p in ('inputs.json','simulation.log','uart.txt','build.log')}),indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--configuration',action='store_true')
    p.add_argument('--variant',choices=['all','fis','single','double'],default='all')
    a=p.parse_args()
    if a.configuration and a.variant!='all':p.error('--variant is only for numerical BASIC tests')
    run(a.out.resolve(),a.configuration,a.variant)
