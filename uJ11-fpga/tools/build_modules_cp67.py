#!/usr/bin/env python3
"""CP67 cold module-table checkpoint over frozen, placed CP63b. No microasm11."""
import argparse
import hashlib
import json
import re
import tarfile
from pathlib import Path
from board_common import ROOT, CORE, BOARD
from build_fram_cp52 import replace_once as rep
from make_ebr import lane_initvals
from rt11_build import build as assemble

OUT = ROOT / 'build/cp67-modules'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def firmware_rom(words):
    """Two 1024x9 EBRs, one byte per chip; no 16-bit output bank mux."""
    assert len(words) == 1024
    lines = ['// CP67: 1024x16 firmware, two byte-wide EBRs.',
             'module uj11_firmware_rom(input wire clk, enable,',
             ' input wire [9:0] address, output wire [15:0] data,',
             ' input wire [1:0] write_enable, input wire [15:0] write_data);',
             "wire [1:0] we=write_enable & {2{address[9:4]==6'b001111}};",
             '`ifdef SYNTHESIS', '`define UJ11_FW_EBR',
             '`elsif UJ11_VENDOR_ROM', '`define UJ11_FW_EBR', '`endif',
             '`ifdef UJ11_FW_EBR']
    for lane in range(2):
        values = [(w >> (8*lane)) & 255 for w in words]
        lines += ['DP8KC #(', ' .DATA_WIDTH_A(9), .DATA_WIDTH_B(9),',
                  ' .REGMODE_A("NOREG"), .REGMODE_B("NOREG"),',
                  ' .CSDECODE_A("0b000"), .CSDECODE_B("0b000"),',
                  ' .WRITEMODE_A("NORMAL"), .WRITEMODE_B("NORMAL"),',
                  ' .GSR("DISABLED"), .RESETMODE("SYNC"),',
                  ' .ASYNC_RESET_RELEASE("SYNC"), .INIT_DATA("STATIC"),']
        lines += [f' .INITVAL_{i:02X}("0x{v:080X}"){"," if i<31 else ""}'
                  for i, v in enumerate(lane_initvals(values, 0))]
        lines.append(f') fw{lane} (')
        ports = []
        for side in 'AB':
            ports += [(f'DI{side}{i}', f'write_data[{8*lane+i}]' if side=='A' and i<8 else "1'b0") for i in range(9)]
            ports += [(f'AD{side}{i}', f'address[{i-3}]' if i>=3 else ("1'b1" if i==0 else "1'b0")) for i in range(13)]
            ports += [(f'CE{side}', 'enable' if side=='A' else "1'b0"),
                      (f'OCE{side}', 'enable' if side=='A' else "1'b0"),
                      (f'CLK{side}', 'clk'), (f'WE{side}', f'we[{lane}]' if side=='A' else "1'b0"),
                      (f'RST{side}', "1'b0")]
            ports += [(f'CS{side}{i}', "1'b0") for i in range(3)]
            ports += [(f'DO{side}{i}', f'data[{8*lane+i}]' if side=='A' and i<8 else '') for i in range(9)]
        lines += [f' .{n}({v}){"," if i<len(ports)-1 else ""}' for i,(n,v) in enumerate(ports)]
        lines.append(');')
    lines += ['`else', 'reg [15:0] words[0:1023]; reg [15:0] value;',
              'initial $readmemh("build/cp67-modules/firmware.mem",words);',
              'always @(posedge clk) if(enable) begin',
              ' value<=words[address];',
              ' if(we[0])words[address][7:0]<=write_data[7:0];',
              ' if(we[1])words[address][15:8]<=write_data[15:8];',
              'end', 'assign data=value;', '`endif', '`undef UJ11_FW_EBR', 'endmodule', '']
    return '\n'.join(lines)


def build(asm=None):
    source = ROOT/'firmware/cp67/BOOT.MAC'
    asm = asm or OUT/('assembly-'+sha(source.read_bytes())[:12])
    if not (asm/'build-inputs.json').exists():
        assemble([source], asm, ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    meta = json.loads((asm/'build-inputs.json').read_text())
    assert sha(source.read_bytes()) in meta['source_sha256'].values()
    for name, digest in meta['outputs'].items():
        assert sha((asm/name).read_bytes()) == digest, name
    listing = (asm/'BOOT.LST').read_text(errors='replace')
    symbols = {n:int(v,8) for n,v in re.findall(r'\b([A-Z][A-Z0-9]{0,5})\s*=?\s+([0-7]{6})(?![0-9R])',listing)}
    blob = (asm/'BOOT.SAV').read_bytes()
    walker = blob[symbols['MSTART']:symbols['MEND']]
    entry = blob[symbols['CSTART']:symbols['CEND']]
    assert len(entry)<=32 and len(walker)<=1024-32
    assert symbols['MSTART']==0o5000 and symbols['MEND']<=0o6000
    manifest = json.loads((ROOT/'synth/reports/cp63b/inputs.json').read_text())
    frozen = {}
    with tarfile.open(ROOT/'synth/reports/cp63b/source.tgz') as archive:
        for name,digest in manifest['files'].items():
            if name.startswith('generated:'): continue
            data=archive.extractfile(name).read()
            assert sha(data)==digest,name
            frozen[name]=data
    prefix='build/cp63-debug/'
    old=[int(w,16) for w in frozen[prefix+'firmware.mem'].decode().split()]
    words=old+[0]*512
    # Install the walker before the old, verified resident initialization.
    jumps=[i for i in range(213,239) if words[i:i+2]==[0o137,0o322]]
    assert len(jumps)==1
    words[jumps[0]+1]=0o12000
    calls=[i for i in range(430,509) if words[i:i+3]==[0o12737,0o312,4]]
    assert len(calls)==1
    words[calls[0]:calls[0]+3]=[0o4737,0o5000,0o240]
    for start,data in ((1024,entry),(1056,walker)):
        for i in range(0,len(data),2): words[(start+i)//2]=int.from_bytes(data[i:i+2],'little')
    outputs=[]
    def put(name,data):
        p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True)
        p.write_bytes(data.encode() if isinstance(data,str) else data)
        outputs.append(str(p.relative_to(ROOT)))
    for name in ('m0.mem','m0.lst','m0.labels.json','m0.stats.json','service.uasm','uj11_m0_ebr.v','decode.mem'):
        put(name,frozen[prefix+name].replace(prefix.encode(),b'build/cp67-modules/'))
    put('firmware.mem',''.join(f'{w:04x}\n' for w in words))
    put('boot-symbols.json',json.dumps(symbols,indent=2)+'\n')
    for name in CORE+BOARD+['boards/hc1200/uj11_button.v','boards/hc1200/uj11_microcomp.v']:
        text=frozen[prefix+'src/'+name].decode().replace(prefix,'build/cp67-modules/')
        if name=='boards/hc1200/uj11_board_bus.v':
            text=rep(text,"word_address[15:10]==6'd4;", "word_address[15:11]==5'd2;")
            text=rep(text,'output wire [8:0]  boot_rom_addr', 'output wire [9:0]  boot_rom_addr')
            text=rep(text,"{5'b01111,word_address[4:1]}","{6'b001111,word_address[4:1]}")
            text=rep(text,'{service_program_selected || (bank && word_address[9]),word_address[8:1]};',
                     '{bank && word_address[10],service_program_selected || (bank && word_address[9]),word_address[8:1]};')
            text=rep(text,'\t\tend else begin\n\t\t\tif (SD_BOOT_ENABLE && sd_ready && write &&',
                     "\t\tend else begin\n\t\t\t// CP67: cold walker runs from FRAM before releasing HALT ROM.\n\t\t\tif (bank && !physical && request && write && word_address==16'o76)\n\t\t\t\tboot_overlay_active <= 0;\n\t\t\tif (SD_BOOT_ENABLE && sd_ready && write &&")
        if name=='boards/hc1200/uj11_board.v':
            text=rep(text,'wire [8:0] rom_address','wire [9:0] rom_address')
        if name=='microcode/generated/uj11_firmware_rom.v': text=firmware_rom(words)
        put('src/'+name,text)
    inputs=['tools/build_modules_cp67.py','firmware/cp67/BOOT.MAC','tools/rt11_build.py',
            'tools/board_common.py','tools/make_ebr.py','tools/build_fram_cp52.py',
            'synth/reports/cp63b/inputs.json','synth/reports/cp63b/source.tgz']
    record=dict(reference='cp63b',mmu=False,microcode_changed=False,expected_ebr=7,
                walker_bytes=len(walker),entry_bytes=len(entry),firmware_words=1024,
                assembler=meta,
                inputs={p:sha((ROOT/p).read_bytes()) for p in inputs},
                outputs={p:sha((ROOT/p).read_bytes()) for p in outputs})
    (OUT/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def adapt():
    build()
    return ([str((OUT/'src'/p).relative_to(ROOT)) for p in CORE],
            [str((OUT/'src'/p).relative_to(ROOT)) for p in BOARD+['boards/hc1200/uj11_button.v']])


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assembly',type=Path)
    print(json.dumps(build(p.parse_args().assembly),indent=2))
