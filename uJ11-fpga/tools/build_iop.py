#!/usr/bin/env python3
"""HC7000-only RV32I firmware; verified build cache permits simulation on macOS."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from board_common import ROOT
from iop_ebr import block, sector_ram

OUT=ROOT/'build/hc7000-iop'
SOURCES=['firmware/iop/start.S','firmware/iop/rk.c','firmware/iop/link.ld',
         'tools/build_iop.py','tools/iop_ebr.py']
FLAGS=['-march=rv32i','-mabi=ilp32','-Os','-ffreestanding','-fno-builtin',
       '-fno-pic','-fno-stack-protector','-msmall-data-limit=0',
       '-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror',
       '-nostdlib','-nostartfiles','-Wl,--gc-sections','-Wl,--no-relax']

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def ram(words):
    lines=['// Generated 2 KiB RV32I RAM, two 512x18 EBRs with byte enables.',
        'module uj11_iop_ram(input wire clk, enable, input wire [8:0] address,',
        ' input wire [3:0] write_enable, input wire [31:0] write_data, output wire [31:0] data);',
        '`ifdef SYNTHESIS','`define UJ11_IOP_EBR',
        '`elsif UJ11_IOP_VENDOR_RAM','`define UJ11_IOP_EBR','`endif','`ifdef UJ11_IOP_EBR']
    for half in range(2):
        lo,hi=16*half,16*half+15
        lines += [f'wire [15:0] wd{half}=write_data[{hi}:{lo}];',
                  f'wire [1:0] we{half}=write_enable[{2*half+1}:{2*half}];',
                  f'wire [15:0] rd{half};assign data[{hi}:{lo}]=rd{half};',
                  block(f'half{half}','address',f'wd{half}',f'we{half}',f'rd{half}',
                        [(w>>lo)&65535 for w in words])]
    lines+=['`else','reg [31:0] words[0:511];reg [31:0] value;',
        'initial $readmemh("build/hc7000-iop/firmware.mem",words);',
        'always @(posedge clk) if(enable) begin',
        ' if(!(|write_enable))value<=words[address];']
    for lane in range(4):
        lines.append(f' if(write_enable[{lane}])words[address][{8*lane+7}:{8*lane}]<=write_data[{8*lane+7}:{8*lane}];')
    lines+=['end','assign data=value;','`endif','`undef UJ11_IOP_EBR','endmodule','']
    return '\n'.join(lines)

def build(rebuild=False):
    inputs={p:sha(ROOT/p) for p in SOURCES}
    compiler=os.environ.get('RISCV_CC','riscv64-unknown-elf-gcc')
    record_path=OUT/'build.json'
    if record_path.exists() and not rebuild:
        record=json.loads(record_path.read_text())
        if record['inputs']==inputs and record['flags']==FLAGS and all(
                (OUT/p).exists() and sha(OUT/p)==h for p,h in record['outputs'].items()):
            print(f"HC7000 IOP: verified firmware cache ({record['program_bytes']} bytes RV32I)")
            return record
    if not shutil.which(compiler):
        raise RuntimeError('Install gcc-riscv64-unknown-elf/binutils-riscv64-unknown-elf or copy a source-matched build/hc7000-iop cache from Linux')
    OUT.mkdir(parents=True,exist_ok=True)
    prefix=os.environ.get('RISCV_PREFIX',compiler.removesuffix('gcc'))
    objects=[]
    # Stable object filenames keep the linker map reproducible too; direct
    # source-to-ELF linking embeds random /tmp/ccXXXX.o names in that report.
    for name in ('start.S','rk.c'):
        obj=OUT/(Path(name).stem+'.o');objects.append(str(obj))
        subprocess.run([compiler]+FLAGS+['-c',str(ROOT/'firmware/iop'/name),'-o',str(obj)],check=True,cwd=ROOT)
    cmd=[compiler]+FLAGS+['-T',str(ROOT/'firmware/iop/link.ld'),
        '-Wl,-Map='+str(OUT/'firmware.map')]+objects+['-o',str(OUT/'firmware.elf'),'-lgcc']
    subprocess.run(cmd,check=True,cwd=ROOT)
    subprocess.run([prefix+'objcopy','-O','binary',str(OUT/'firmware.elf'),str(OUT/'firmware.bin')],check=True)
    data=(OUT/'firmware.bin').read_bytes()
    if len(data)>1536: raise ValueError('IOP program overlaps stack')
    words=[int.from_bytes(data.ljust(2048,b'\0')[i:i+4],'little') for i in range(0,2048,4)]
    (OUT/'firmware.mem').write_text(''.join(f'{w:08x}\n' for w in words))
    (OUT/'uj11_iop_ram.v').write_text(ram(words))
    (OUT/'uj11_sector_ram.v').write_text(sector_ram())
    (OUT/'firmware.lst').write_bytes(subprocess.check_output([prefix+'objdump','-d',str(OUT/'firmware.elf')]))
    version=subprocess.check_output([compiler,'--version'],text=True).splitlines()[0]
    record=dict(inputs=inputs,flags=FLAGS,compiler=version,program_bytes=len(data),ram_bytes=2048,
        outputs={p.name:sha(p) for p in OUT.iterdir() if p.name in
                 ('firmware.elf','firmware.bin','firmware.mem','firmware.map','firmware.lst','uj11_iop_ram.v','uj11_sector_ram.v')})
    record_path.write_text(json.dumps(record,indent=2)+'\n')
    print(f'HC7000 IOP: {len(data)} bytes RV32I, 2048 bytes RAM, {version}')
    return record

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rebuild',action='store_true')
    build(p.parse_args().rebuild)
