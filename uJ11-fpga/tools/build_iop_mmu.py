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

OUT=ROOT/'build/hc7000-mmu-iop'
LEGACY_SOURCES=['firmware/iop/start.S','firmware/iop-mmu/rk.c','firmware/iop/link.ld',
         'tools/build_iop_mmu.py','tools/iop_ebr.py']
FLAGS=['-march=rv32i','-mabi=ilp32','-Os','-ffreestanding','-fno-builtin',
       '-fno-pic','-fno-stack-protector','-msmall-data-limit=0',
       '-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror',
       '-nostdlib','-nostartfiles','-Wl,--gc-sections','-Wl,--no-relax']

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def profile():
    mode=os.environ.get('UJ11_MMU_IOP','legacy')
    if mode not in ('legacy','storage'):raise ValueError('UJ11_MMU_IOP must be legacy or storage')
    return mode

def sources():
    if profile()=='legacy':return LEGACY_SOURCES
    return ['firmware/storage/'+p for p in ('start.S','main.c','label.c','label.h','link.ld','storage.h','controllers.c','rk05.c','mscp.c')]+['tools/build_iop_mmu.py','tools/iop_ebr.py']

def flags():
    # Compressed instructions only for the partitioned HC7000 storage profile.
    if profile()=='legacy':return FLAGS[:]
    overrides={'-march=rv32i':'-march=rv32ic','-Wl,--no-relax':'-Wl,--relax',
               '-msmall-data-limit=0':'-msmall-data-limit=2048'}
    return [overrides.get(f,f) for f in FLAGS]+['-flto','-msave-restore','-fno-jump-tables','-fstack-usage']

def cpu_wrapper():
    return '''
// Keep the firmware ISA and its hardware decoder in the same generated file.
module uj11_mmu_service_cpu(
 input wire clk,i_rst,i_timer_irq,
 output wire [31:0] o_ibus_adr,output wire o_ibus_cyc,
 input wire [31:0] i_ibus_rdt,input wire i_ibus_ack,
 output wire [31:0] o_dbus_adr,o_dbus_dat,output wire [3:0] o_dbus_sel,
 output wire o_dbus_we,o_dbus_cyc,input wire [31:0] i_dbus_rdt,input wire i_dbus_ack);
 serv_rf_top #(.WITH_CSR(0),.COMPRESSED(%d),.MDU(0),.PRE_REGISTER(1),.RESET_STRATEGY("MINI")) core(
 .clk(clk),.i_rst(i_rst),.i_timer_irq(i_timer_irq),
 .o_ibus_adr(o_ibus_adr),.o_ibus_cyc(o_ibus_cyc),.i_ibus_rdt(i_ibus_rdt),.i_ibus_ack(i_ibus_ack),
 .o_dbus_adr(o_dbus_adr),.o_dbus_dat(o_dbus_dat),.o_dbus_sel(o_dbus_sel),.o_dbus_we(o_dbus_we),.o_dbus_cyc(o_dbus_cyc),
 .i_dbus_rdt(i_dbus_rdt),.i_dbus_ack(i_dbus_ack),
 .o_ext_rs1(),.o_ext_rs2(),.o_ext_funct3(),.i_ext_rd(32'b0),.i_ext_ready(1'b0),.o_mdu_valid());
endmodule
''' % int(profile()=='storage')

def ram(words):
    banks=len(words)//512
    lines=[f'// Generated {len(words)*4} byte RV32I RAM, {banks*2} EBRs with byte enables.',
        'module uj11_mmu_iop_ram(input wire clk, enable, input wire [11:0] address,',
        ' input wire [3:0] write_enable, input wire [31:0] write_data, output wire [31:0] data,',
        ' output wire storage_enabled);',f"assign storage_enabled=1'b{int(banks>1)};",
        '`ifdef SYNTHESIS','`define UJ11_IOP_EBR',
        '`elsif UJ11_IOP_VENDOR_RAM','`define UJ11_IOP_EBR','`endif','`ifdef UJ11_IOP_EBR']
    if banks>1:
        lines+=['reg [2:0] read_bank;always @(posedge clk)if(enable && !(|write_enable))read_bank<=address[11:9];']
    for bank in range(banks):
        lines+=[f'wire bank{bank}_enable=enable'+(f" && address[11:9]==3'd{bank};" if banks>1 else ';'),
                f'wire [31:0] bank{bank}_data;','wire [8:0] bank_address=address[8:0];' if bank==0 else '']
        for half in range(2):
            lo,hi=16*half,16*half+15
            tag=f'b{bank}h{half}'
            lines += [f'wire [15:0] {tag}_wd=write_data[{hi}:{lo}];',
                      f'wire [1:0] {tag}_we=write_enable[{2*half+1}:{2*half}];',
                      f'wire [15:0] {tag}_rd;assign bank{bank}_data[{hi}:{lo}]={tag}_rd;',
                      block(tag,'bank_address',tag+'_wd',tag+'_we',tag+'_rd',
                            [(w>>lo)&65535 for w in words[bank*512:(bank+1)*512]])
                            .replace('enable &&',f'bank{bank}_enable &&')]
    lines+=['assign data='+('bank0_data;' if banks==1 else
        ''.join(f'read_bank=={b} ? bank{b}_data : ' for b in range(banks))+"32'b0;"),
        '`else',f'reg [31:0] words[0:{len(words)-1}];reg [31:0] value;',
        'wire ['+('8:0' if banks==1 else '11:0')+'] a=address;',
        'initial $readmemh("build/hc7000-mmu-iop/firmware.mem",words);',
        'always @(posedge clk) if(enable) begin',
        ' if(!(|write_enable))value<=words[a];']
    for lane in range(4):
        lines.append(f' if(write_enable[{lane}])words[a][{8*lane+7}:{8*lane}]<=write_data[{8*lane+7}:{8*lane}];')
    lines+=['end','assign data=value;','`endif','`undef UJ11_IOP_EBR','endmodule','']
    return '\n'.join(lines)+cpu_wrapper()

def build(rebuild=False):
    mode=profile();ram_bytes=12288 if mode=='storage' else 2048
    build_flags=flags()
    inputs={p:sha(ROOT/p) for p in sources()}
    compiler=os.environ.get('RISCV_CC','riscv64-unknown-elf-gcc')
    record_path=OUT/'build.json'
    if record_path.exists() and not rebuild:
        record=json.loads(record_path.read_text())
        if record['inputs']==inputs and record['flags']==build_flags and all(
                (OUT/p).exists() and sha(OUT/p)==h for p,h in record['outputs'].items()):
            print(f"HC7000 MMU IOP: verified firmware cache ({record['program_bytes']} bytes, {build_flags[0]})")
            return record
    if not shutil.which(compiler):
        raise RuntimeError('Install gcc-riscv64-unknown-elf/binutils-riscv64-unknown-elf or copy a source-matched build/hc7000-mmu-iop cache from Linux')
    OUT.mkdir(parents=True,exist_ok=True)
    prefix=os.environ.get('RISCV_PREFIX',compiler.removesuffix('gcc'))
    objects=[]
    # Stable object filenames keep the linker map reproducible too; direct
    # source-to-ELF linking embeds random /tmp/ccXXXX.o names in that report.
    for source in (p for p in sources() if p.endswith(('.S','.c'))):
        name=Path(source).name
        obj=OUT/(Path(name).stem+'.o');objects.append(str(obj))
        subprocess.run([compiler]+build_flags+['-c',str(ROOT/source),'-o',str(obj)],check=True,cwd=ROOT)
    cmd=[compiler]+build_flags+['-T',str(ROOT/next(p for p in sources() if p.endswith('.ld'))),
        '-Wl,-Map='+str(OUT/'firmware.map')]+objects+['-o',str(OUT/'firmware.elf'),'-lgcc']
    subprocess.run(cmd,check=True,cwd=ROOT)
    subprocess.run([prefix+'objcopy','-O','binary',str(OUT/'firmware.elf'),str(OUT/'firmware.bin')],check=True)
    data=(OUT/'firmware.bin').read_bytes()
    if len(data)>ram_bytes: raise ValueError('IOP program exceeds RAM')
    words=[int.from_bytes(data.ljust(ram_bytes,b'\0')[i:i+4],'little') for i in range(0,ram_bytes,4)]
    (OUT/'firmware.mem').write_text(''.join(f'{w:08x}\n' for w in words))
    (OUT/'uj11_mmu_iop_ram.v').write_text(ram(words))
    (OUT/'uj11_sector_ram.v').write_text(sector_ram())
    (OUT/'firmware.lst').write_bytes(subprocess.check_output([prefix+'objdump','-d',str(OUT/'firmware.elf')]))
    symbols={line.split()[2]:int(line.split()[0],16) for line in
             subprocess.check_output([prefix+'nm','--defined-only',str(OUT/'firmware.elf')],text=True).splitlines()
             if len(line.split())==3}
    memory=dict(bss_start=symbols.get('__bss_start',len(data)),bss_end=symbols.get('__bss_end',len(data)),
                stack_bottom=symbols.get('__stack_bottom',ram_bytes-512),stack_top=symbols.get('__stack_top',ram_bytes))
    assert len(data)<=memory['stack_bottom']
    assert memory['bss_end']<=memory['stack_bottom']
    (OUT/'firmware.stack').write_text(''.join(p.read_text() for p in sorted(OUT.glob('firmware.elf.ltrans*.su'))))
    version=subprocess.check_output([compiler,'--version'],text=True).splitlines()[0]
    record=dict(inputs=inputs,flags=build_flags,compiler=version,program_bytes=len(data),ram_bytes=ram_bytes,ebr=ram_bytes//1024,profile=mode,
        memory=memory,outputs={p.name:sha(p) for p in OUT.iterdir() if p.name in
                 ('firmware.elf','firmware.bin','firmware.mem','firmware.map','firmware.lst','firmware.stack','uj11_mmu_iop_ram.v','uj11_sector_ram.v')})
    record_path.write_text(json.dumps(record,indent=2)+'\n')
    print(f'HC7000 MMU IOP: {len(data)} bytes, {build_flags[0]}, {ram_bytes} bytes RAM, {version}')
    return record

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rebuild',action='store_true')
    build(p.parse_args().rebuild)
