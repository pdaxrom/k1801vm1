#!/usr/bin/env python3
"""Package the verified RT-11FB distribution and current uJ11 software for SD.

Writes a fresh output directory only; never opens a physical device. Directory
validation treats E.EOS as a one-word marker (including the final segment word),
not a file with a length. The source RK07 reserves its final 66 blocks.
"""
import argparse
from datetime import date
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from board_common import ROOT
from build_software import native

BASE = ROOT.parent/'lsi11-fpga/images/rt11v503.dsk'
BASE_SHA = 'e769228f2e1262220297bfa98b8f2841688849ab4c49ad9cd48d0d73d0a99553'
NAME = 'uj11-rt11fb.img'
RELEASE_DATE = '2026-09-26'
RAD50 = ' ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def rad50(word):
    if word >= 40**3:
        raise ValueError('invalid RAD50 name')
    return RAD50[word//1600]+RAD50[word//40 % 40]+RAD50[word % 40]


def directory(blob):
    """Validate the used directory chain, allocations and reserved disk tail."""
    assert len(blob) == 53790*512, 'expected the verified RK07 geometry'
    start = struct.unpack_from('<H', blob, 512+0o724)[0]
    assert start == 6
    segment = 1
    seen = set()
    files = {}
    free = []
    data_end = None
    total = None
    while segment:
        assert segment not in seen, 'directory loop'
        seen.add(segment)
        origin = (start+2*(segment-1))*512
        count, nxt, highest, extra, data = struct.unpack_from('<5H', blob, origin)
        assert count == 31 and extra == 0 and 0 <= nxt <= count
        assert 1 <= highest <= count and 1 <= segment <= count
        total = count
        if data_end is None:
            assert data == start+2*count
        else:
            assert data == data_end, 'gap/overlap between directory segments'
        offset = origin+10
        while offset+2 <= origin+1024:
            status = struct.unpack_from('<H', blob, offset)[0]
            if status & 0o4000:  # E.EOS has no length field.
                break
            assert offset+14 <= origin+1024, 'truncated directory entry'
            status, a, b, ext, blocks, job, stamp = struct.unpack_from('<7H', blob, offset)
            assert blocks > 0 and data+blocks <= 53724
            assert not status & 0o400, 'tentative file in distribution'
            if status & 0o2000:
                name = (rad50(a)+rad50(b)).rstrip()+'.'+rad50(ext).rstrip()
                assert re.fullmatch(r'[A-Z0-9$%.]{1,6}\.[A-Z0-9$%]{1,3}', name), name
                assert name not in files, name
                payload = blob[data*512:(data+blocks)*512]
                files[name] = dict(start_block=data, blocks=blocks, bytes=len(payload),
                                   sha256=sha(payload), date=stamp, entry_offset=offset)
            else:
                assert status & 0o1000, f'unknown entry status {status:o}'
                free.append((data, blocks))
            data += blocks
            offset += 14
        else:
            raise ValueError('missing directory end marker')
        data_end = data
        segment = nxt
    assert data_end == 53724, 'source RK07 reserves the final 66 blocks'
    return files, free, dict(allocated_directory_segments=total,
                            used_directory_segments=len(seen),
                            data_start_block=68, data_end_block=data_end,
                            reserved_tail_blocks=66, free_blocks=sum(n for _, n in free))


def text_bytes(path):
    text = path.read_text(encoding='ascii').replace('\r\n', '\n')
    assert '\r' not in text and '\0' not in text
    return text.replace('\n', '\r\n').encode('ascii')


def build(out, release_date=RELEASE_DATE):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    staging = out/'files'
    staging.mkdir()
    source = BASE.read_bytes()
    assert sha(source) == BASE_SHA, 'unrecognized base distribution'
    original, _, _ = directory(source)
    inputs = {str(BASE.relative_to(ROOT.parent)): BASE_SHA}
    additions = {}

    def add(path, data=None, metadata=None):
        assert re.fullmatch(r'[A-Z0-9]{1,6}\.[A-Z0-9]{1,3}', path.name), path
        assert path.name not in additions
        content = path.read_bytes() if data is None else data
        (staging/path.name).write_bytes(content)
        inputs[str(path.relative_to(ROOT))] = sha(path.read_bytes())
        additions[path.name] = dict(source=str(path.relative_to(ROOT)), bytes=len(content),
                                   sha256=sha(content), **(metadata or {}))

    for group in ('software', 'hg', 'basic'):
        folder = ROOT/'releases'/group
        manifest = folder/'manifest.json'
        inputs[str(manifest.relative_to(ROOT))] = sha(manifest.read_bytes())
        for name, info in json.loads(manifest.read_text())['files'].items():
            path = folder/name
            assert sha(path.read_bytes()) == info['sha256'], name
            add(path)
    brightness = ROOT/'releases/software/panel-brightness.json'
    inputs[str(brightness.relative_to(ROOT))] = sha(brightness.read_bytes())
    panel_release = json.loads(brightness.read_text())
    for name in ('DSPDEM', 'KEYDEM', 'RGBDEM'):
        path = ROOT/f'demos/rt11/panel/{name}.SAV'
        assert sha(path.read_bytes()) == panel_release['files'][str(path.relative_to(ROOT))]['sha256']
        add(path)
        for src in (ROOT/f'demos/rt11/panel/{name}.MAC', ROOT/'demos/rt11/panel/PNLDRV.MAC'):
            inputs[str(src.relative_to(ROOT))] = sha(src.read_bytes())
    for src in (ROOT/'demos/rt11/panel/TMRATE.MAC', ROOT/'tests/ODTCHK.MAC'):
        blob, _, assembly, compiled = native(src)
        add(compiled/(src.stem+'.SAV'), blob, {'assembly': assembly})
        inputs[str(src.relative_to(ROOT))] = sha(src.read_bytes())
    for path in sorted((ROOT/'demos/rt11/basic').glob('*.BAS')):
        add(path, text_bytes(path))
    for path in sorted((ROOT/'demos/rt11/sd').iterdir()):
        if path.suffix in ('.TXT', '.COM'):
            add(path, text_bytes(path))

    image = out/NAME
    image.write_bytes(source)
    rt = ROOT.parent/'lsi11/rt11tool'
    commands = []
    def tool(*args):
        result = subprocess.run([str(rt), *map(str, args)], check=True, capture_output=True, text=True)
        commands.append(dict(args=list(map(str, args)), stdout=result.stdout, stderr=result.stderr))
        return result.stdout
    tool('rm', image, 'STARTF.COM')
    for name in sorted(additions):
        tool('add', image, staging/name, name)

    # rt11tool stamps the wall date. Pin only newly packaged entries for a
    # reproducible release while preserving original DEC dates and boot files.
    stamp = date.fromisoformat(release_date)
    assert 1972 <= stamp.year <= 2035
    year = stamp.year-1972
    stamp_word = ((year//32)<<14) | (stamp.month<<10) | (stamp.day<<5) | (year % 32)
    blob = bytearray(image.read_bytes())
    entries, free, _ = directory(blob)
    for name in additions:
        struct.pack_into('<H', blob, entries[name]['entry_offset']+12, stamp_word)
    # Do not publish stale deleted data from the source image's free extents.
    for start, blocks in free:
        blob[start*512:(start+blocks)*512] = bytes(blocks*512)
    image.write_bytes(blob)
    entries, free, geometry = directory(blob)
    assert blob[:6*512] == source[:6*512], 'boot/home blocks must not change'
    assert blob[-66*512:] == source[-66*512:], 'reserved tail must not change'
    for name, info in original.items():
        if name != 'STARTF.COM':
            assert entries[name]['sha256'] == info['sha256'], name
            assert entries[name]['start_block'] == info['start_block'], name
    extracted = out/'extracted'
    extracted.mkdir()
    for name, info in additions.items():
        tool('extract', image, extracted, name)
        payload = (staging/name).read_bytes()
        padded = payload+bytes((-len(payload)) % 512)
        assert (extracted/name).read_bytes() == padded, name
        assert entries[name]['sha256'] == sha(padded), name
        info.update(image_sha256=sha(padded), blocks=len(padded)//512)

    compressed = out/(NAME+'.gz')
    with compressed.open('wb') as stream:
        with gzip.GzipFile(filename='', mode='wb', fileobj=stream, mtime=0, compresslevel=9) as gz:
            gz.write(blob)
    assert gzip.decompress(compressed.read_bytes()) == blob
    listing = tool('ls', image, '--long')
    (out/'directory.txt').write_text(''.join(line.rstrip()+'\n' for line in listing.splitlines()))
    (out/'image-info.txt').write_text(tool('info', image))
    inputs[str(Path(__file__).relative_to(ROOT))] = sha(Path(__file__).read_bytes())
    report = dict(release_date=release_date, monitor='RT-11FB V05.03', format='raw RK07 at SD LBA 0; no MBR/FAT',
                  image=dict(name=NAME, bytes=len(blob), sectors=len(blob)//512, sha256=sha(blob)),
                  compressed=dict(name=compressed.name, bytes=compressed.stat().st_size, sha256=sha(compressed.read_bytes())),
                  base_sha256=BASE_SHA, inputs=inputs, files=additions, directory=entries,
                  geometry=geometry, zeroed_free_extents=True,
                  verification=dict(directory_and_extents=True, extracted_files=True,
                                    preserved_distribution=True, preserved_boot_blocks=True,
                                    decompression=True))
    (out/'manifest.json').write_text(json.dumps(report, indent=2)+'\n')
    (out/'packaging.json').write_text(json.dumps(commands, indent=2)+'\n')
    (out/'SHA256SUMS').write_text(f'{sha(blob)}  {NAME}\n{sha(compressed.read_bytes())}  {compressed.name}\n')
    assert sha(BASE.read_bytes()) == BASE_SHA
    print(json.dumps({k: report[k] for k in ('image', 'compressed', 'geometry')}, indent=2))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT/'build/sd')
    parser.add_argument('--date', default=RELEASE_DATE, help='fixed release date YYYY-MM-DD')
    args = parser.parse_args()
    build(args.out, args.date)
