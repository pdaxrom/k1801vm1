#!/usr/bin/env python3
"""Boot an already packaged SD image in SIMH and optionally the full uJ11 RTL.

SIMH gets private writable copies. SPI SD uses the existing read-only image
backing plus RAM overlay. No release file or physical board is modified.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

from board_common import ROOT
from build_sd_image import sha, directory
from rt11_build import Console


def command(c, value):
    c.send(value+'\r')
    echo = c.expect(re.escape(value.encode())+rb'\r\n')
    return echo+c.expect(rb'\r\n\.')


def simh(image, out, cpu, programs):
    out.mkdir(parents=True)
    disk = out/'test.dsk'
    shutil.copyfile(image, disk)
    ini = out/'test.ini'
    config = f'set cpu {cpu}\nset cpu 64k\n'
    if cpu == '11/03':
        config += 'set cpu eis\nset cpu fis\n'
    ini.write_text(config+f'set clk 50hz\nset hk0 rk07\nattach hk0 {disk}\nboot hk0\n')
    docs = sorted(p.name for p in (ROOT/'demos/rt11/sd').glob('*.TXT'))
    counts = []
    with (out/'console.uart').open('wb') as log:
        c = Console(ini, log)
        try:
            c.expect(rb'RT-11FB')
            startup = c.expect(rb'FRAM modules are not changed by this disk\.')
            c.expect(rb'\r\n\.')
            assert b'?' not in startup, startup
            for name in docs:
                text = command(c, 'TYPE SY:'+name)
                assert (ROOT/'demos/rt11/sd'/name).read_bytes().splitlines()[0] in text, name
                assert b'?PIP' not in text, text
            text = command(c, 'DIR SY:')
            for name in ('UJMOD', 'UJBOOT', 'CLOCK', 'HGTIME', 'ODTCHK', 'TMRATE', 'FP11'):
                assert name.encode() in text, name
            for name in programs:
                c.send(f'RUN SY:{name}\r')
                c.expect(rb'OPTIONAL FUNCTIONS.*\?')
                c.send('A\r')
                c.expect(rb'READY\r\n')
                c.send('RUN SY:B81TST\r')
                text = c.expect(rb'READY\r\n', timeout=120)
                assert b'CP81 PASS' in text and re.search(rb'CHECKS=\s*29\s+ERRORS=\s*0', text), text
                counts.append(dict(program=name, comparisons=29, errors=0))
                if name == 'B81FPD':
                    c.send('RUN SY:B81DBL\r')
                    text = c.expect(rb'READY\r\n')
                    assert b'DOUBLE PASS' in text, text
                c.send('BYE\r')
                c.expect(rb'\r\n\.')
            # Exercise RT-11 directory allocation and a native round-trip on
            # the private test disk, not just the host directory parser.
            for cmd in ('COPY SY:README.TXT SY:CHKSD.TXT', 'TYPE SY:CHKSD.TXT',
                        'DELETE/NOQUERY SY:CHKSD.TXT'):
                text = command(c, cmd)
                assert b'?PIP' not in text and b'?KMON' not in text, text
        finally:
            c.close()
    entries, _, _ = directory(disk.read_bytes())
    assert 'CHKSD.TXT' not in entries
    return dict(passed=True, cpu=cpu, documents=len(docs), basic=counts,
                startup=True, hg_not_accessed=True, native_copy_delete=True,
                console_sha256=sha((out/'console.uart').read_bytes()))


def rtl(image, out):
    from build_hardware import adapt
    out.mkdir(parents=True)
    core, board = adapt()
    harness = ROOT/'tests/rt11_harness.vh'
    cases = ROOT/'tests/sd_image.vh'
    text = harness.read_text().replace('"UJLOAD> "', '"UJMOD> "')
    # Reuse the electrical harness; its unused ODT helpers need two symbols.
    text = text.replace('`include "odt_symbols.vh"', 'localparam O_SCREEN=0, O_KLAST=0;')
    text = text.replace('CP64 progress', 'SD image progress')
    text += cases.read_text()+'\nendmodule\n'
    test = out/'tb.v'
    test.write_text(text)
    sources = [str(test.relative_to(ROOT))]+core+board+[
        'rtl/uj11_rom.v', 'tests/models/ODDRXE.v',
        'tests/models/spi_fram_model.v', 'tests/models/spi_sd_model.v']
    files = {p: sha((ROOT/p).read_bytes()) for p in sources}
    for p in (harness, cases, ROOT/'build/hardware/inputs.json',
              ROOT/'build/hardware/m0.mem', ROOT/'build/hardware/decode.mem',
              ROOT/'build/hardware/firmware.mem'):
        files[str(p.relative_to(ROOT))] = sha(p.read_bytes())
    (out/'inputs.json').write_text(json.dumps(dict(image_sha256=sha(image.read_bytes()),
        files=files, recovery_window_overrides=[], initial_fram='zero; no installed modules'), indent=2)+'\n')
    cmd = ['verilator', '--binary', '--timing', '-Wno-WIDTH', '--top-module', 'tb_rt11',
           '-j', '4', '--Mdir', str(out/'obj')]+sources
    with (out/'build.log').open('w') as log:
        subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (out/'simulation.log').open('w') as log:
        subprocess.run([str(out/'obj/Vtb_rt11'), f'+SD_IMAGE={image}', f'+UART_LOG={out}/console.uart'],
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    for p, digest in files.items():
        assert sha((ROOT/p).read_bytes()) == digest, p
    text = (out/'simulation.log').read_text()
    match = re.search(r'PASS SD image: (\d+) checks, (\d+) clocks, (\d+) UART bytes', text)
    assert match, text[-2000:]
    return dict(passed=True, checks=int(match[1]), clocks=int(match[2]), uart_bytes=int(match[3]),
                production_rom=True, recovery_window_overrides=False,
                console_sha256=sha((out/'console.uart').read_bytes()),
                simulation_sha256=sha((out/'simulation.log').read_bytes()))


def run(image, out, full_rtl=False):
    out.mkdir(parents=True, exist_ok=False)
    digest = sha(image.read_bytes())
    report = dict(image_sha256=digest, simh=[],
                  runner_sha256=sha(Path(__file__).read_bytes()))
    for cpu, programs in (('11/73', ('B81FPU', 'B81FPD')), ('11/03', ('B81FIS', 'B81FIJ'))):
        result = simh(image, out/cpu.replace('/', '-'), cpu, programs)
        report['simh'].append(result)
        print('PASS SIMH', cpu, flush=True)
        (out/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    if full_rtl:
        report['rtl'] = rtl(image, out/'rtl')
        print('PASS full uJ11 SPI boot/CLOCK/BASIC', flush=True)
    assert sha(image.read_bytes()) == digest
    report['passed'] = True
    (out/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--rtl', action='store_true')
    args = parser.parse_args()
    run(args.image.resolve(), args.out.resolve(), args.rtl)
