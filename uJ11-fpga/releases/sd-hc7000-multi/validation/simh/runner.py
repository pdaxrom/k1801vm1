#!/usr/bin/env python3
"""Boot OS partitions extracted from a HC7000 suite; never modify the SD file."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import time

from board_common import ROOT
from rt11_build import Console
from sdcard import copy_bytes, read_label
from storage_menu import read as read_menu
from test_os_boot_simh import quiet


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(image, out):
    image = image.resolve()
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    image_hash = digest(image)
    media = out / 'media'
    media.mkdir()
    paths = {}
    partitions = []
    with image.open('rb') as stream:
        label, _ = read_label(stream)
        menu = read_menu(stream)
        for partition in label.partitions:
            path = media / f'{partition.kind}-{partition.unit}.dsk'
            stream.seek(partition.start * 512)
            with path.open('xb') as target:
                copy_bytes(stream, target, partition.blocks * 512)
            paths[partition.kind, partition.unit] = path
            partitions.append(dict(kind=partition.kind, unit=partition.unit,
                                   bytes=path.stat().st_size, sha256=digest(path)))
    simulator = Path(shutil.which('pdp11')).resolve()
    results = []
    cases = (
        ('rt11xm', '11/40', '256k', 'hk0',
         [('hk0', 2, 0), ('hk1', 2, 1), ('rk0', 1, 0),
          ('rk1', 1, 1), ('rk2', 1, 2), ('rk3', 1, 3)]),
        ('rt11v4', '11/40', '256k', 'rk0', [('rk0', 1, 0)]),
        ('rsx', '11/70', '2m', 'rq1', [('rq0', 4, 0), ('rq1', 4, 1)]),
        ('bsd', '11/70', '2m', 'rl0', [('rl0', 5, 0), ('rl1', 5, 1), ('rp0', 3, 0)]),
    )
    record = dict(passed=False, scope='SIMH reference on extracted private disks; not FPGA/PAL qualification',
                  image_sha256=image_hash, menu=menu, partitions=partitions,
                  simulator=str(simulator), simulator_sha256=digest(simulator),
                  driver_sha256=digest(Path(__file__)), cases=results)
    try:
        for name, cpu, memory, boot, attachments in cases:
            case = out / name
            case.mkdir()
            result = dict(case=name, cpu=cpu, memory=memory, fpp=False,
                          fis=cpu == '11/40', boot=boot, passed=False, commands=[])
            results.append(result)
            lines = [f'set cpu {cpu}', f'set cpu {memory}', 'set clk 50hz',
                     'set tti 7b', 'set tto 7b']
            lines.append('set cpu fis' if cpu == '11/40' else 'set cpu nofpp')
            active = {unit[:2] for unit, _, _ in attachments}
            for controller in ('hk', 'rk', 'rq', 'rl', 'rp', 'tm', 'tq', 'dz', 'lp', 'xq'):
                lines.append(f'set {controller} ' + ('enable' if controller in active else 'disable'))
            for unit, kind, number in attachments:
                path = case / f'{unit}.dsk'
                shutil.copyfile(paths[kind, number], path)
                geometry = {'hk': 'rk07', 'rq': 'rd54', 'rl': 'rl02', 'rp': 'rm05'}.get(unit[:2])
                if geometry:
                    lines.append(f'set {unit} {geometry}')
                lines.append(f'attach {unit} {path}')
            lines += ['show cpu', f'boot {boot}']
            ini = case / 'test.ini'
            ini.write_text('\n'.join(lines) + '\n')
            with (case / 'uart.txt').open('wb') as log:
                console = Console(ini, log)
                try:
                    def command(text, tokens=(), prompt=rb'\r\n\.'):
                        console.send(text + '\r')
                        console.expect(re.escape(text.encode()) + rb'\r+\n', 60)
                        response = console.expect(prompt, 90)
                        if not all(token in response for token in tokens):
                            raise AssertionError((text, response))
                        if re.search(rb'\?(?:MON|KMON|DIR|PIP|LINK)|panic:|Ovly err', response):
                            raise AssertionError((text, response))
                        result['commands'].append(dict(command=text,
                                                       output=response.decode('ascii', 'backslashreplace')))
                        return response

                    if name.startswith('rt11'):
                        banner = rb'RT-11XM[^\r\n]*V05\.03' if name == 'rt11xm' else rb'RT-11SJ[^\r\n]*V04\.00C'
                        response = console.expect(banner, 60)
                        response += console.expect(rb'\r\n\.', 60) + quiet(console)
                        result['boot_output'] = response.decode('ascii', 'backslashreplace')
                        if name == 'rt11xm':
                            command('SET SL OFF')
                            command('SHOW CONFIGURATION', [b'Booted from DM0:RT11XM'])
                            command('SHOW MEMORY', [b'MEMTOP'])
                            for text, tokens in (
                                ('DIR VOL:', [b'ADDER']), ('DIR BAS:BASIC.SAV', [b'BASIC']),
                                ('DIR PAS:XM.SAV', [b'XM']), ('DIR FOR:FORTRA.SAV', [b'FORTRA']),
                                ('DIR SY:HGX.SYS', [b'HGX']), ('DIR SY:HGTIME.SAV', [b'HGTIME']),
                                ('TYPE SY:SDHELP.TXT', [b'11M-PLUS', b'LIBEIS', b'@HGSYNC'])):
                                command(text, tokens)
                            console.send('RUN BAS:BASIC\r')
                            console.expect(rb'OPTIONAL FUNCTIONS[^\r\n]*\? ', 60)
                            console.send('A\r')
                            console.expect(rb'READY', 60)
                            command('PRINT 2+3', [b'5'], rb'READY')
                            command('BYE')
                            console.send('RUN PAS:XM\r')
                            console.expect(rb'\*', 60)
                            response = command('VOL:CTPAS,VOL:CTPAS=VOL:ADDER')
                            if b'error' in response.lower():
                                raise AssertionError(response)
                            console.send('R LINK\r')
                            console.expect(rb'\*', 60)
                            command('VOL:CTPAS,VOL:CTPAS=VOL:CTPAS,PAS:LIBEIS', prompt=rb'\*')
                            console.send('\x03')
                            console.expect(rb'\r\n\.', 60)
                            console.send('RUN VOL:CTPAS\r')
                            quiet(console)
                            command('2 3', [b'5.000000E+00'])
                        else:
                            command('DIR RK0:RT11*.SYS', [b'RT11SJ'])
                            command('TYPE V4USER.TXT', [b'Welcome to RT-11 Version 4'])
                    elif name == 'rsx':
                        response = console.expect(rb'Please enter time and date[^\n]*\[S\]: ', 90)
                        if b'RSX-11M-PLUS V4.6  BL87' not in response:
                            raise AssertionError(response)
                        result['boot_output'] = response.decode('ascii', 'backslashreplace')
                        console.send('12:00 02-OCT-1999\r')
                        response = console.expect(rb'(?:\r\n|\n\r)>', 90) + quiet(console, seconds=2)
                        result['startup_output'] = response.decode('ascii', 'backslashreplace')
                        prompt = rb'(?:\r\n|\n\r)>'
                        command('DEV DU:', [b'DU0:', b'DU1:'], prompt)
                        command('PIP DU1:[1,54]RSX11M.SYS/LI',
                                [b'RSX11M.SYS;1', b'Total of 1026./1026. blocks in 1. file'], prompt)
                    else:
                        console.expect(rb'70Boot[\s\x7f]*: ', 60)
                        console.send('rl(0,0)rlunix\r')
                        response = console.expect(rb'# ', 90)
                        if b'rl 0 csr 174400' not in response or b'xp 0 csr 176700' not in response:
                            raise AssertionError(response)
                        result['boot_output'] = response.decode('ascii', 'backslashreplace')
                        console.send('\x04')
                        response = console.expect(rb'login: ', 90)
                        if b'Mounted /usr' not in response:
                            raise AssertionError(response)
                        time.sleep(1)
                        console.send('root\r')
                        console.expect(rb'# ', 60)
                        command('stty nl0 cr0', prompt=rb'# ')
                        command('ls /usr', [b'bin'], rb'# ')
                        command('cat /etc/fstab', [b'/dev/rl1:swap'], rb'# ')
                        command('sync', prompt=rb'# ')
                    result['passed'] = True
                finally:
                    console.close()
            print(f'PASS {name}: {cpu}, FPP off' + (', FIS' if cpu == '11/40' else ''), flush=True)
        record['passed'] = True
    except Exception as error:
        record['error'] = repr(error)
        raise
    finally:
        record['image_unchanged'] = digest(image) == image_hash
        (out / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
        if not record['image_unchanged']:
            raise AssertionError('SD image changed')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    run(args.image, args.out)
