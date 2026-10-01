#!/usr/bin/env python3
"""HC7000-only RV32I firmware; verified build cache permits simulation on macOS."""
import argparse
import hashlib
import json
import os
import re
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
    return ['firmware/storage/'+p for p in ('start.S','main.c','bootstrap.c','label.c','label.h','link.ld','storage.h','controllers.c','rk05.c','mscp.c','terminal.c','terminal.h','terminal_font.h')]+['firmware/boot/SDIOP.MAC','tools/build_software.py','tools/build_iop_mmu.py','tools/iop_ebr.py','vendor/serv/rtl/serv_rf_top.v','vendor/serv/rtl/serv_rf_ram.v']

def flags():
    # Compressed instructions only for the partitioned HC7000 storage profile.
    if profile()=='legacy':return FLAGS[:]
    overrides={'-march=rv32i':'-march=rv32ic','-Wl,--no-relax':'-Wl,--relax',
               '-msmall-data-limit=0':'-msmall-data-limit=2048'}
    # PAL adds only CSR forwarding, no static data. Disabling the dominator
    # pass avoids code growth in GCC 13.2 and keeps the same 640-byte stack.
    # Video-off retains its released compiler flags.
    return [overrides.get(f,f) for f in FLAGS]+['-flto','-msave-restore','-fno-jump-tables','-fstack-usage']+(['-fno-tree-dominator-opts','-DUJ11_VIDEO_PAL'] if os.environ.get('UJ11_HC7000_VIDEO','0')=='1' else [])+(['-DUJ11_TERMINAL'] if os.environ.get('UJ11_HC7000_TERMINAL','0')=='1' else [])

def cpu_wrapper():
    storage=profile()=='storage'
    result= '''
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
''' % int(storage)
    if not storage:return result
    result=result.replace('serv_rf_top #','uj11_mmu_storage_rf_top #')
    top=(ROOT/'vendor/serv/rtl/serv_rf_top.v').read_text()
    top,count=re.subn(r'\bserv_rf_top\b','uj11_mmu_storage_rf_top',top)
    assert count>=1
    top,count=re.subn(r'\bserv_rf_ram\b','uj11_mmu_storage_rf_ram',top)
    assert count==1
    rf=(ROOT/'vendor/serv/rtl/serv_rf_ram.v').read_text()
    rf=rf.replace('module serv_rf_ram','module uj11_mmu_storage_rf_ram')
    rf,count=re.subn(r'(memory \[0:depth-1\])',r'\1 /* synthesis syn_ramstyle = "distributed" */',rf)
    assert count==1
    return result+'\n// HC7000 storage-only RF; upstream files and HC1200 stay unchanged.\n'+top+'\n'+rf+'\n`default_nettype wire\n'

def ram(words):
    shared=len(words)>512
    banks=9 if shared else 1
    assert len(words)==(4864 if shared else 512)
    lines=[f'// Generated {len(words)*4} byte RAM; {19 if shared else 2} EBRs.',
        'module uj11_mmu_iop_ram(input wire clk, reset, enable, input wire [12:0] address,',
        ' input wire [3:0] write_enable, input wire [31:0] write_data, output wire [31:0] data,',
        ' input wire sector_request, sector_write, input wire [7:0] sector_address,',
        ' input wire [15:0] sector_write_data, output wire [15:0] sector_read_data,',
        ' output wire storage_enabled, ready);',f"assign storage_enabled=1'b{int(shared)};"]
    if shared:
        lines += ["wire tail_selected=address[12:8]==5'd18;",
            '// First 18 KiB are full-width. SD owns only the upper half of the 16-bit tail.',
            'wire sector_access=sector_request && !reset;',
            'reg [2:0] phase;reg [15:0] low;reg [31:0] tail_data;',
            'wire cpu_tail=enable && tail_selected && !reset;',
            'wire tail_enable=sector_access || (cpu_tail && !sector_access && phase<3);',
            "wire [8:0] tail_address=sector_access ? {1'b1,sector_address} : {address[7:0],phase!=0};",
            'wire [15:0] tail_wd=sector_access ? sector_write_data : phase==0 ? write_data[15:0] : write_data[31:16];',
            "wire [1:0] tail_we=sector_access ? {2{sector_write}} : phase==0 ? write_enable[1:0] : write_enable[3:2];",
            'wire [15:0] tail_read;',
            'always @(posedge clk)begin',
            ' if(reset || !enable || !tail_selected)phase<=0;',
            ' else case(phase)',
            '  0:if(!sector_access)phase<=1;',
            '// Capture issued reads even when the engine takes the next RAM slot.',
            '  1:begin low<=tail_read;phase<=sector_access ? 2 : 3;end',
            '  2:if(!sector_access)phase<=3;',
            '  3:begin tail_data<={tail_read,low};phase<=4;end',
            '  default:begin end',
            ' endcase',
            'end',
            'assign ready=!reset && (!tail_selected || phase==4);',
            'reg sector_pending;reg [15:0] sector_value;',
            'always @(posedge clk)begin',
            ' if(reset)sector_pending<=0;',
            ' else begin',
            '  sector_pending<=sector_access && !sector_write;',
            '  if(sector_pending)sector_value<=tail_read;',
            ' end',
            'end',
            'assign sector_read_data=sector_value;',
            'reg [3:0] read_bank;',
            'always @(posedge clk)if(enable && ready && !(|write_enable))read_bank<=address[12:9];']
    else:
        lines += ["assign ready=!reset;",
            'uj11_sector_ram sector(.clk(clk),.write(sector_write && !reset),.address(sector_address),',
            ' .write_data(sector_write_data),.data(sector_read_data));']
    for bank in range(banks):
        tag=f'bank{bank}'
        lines += [f'wire {tag}_enable=enable && !reset'+(f" && address[12:9]==4'd{bank};" if shared else ';'),
            f'wire [8:0] {tag}_address=address[8:0];',
            f'wire [3:0] {tag}_we=write_enable;',f'wire [31:0] {tag}_wd=write_data;',
            f'wire [31:0] {tag}_data;']
    lines += ['`ifdef SYNTHESIS','`define UJ11_IOP_EBR',
        '`elsif UJ11_IOP_VENDOR_RAM','`define UJ11_IOP_EBR','`endif','`ifdef UJ11_IOP_EBR']
    for bank in range(banks):
        for half in range(2):
            lo,hi=16*half,16*half+15;tag=f'b{bank}h{half}'
            lines += [f'wire [15:0] {tag}_wd=bank{bank}_wd[{hi}:{lo}];',
                      f'wire [1:0] {tag}_we=bank{bank}_we[{2*half+1}:{2*half}];',
                      f'wire [15:0] {tag}_rd;assign bank{bank}_data[{hi}:{lo}]={tag}_rd;',
                      block(tag,f'bank{bank}_address',tag+'_wd',tag+'_we',tag+'_rd',
                            [(w>>lo)&65535 for w in words[bank*512:(bank+1)*512]])
                            .replace('enable &&',f'bank{bank}_enable &&')]
    if shared:
        lines += [block('tail','tail_address','tail_wd','tail_we','tail_read',
                        [half for w in words[4608:] for half in (w&65535,w>>16)])
                  .replace('enable &&','tail_enable &&')]
    lines += ['`else',f'reg [31:0] words[0:{len(words)-1}];',
        f'reg [31:0] bank_value[0:{banks-1}];',
        'initial $readmemh("build/hc7000-mmu-iop/firmware.mem",words);']
    for bank in range(banks):lines += [f'assign bank{bank}_data=bank_value[{bank}];']
    lines += ['always @(posedge clk)begin']
    for bank in range(banks):
        tag=f'bank{bank}';a=f'{bank*512}+{tag}_address'
        lines += [f' if({tag}_enable)begin',f'  if(!(|{tag}_we))bank_value[{bank}]<=words[{a}];']
        for lane in range(4):
            bits=f'{8*lane+7}:{8*lane}'
            lines += [f'  if({tag}_we[{lane}])words[{a}][{bits}]<={tag}_wd[{bits}];']
        lines += [' end']
    lines += ['end']
    if shared:
        lines += ['reg [15:0] tail_value;assign tail_read=tail_value;',
            'always @(posedge clk)if(tail_enable)begin',
            ' if(!(|tail_we))tail_value<=words[4608+tail_address[8:1]][16*tail_address[0]+:16];',
            ' if(tail_we[0])words[4608+tail_address[8:1]][16*tail_address[0]+:8]<=tail_wd[7:0];',
            ' if(tail_we[1])words[4608+tail_address[8:1]][16*tail_address[0]+8+:8]<=tail_wd[15:8];',
            'end']
    lines += ['`endif','wire [31:0] base_data='+('bank0_data;' if not shared else
        ''.join(f'read_bank=={b} ? bank{b}_data : ' for b in range(banks))+"32'b0;"),
        'assign data='+('tail_selected ? tail_data : base_data;' if shared else 'base_data;'),
        '`undef UJ11_IOP_EBR','endmodule','']
    return '\n'.join(lines)+cpu_wrapper()

def build(rebuild=False):
    mode=profile();ram_bytes=19456 if mode=='storage' else 2048
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
    if mode=='storage':
        from build_software import native
        raw,symbols,_,_=native(ROOT/'firmware/boot/SDIOP.MAC')
        boot=raw[0o4000:symbols['IMEND']]
        assert 0<len(boot)<=512 and len(boot)%2==0
        values=[int.from_bytes(boot[i:i+2],'little') for i in range(0,len(boot),2)]
        (OUT/'bootstrap.h').write_text('// Generated from firmware/boot/SDIOP.MAC.\n'
            'static const uint16_t j11_bootstrap[] = {\n'+
            ''.join('\t'+', '.join(f'0{v:06o}' for v in values[i:i+8])+',\n' for i in range(0,len(values),8))+'};\n')
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
    if mode=='storage':
        memory.update(cold_start=symbols['__cold_start'],cold_end=symbols['__cold_end'],
                      cold_limit=symbols['__cold_limit'],sector_start=symbols['__sector_start'],sector_end=symbols['__sector_end'])
        assert memory['stack_top']==memory['cold_start']==18432
        assert memory['cold_start']<=memory['cold_end']<=memory['cold_limit']==memory['sector_start']==ram_bytes-512
        assert memory['sector_end']==ram_bytes
    assert len(data)<=memory['stack_bottom']
    assert memory['bss_end']<=memory['stack_bottom']
    (OUT/'firmware.stack').write_text(''.join(p.read_text() for p in sorted(OUT.glob('firmware.elf.ltrans*.su'))))
    version=subprocess.check_output([compiler,'--version'],text=True).splitlines()[0]
    record=dict(inputs=inputs,flags=build_flags,compiler=version,program_bytes=len(data),ram_bytes=ram_bytes,ebr=ram_bytes//1024,profile=mode,
        memory=memory,outputs={p.name:sha(p) for p in OUT.iterdir() if p.name in
                 ('firmware.elf','firmware.bin','firmware.mem','bootstrap.h','firmware.map','firmware.lst','firmware.stack','uj11_mmu_iop_ram.v','uj11_sector_ram.v')})
    record_path.write_text(json.dumps(record,indent=2)+'\n')
    print(f'HC7000 MMU IOP: {len(data)} bytes, {build_flags[0]}, {ram_bytes} bytes RAM, {version}')
    return record

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rebuild',action='store_true')
    build(p.parse_args().rebuild)
