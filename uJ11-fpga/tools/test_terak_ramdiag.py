#!/usr/bin/env python3
"""Build a bootable RAM diagnostic and run its real CPU/ROM/SD path."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from board_common import ROOT
from build_terak import build, CORE, BOARD, GENERATED, CPU_OUT, OUT
from render_terak_screen import render
from serv_test import GUARD, memory_args
from terak_image import decode_imd
from terak_mbr import create as mbr_card


def diagnostic_source(ram_bytes, wide):
    source = ROOT/'tests/terak/ramdiag.asm'
    font_source = ROOT/'firmware/storage/terminal_font.h'
    glyphs = re.findall(r'\{\s*((?:0x[0-9a-f]{2}[,\s]*){8})\}', font_source.read_text())
    if len(glyphs) != 96:
        raise ValueError('Expected the existing 96 printable VT52 glyphs')
    font = '\n'.join('        db '+str(ord(ch))+'.,0,'+
                     ','.join(re.findall(r'0x[0-9a-f]{2}', glyphs[ord(ch)-32]))+',0'
                     for ch in '0248KPASFIL')
    return (source.read_text().replace('@IO_PAR@', '177600' if wide else '7600')
            .replace('@MMR3@', '20' if wide else '0').replace('@BANKS@', str(ram_bytes//8192))
            .replace('@PASS_LABEL@', f'{ram_bytes//1024}K PASS').replace('; @FONT@', font))


def run(out, compiled=None):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    hardware = build()
    if hardware['memory_configuration_kib'] not in (256, 2048):
        raise ValueError('RAM diagnostic requires UJ11_TERAK_RAM_KIB=256 or 2048')
    ram_bytes = hardware['guest_ram_bytes']
    wide = hardware['physical_address_bits'] == 22
    banks = ram_bytes//8192
    verified_bytes = ram_bytes-8192
    pass_label = f'{ram_bytes//1024}K PASS'
    source = ROOT/'tests/terak/ramdiag.asm'
    generated = out/'ramdiag.asm'
    generated.write_text(diagnostic_source(ram_bytes, wide))
    binary = out/'ramdiag.bin'
    assembler = os.environ.get('MICROASM11', str(ROOT.parent/'microasm11/microasm11'))
    subprocess.run([assembler, '-binary', str(generated), str(binary)], check=True)
    program = binary.read_bytes()
    if len(program)>512:
        raise ValueError('RAM diagnostic exceeds its first boot sector')
    sys.path.insert(0, str(ROOT/'tests'))
    from test_terak_image import double_fixture
    _, tracks = decode_imd(double_fixture())
    imd = bytearray(f'IMD 1.18: TERAK {ram_bytes//1024}K CPU/MMU RAM diagnostic\r\n\x1a'.encode())
    for (c, h), (mfm, sectors) in sorted(tracks.items()):
        imd.extend(bytes([3 if mfm else 0, c, h, len(sectors), sectors[0][3]]))
        imd.extend(bytes(s[2] for s in sectors))
        for _, _, r, n, _, _ in sectors:
            payload = bytearray(128<<n)
            if (c, h, r) == (0, 0, 7):
                payload[71], payload[75] = 0xd4, 0xf2
            if (c, h, r) == (1, 0, 1):
                payload[:len(program)] = program
            imd.append(1)
            imd.extend(payload)
    disk = out/'ramdiag.IMD'
    disk.write_bytes(imd)
    image = out/'ramdiag-card.img'
    image.unlink(missing_ok=True)
    mbr_card([('terak-dsdd', disk)], image, [0], logical=True)
    card = json.loads(image.with_suffix('.json').read_text())
    extent = card['disks'][0]
    raw = out/'ramdiag.raw'
    with image.open('rb') as stream:
        stream.seek(extent['start']*512)
        raw.write_bytes(stream.read(1177344))
    if hardware['boot_mode'] == 'bios':
        sys.path.insert(0, str(ROOT.parent/'bk0010-01-fpga/tools'))
        from prepare_disk_sd import Fat16
        data = bytearray(image.read_bytes())
        volume = Fat16(data[2048*512:(2048+32768)*512])
        volume.put('TERAK4.CFG', card['config'].encode())
        volume.put('TBIOS.BIN', (OUT/'bios/TBIOS.BIN').read_bytes())
        data[2048*512:(2048+32768)*512] = volume.data
        image.write_bytes(data)
    template = ROOT/'tests/terak/tb_terak_boot.v'
    text = template.read_text()
    text = text[:text.index('    initial begin: boot_test')]+'''    integer high_reads=0,high_writes=0;
    always @(posedge clk)if(!reset && dut.request && dut.ready && dut.bus.ram_selected &&
        dut.address>=8192 && dut.address<@RAM_BYTES@)begin
        if(dut.writing)high_writes++;else high_reads++;
    end
    initial begin: boot_test
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);initial_reset=0;
        wait(dut.cpu_start);
        if(BOOT_BIOS)begin
            for(integer n=0;n<1000;n++)begin
                #1000000;
                if(has_text("DEL: ENTER BIOS SETUP") || has_text("ESC: CANCEL CHANGES"))break;
            end
            if(has_text("DEL: ENTER BIOS SETUP"))begin
                ps2_byte(8'he0);ps2_byte(8'h71);
                for(integer n=0;n<1000;n++)begin
                    #1000000;if(has_text("ESC: CANCEL CHANGES"))break;
                end
            end
            if(!dut.bus.boot_menu || !has_text("ESC: CANCEL CHANGES"))$fatal(1,"RAM test missed primary BIOS");
            ps2_byte(8'h25);#20000000;ps2_byte(8'h5a); // Select TERAK4.CFG with the real keyboard path.
        end
        for(integer n=0;n<400;n++)begin
            #50000000;
            if(n%20==0)begin
                $display("RAMDIAG t=%0t PC=%o read=%0d write=%0d",$time,dut.cpu.pc,high_reads,high_writes);
                $fflush();
            end
            if(has_text("FAIL"))$fatal(1,"guest RAM diagnostic failed");
            if(has_text("@PASS_LABEL@"))begin
                #20000000;
                if(high_reads<@VERIFIED_WORDS@ || high_writes<@VERIFIED_WORDS@)$fatal(1,"guest bypassed RAM banks");
                if(dut.bus.underruns || !white_pixels)$fatal(1,"diagnostic display not rendered");
                screen();
                if($value$plusargs("SCREEN_MEMORY=%s",snapshot))save_screen(snapshot);
                $display("SCREEN GAR=%0d VIR=%0d VCR=%0d",dut.bus.gar,dut.bus.vir,dut.bus.vcr);
                $display("PASS native TERAK RAM diagnostic: CPU/MMU verified all @NONRESIDENT_BANKS@ nonresident 8 KiB banks");
                passed=1;break;
            end
        end
        if(!passed)$fatal(1,"RAM diagnostic timeout PC=%o",dut.cpu.pc);
        $finish;
    end
    initial begin #22000000000;$fatal(1,"boot timeout");end
endmodule
'''
    text = (text.replace('@RAM_BYTES@', str(ram_bytes)).replace('@PASS_LABEL@', pass_label)
            .replace('@VERIFIED_WORDS@', str(verified_bytes//2))
            .replace('@NONRESIDENT_BANKS@', str(banks-1)))
    tb = out/'tb.v'
    tb.write_text(text)
    inputs = set(hardware['files']) | {str(source.relative_to(ROOT)), str(template.relative_to(ROOT)),
             'tools/test_terak_ramdiag.py', 'firmware/storage/terminal_font.h', 'tests/test_terak_image.py', GUARD,
             'tests/models/async_sram_model.v', 'tests/models/spi_sd_model.v'}
    sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
    hashes = {p: sha(ROOT/p) for p in sorted(inputs)}
    command = ['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD', '-Wno-PINMISSING',
               '--top-module', 'tb_terak_boot', '-GDISPLAY_EBR=1', f'-GRAM_BYTES={ram_bytes}',
               f"-GBOOT_BIOS={int(hardware['boot_mode'] == 'bios')}",
               f"-GRL_ENABLE={int(hardware['hard_disk'] == 'rl')}",
               f'-GMICROCODE="{CPU_OUT.relative_to(ROOT)}/microcode.mem"', '-j', '4', '--Mdir', str(out/'obj')]
    command += CORE+BOARD+GENERATED+[str(tb.relative_to(ROOT)), GUARD,
                                    'tests/models/async_sram_model.v', 'tests/models/spi_sd_model.v']
    executable = out/'obj/Vtb_terak_boot'
    if compiled is None:
        with (out/'build.log').open('w') as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    else:
        compiled = compiled.resolve()
        previous = json.loads((compiled/'result.json').read_text())
        if not previous['passed'] or previous['hardware'] != hardware or (compiled/'tb.v').read_bytes() != tb.read_bytes():
            raise ValueError('Compiled diagnostic must match the current hardware and testbench')
        for path in CORE+BOARD+GENERATED+[GUARD, 'tests/models/async_sram_model.v', 'tests/models/spi_sd_model.v']:
            if previous['files'][path] != sha(ROOT/path):
                raise ValueError('Compiled diagnostic source changed: '+path)
        executable = compiled/'obj/Vtb_terak_boot'
    before = sha(image)
    with (out/'simulation.log').open('w') as log:
        rc = subprocess.run([str(executable)]+memory_args(hardware)+
                            [f'+SD_IMAGE={image}', f'+SCREEN_MEMORY={out}/screen.hex'],
                            cwd=ROOT, stdout=log, stderr=subprocess.STDOUT).returncode
    output = (out/'simulation.log').read_text()
    print(output[-4000:])
    if rc or 'PASS native TERAK RAM diagnostic' not in output:
        raise RuntimeError('RAM diagnostic failed; see '+str(out/'simulation.log'))
    assert sha(image)==before and all(sha(ROOT/p)==h for p,h in hashes.items())
    registers = re.search(r'SCREEN GAR=(\d+) VIR=(\d+) VCR=(\d+)', output)
    snapshot_bytes, address = bytearray(73728), 0
    for line in (out/'screen.hex').read_text().splitlines():
        for token in line.split('//', 1)[0].split():
            if token.startswith('@'):
                address = int(token[1:], 16)
            else:
                snapshot_bytes[address] = int(token, 16)
                address += 1
    glyphs = program[-121:]
    for index in range(11):
        code, *rows = glyphs[index*11:(index+1)*11]
        if bytes(snapshot_bytes[69632+code*16:69632+code*16+10]) != bytes(rows):
            raise AssertionError('Guest diagnostic font mismatch: '+chr(code))
    render(out/'screen.hex', *map(int, registers.groups()), out/'screen.png')
    (out/'result.json').write_text(json.dumps(dict(passed=True, hardware=hardware, files=hashes,
                resident_bank_excluded=True, guest_verified_bytes=verified_bytes,
                program_bytes=len(program), image_sha256=before, raw=str(raw), raw_sha256=sha(raw),
                diagnostic_glyphs_verified=11, executable_sha256=sha(executable)),
                indent=2)+'\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=OUT/'ramdiag')
    parser.add_argument('--compiled', type=Path, help='Reuse an unchanged, previously qualified RTL testbench')
    args = parser.parse_args()
    run(args.out, args.compiled)
