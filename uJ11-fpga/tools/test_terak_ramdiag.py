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


def run(out):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    hardware = build()
    if hardware['guest_ram_bytes'] != 248*1024:
        raise ValueError('RAM diagnostic requires UJ11_TERAK_RAM_KIB=256')
    source = ROOT/'tests/terak/ramdiag.asm'
    binary = out/'ramdiag.bin'
    assembler = os.environ.get('MICROASM11', str(ROOT.parent/'microasm11/microasm11'))
    subprocess.run([assembler, '-binary', str(source), str(binary)], check=True)
    program = binary.read_bytes()
    if len(program)>512:
        raise ValueError('RAM diagnostic exceeds its first boot sector')
    sys.path.insert(0, str(ROOT/'tests'))
    from test_terak_image import double_fixture
    _, tracks = decode_imd(double_fixture())
    imd = bytearray(b'IMD 1.18: TERAK 248K CPU/MMU RAM diagnostic\r\n\x1a')
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
        dut.address>=8192 && dut.address<253952)begin
        if(dut.writing)high_writes++;else high_reads++;
    end
    initial begin: boot_test
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);initial_reset=0;
        wait(dut.cpu_start);
        if(BOOT_BIOS)begin
            for(integer n=0;n<100;n++)begin
                #1000000;
                if(has_text("PRESS 1-4"))break;
            end
            if(!dut.bus.boot_menu || !has_text("SELECT BOOT"))$fatal(1,"RAM test missed primary BIOS");
            ps2_byte(8'h25); // Select TERAK4.CFG with the real keyboard path.
        end
        for(integer n=0;n<400;n++)begin
            #50000000;
            if(n%20==0)begin
                $display("RAMDIAG t=%0t PC=%o read=%0d write=%0d",$time,dut.cpu.pc,high_reads,high_writes);
                $fflush();
            end
            if(has_text("FAIL"))$fatal(1,"guest RAM diagnostic failed");
            if(has_text("248K PASS"))begin
                #20000000;
                if(high_reads<122880 || high_writes<122880)$fatal(1,"guest bypassed RAM banks");
                if(dut.bus.underruns || !white_pixels)$fatal(1,"diagnostic display not rendered");
                screen();
                if($value$plusargs("SCREEN_MEMORY=%s",snapshot))save_screen(snapshot);
                $display("SCREEN GAR=%0d VIR=%0d VCR=%0d",dut.bus.gar,dut.bus.vir,dut.bus.vcr);
                $display("PASS native TERAK RAM diagnostic: CPU/MMU verified all 30 nonresident 8 KiB banks");
                passed=1;break;
            end
        end
        if(!passed)$fatal(1,"RAM diagnostic timeout PC=%o",dut.cpu.pc);
        $finish;
    end
    initial begin #22000000000;$fatal(1,"boot timeout");end
endmodule
'''
    tb = out/'tb.v'
    tb.write_text(text)
    inputs = set(hardware['files']) | {str(source.relative_to(ROOT)), str(template.relative_to(ROOT)),
             'tools/test_terak_ramdiag.py', 'tests/test_terak_image.py', GUARD,
             'tests/models/async_sram_model.v', 'tests/models/spi_sd_model.v'}
    sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
    hashes = {p: sha(ROOT/p) for p in sorted(inputs)}
    command = ['verilator', '--binary', '--timing', '-Wno-WIDTH', '-Wno-TIMESCALEMOD', '-Wno-PINMISSING',
               '--top-module', 'tb_terak_boot', '-GDISPLAY_EBR=1', '-GRAM_BYTES=253952',
               f"-GBOOT_BIOS={int(hardware['boot_mode'] == 'bios')}",
               f'-GMICROCODE="{CPU_OUT.relative_to(ROOT)}/microcode.mem"', '-j', '4', '--Mdir', str(out/'obj')]
    command += CORE+BOARD+GENERATED+[str(tb.relative_to(ROOT)), GUARD,
                                    'tests/models/async_sram_model.v', 'tests/models/spi_sd_model.v']
    with (out/'build.log').open('w') as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    before = sha(image)
    with (out/'simulation.log').open('w') as log:
        rc = subprocess.run([str(out/'obj/Vtb_terak_boot')]+memory_args(hardware)+
                            [f'+SD_IMAGE={image}', f'+SCREEN_MEMORY={out}/screen.hex'],
                            cwd=ROOT, stdout=log, stderr=subprocess.STDOUT).returncode
    output = (out/'simulation.log').read_text()
    print(output[-4000:])
    if rc or 'PASS native TERAK RAM diagnostic' not in output:
        raise RuntimeError('RAM diagnostic failed; see '+str(out/'simulation.log'))
    assert sha(image)==before and all(sha(ROOT/p)==h for p,h in hashes.items())
    registers = re.search(r'SCREEN GAR=(\d+) VIR=(\d+) VCR=(\d+)', output)
    render(out/'screen.hex', *map(int, registers.groups()), out/'screen.png')
    (out/'result.json').write_text(json.dumps(dict(passed=True, hardware=hardware, files=hashes,
                resident_bank_excluded=True, guest_verified_bytes=240*1024,
                program_bytes=len(program), image_sha256=before, raw=str(raw), raw_sha256=sha(raw)),
                indent=2)+'\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT/'build/hc7000-terak-256k/ramdiag')
    run(parser.parse_args().out)
