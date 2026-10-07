#!/usr/bin/env python3
"""Build native-console RT-11 V5.7 XM with DEC/Mentec MACRO/LINK/SYSGEN.

The downloaded distribution is immutable. Work uses private regular disk
images in SIMH; no physical devices or licensed sources enter the repository.
"""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import urllib.request
import zipfile
from pathlib import Path

from board_common import ROOT
from rt11_build import Console

URL = 'https://bitsavers.org/bits/DEC/pdp11/rt-11/Fine_RT-11_collection/RT11DV50.ISO.zip'
ARCHIVE_SHA256 = 'f054195e87e0398187578642053b0cd47096371b1f995ec34dfd88b4e612f746'
INPUTS = ('firmware/terak/rt11_tk.mac', 'tools/build_terak_rt11_xm.py', 'tools/rt11_build.py')
FILES = '''SWAP.SYS DL.SYS DLX.SYS VM.SYS VMX.SYS LD.SYS LDX.SYS NL.SYS NLX.SYS
PIP.SAV DUP.SAV DIR.SAV IND.SAV RESORC.SAV EDIT.SAV KED.SAV KEX.SAV MACRO.SAV LINK.SAV
LIBR.SAV CREF.SAV FILEX.SAV SRCCOM.SAV BINCOM.SAV SLP.SAV DUMP.SAV SIPP.SAV BUP.SAV PAT.SAV
HELP.SAV SYSMAC.SML SYSLIB.OBJ BATCH.SAV QUEMAN.SAV FORMAT.SAV SETUP.SAV DATIME.SAV LET.SAV
TECO.SAV ODT.OBJ VDT.OBJ V5USER.TXT V5NOTE.TXT CUSTOM.TXT VERIFY.COM IVP.COM IVP.MAC
DEMOBG.MAC DEMOFG.MAC DEMOM1.MAC'''.split()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rad50(value):
    chars = ' ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789'
    return ''.join(chars[(value//p)%40] for p in (1600, 40, 1))


def distribution(archive, out):
    if not archive.exists():
        archive.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(URL, timeout=60) as src, archive.open('xb') as dst:
            shutil.copyfileobj(src, dst)
    if sha(archive) != ARCHIVE_SHA256:
        raise ValueError('Distribution archive SHA256 differs from the recorded source')
    with zipfile.ZipFile(archive) as source:
        names = [n for n in source.namelist() if n.upper().endswith('.ISO')]
        if len(names) != 1:
            raise ValueError('Expected one RT11DV50 ISO')
        iso = source.read(names[0])
    # This CD is an RT-11 four-partition volume, not ISO9660. Partition
    # 2 starts at 2*65536 logical blocks; follow its RT-11 directory.
    base, segment, found, visited = 2*65536*512, 1, None, set()
    while segment:
        if segment in visited:
            raise ValueError('Cyclic distribution directory')
        visited.add(segment)
        data = iso[base+(6+2*(segment-1))*512:base+(8+2*(segment-1))*512]
        _, segment, _, extra, block = struct.unpack_from('<5H', data)
        for p in range(10, 1015, 14+extra):
            status, a, b, ext, count = struct.unpack_from('<5H', data, p)
            if status & 0o4000:
                break
            if status & 0o2000 and (rad50(a)+rad50(b)+'.'+rad50(ext)).rstrip() == 'RTV5RL.07':
                found = iso[base+block*512:base+(block+count)*512]
            block += count
    if found is None:
        raise ValueError('RTV5RL.07 is missing from the distribution')
    if hashlib.sha256(found).hexdigest() != 'a67b23edb812493ebfe37c3fefa8e94bea28c8cb65b500678731252778c44b73':
        raise ValueError('RTV5RL.07 source image differs from the recorded release')
    target = out/'RT11KIT.rl02'
    target.write_bytes(found.ljust(10485760, b'\0'))
    return target


def patch_console(source, out):
    tk = (ROOT/'firmware/terak/rt11_tk.mac').read_text()
    for name in ('RMON.MAC', 'BSTRAP.MAC'):
        text = (source/name).read_bytes().rstrip(b'\0').decode('latin1').replace('\r\n', '\n')
        if name == 'RMON.MAC':
            marker = '\f.SBTTL\tRMSTAK\t- System Stack'
            if text.count(marker) != 1:
                raise ValueError('RMON stack marker changed')
            text = text.replace(marker, tk+'\n'+marker)
        else:
            marker = '\t.WORD\t<V.DL11+4!...REL>,TTOINT,PR7\n'
            if text.count(marker) != 2:
                raise ValueError('Bootstrap vector lists changed')
            text = text.replace(marker, marker+'\t.WORD\t<164!...REL>,TKISR,PR7\n')
            # KMON clears unprotected vectors when starting a program. The
            # native TERAK output ISR must survive just like vectors 60/64.
            marker = 'CLNUP::\n'
            if text.count(marker) != 1:
                raise ValueError('Bootstrap console cleanup marker changed')
            text = text.replace(marker, marker+'\tMOV\t#164,R1\n\tCALL\t@#PROTEC\n')
        (out/name).write_bytes(text.replace('\n', '\r\n').encode('latin1'))


def build(archive, out):
    out = out.resolve()
    saved = out/'build.json'
    if saved.exists():
        record = json.loads(saved.read_text())
        products = {p.name:p for p in [out/'RT11XM57.rl01', out/'RT11KIT.rl02']+
                    list((out/'products').iterdir())}
        if record['inputs'] == {p:sha(ROOT/p) for p in INPUTS} and all(
                name in products and products[name].exists() and sha(products[name]) == value
                for name, value in record['output_sha256'].items()):
            print('Using verified RT-11 V5.7 XM build', flush=True)
            return record
        raise ValueError('RT-11 build inputs/outputs changed; choose a fresh --out directory')
    out.mkdir(parents=True, exist_ok=False)
    rt = ROOT.parent/'lsi11/rt11tool'
    def rttool(*args):
        subprocess.run([str(rt)]+[str(a) for a in args], check=True, capture_output=True)
    kit = distribution(archive, out)
    original = sha(kit)
    source, patched = out/'source', out/'patched'
    source.mkdir(); patched.mkdir()
    rttool('extract', kit, source)
    patch_console(source, patched)
    work = out/'sysgen.rl02'
    shutil.copyfile(kit, work)
    for p in patched.iterdir():
        rttool('rm', work, p.name, '--force')
        rttool('add', work, p, p.name)
    ini = out/'sysgen.ini'
    ini.write_text(f'''set cpu 11/73
set cpu 256K
set cpu nofpp
set clk 60hz
set tq disable
set rq disable
set hk disable
set rl0 rl02
attach rl0 {work}
boot rl0
''')
    with (out/'sysgen.log').open('wb') as log:
        c = Console(ini, log)
        try:
            c.expect(rb'RETURN" key when ready to continue\.', 60); c.send('\r')
            c.expect(rb'Type YES or NO.*key\): ', 60); c.send('NO\r')
            c.expect(rb'RETURN" key when ready to continue\.', 60); c.send('\r')
            c.expect(rb'RT-11FB\s+V05.07', 60)
            c.expect(rb'\r\n\.R MSCPCK', 60); c.expect(rb'\r\n\.', 60)
            print('RT-11 V5.7: generating XM configuration', flush=True)
            c.send('R IND\r'); c.expect(rb'\*')
            c.send('SYSGEN NOLIST NOQUERY XBXM.ANS\r'); reply = c.expect(rb'\*', 180)
            if b'?IND-' in reply:
                raise RuntimeError(reply.decode('ascii', 'replace'))
            c.send('\x03'); c.expect(rb'\r\n\.')
            for device in ('SRC', 'BIN', 'OBJ', 'MAP'):
                c.send(f'ASS DL0 {device}\r'); c.expect(rb'\r\n\.')
            print('RT-11 V5.7: MACRO, LINK and MONMRG', flush=True)
            c.send('$@XBXM.MON\r')
            reply = c.expect(rb'\.DEL BIN:RT11XB.TM\(1,2,3,4,5\)\r\n\r\n\.', 600)
            if re.search(rb'Errors detected:\s+[1-9]|\?(?:MACRO|LINK|MONMRG|KMON)-', reply):
                raise RuntimeError(reply.decode('ascii', 'replace'))
        finally:
            c.close()
    products = out/'products'; products.mkdir()
    for name in ('RT11XM.SYG', 'RT11XM.MAP', 'XBXM.CND', 'XBXM.TBL', 'XBXM.MON'):
        rttool('extract', work, products, name)
    system = out/'RT11XM57.rl01'
    rttool('mkfs', system, '--type', 'rl01', '--segments', '16', '--volid', 'TERAKXM57')
    for name in FILES:
        rttool('add', system, source/name, name)
    rttool('add', system, products/'RT11XM.SYG', 'RT11XM.SYS')
    startup = out/'STRTXM.COM'
    startup.write_bytes(b'SET TT NOSCOPE\r\nSHOW CONFIG\r\nSHOW MEMORY\r\n')
    rttool('add', system, startup, 'STRTXM.COM')
    # Use the actual RT-11 PIP to install its DL bootstrap and system blocks.
    ini.write_text(ini.read_text().replace('boot rl0', f'set rl1 rl01\nattach rl1 {system}\nboot rl0'))
    replies = []
    with (out/'install.log').open('wb') as log:
        c = Console(ini, log)
        try:
            c.expect(rb'\.R MSCPCK', 60); c.expect(rb'\r\n\.')
            c.send('COPY/BOOT DL1:RT11XM.SYS DL1:\r'); reply = c.expect(rb'\r\n\.')
            if b'?PIP-' in reply:
                raise RuntimeError(reply.decode('ascii', 'replace'))
            c.send('BOOT DL1:\r'); reply = c.expect(rb'\r\n\.\s*$', 60)
            c.expect(rb'SHOW MEMORY', 60); reply += c.expect(rb'\r\n\.\s*$', 60)
            if b'RT-11XM (S) V05.07' not in reply or b'MEMTOP' not in reply:
                raise RuntimeError(reply.decode('ascii', 'replace'))
            for cmd in ('DIR', 'SHOW DEVICES', 'COPY STRTXM.COM RLTEST.COM', 'TYPE RLTEST.COM', 'DELETE RLTEST.COM'):
                c.send(cmd+'\r'); text = c.expect(rb'\r\n\.\s*$', 60)
                if cmd.startswith('DELETE') and b'?' in text:
                    c.send('Y\r'); text += c.expect(rb'\r\n\.\s*$', 60)
                if re.search(rb'\?(?:PIP|KMON|DUP)-[EF]-', text):
                    raise RuntimeError(text.decode('ascii', 'replace'))
                replies.append(dict(command=cmd, output=text.decode('ascii', 'replace')))
        finally:
            c.close()
    # Native PIP leaves the ignored EOS length at its previous free-space
    # value. Normalize this directory bookkeeping for the host image tools.
    rttool('fsck', system, '--repair')
    rttool('fsck', system)
    if sha(kit) != original:
        raise AssertionError('Original distribution changed')
    record = dict(version='V5.7', monitor='XM', clock_hz=60, cpu='11/73', fpp=False,
                  source_url=URL, archive_sha256=ARCHIVE_SHA256, source_disk_sha256=original,
                  original_unchanged=True, commands=replies,
                  inputs={p:sha(ROOT/p) for p in INPUTS},
                  output_sha256={p.name:sha(p) for p in [system, kit]+list(products.iterdir())})
    (out/'build.json').write_text(json.dumps(record, indent=2)+'\n')
    print('PASS RT-11 V5.7 XM native console build, RL boot, DIR and read/write', flush=True)
    return record


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive', type=Path, default=ROOT/'build/terak-media/RT11DV50.ISO.zip')
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    build(args.archive.resolve(), args.out)
