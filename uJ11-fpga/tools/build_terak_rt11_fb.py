#!/usr/bin/env python3
"""Build a separate FB floppy with the original RT-11 PIP, through the guest CPU.

The source disk and simulated SD backing file remain read-only. Only writes
inside the target clone are exported; no guest monitor/bootstrap is host-patched.
"""
import argparse
import hashlib
import json
from pathlib import Path
from test_terak_rt11 import run
from terak_mbr import payload
from terak_image import decode_imd


def build(source, out):
    # The V3B reserved cylinder has deleted marks. None may occur in the
    # filesystem area used by the guest or be changed by this preparation.
    _, tracks = decode_imd(source.read_bytes())
    if any(s[4] != 1 for (c, _), (_, sectors) in tracks.items() if c for s in sectors):
        raise ValueError('FB preparation expects normal filesystem sectors')
    record = run([source, source], out, mechanical_speedup=100, build_fb=True,
                 profile='sssd', discard_status=True)
    target = record['card']['disks'][1]
    raw = bytearray(payload(source, 'sssd', discard_status=True))
    seen = set()
    for line in (out/'overlay.hex').read_text().splitlines():
        address, encoded = line.split()
        lba, sector = int(address, 16), bytes.fromhex(encoded)
        if lba in seen or not target['start'] <= lba < target['start']+target['blocks'] or len(sector) != 512:
            raise ValueError('Invalid/duplicate overlay sector or write outside the FB clone')
        offset = (lba-target['start'])*512
        end = min(offset+512, len(raw))
        if offset >= end:
            raise ValueError('Overlay sector falls outside the floppy data')
        # The final SD sector includes padding outside the physical floppy.
        raw[offset:end] = sector[:end-offset]
        seen.add(lba)
    if not seen:
        raise ValueError('The guest did not write the target floppy')
    original_raw = payload(source, 'sssd', discard_status=True)
    if raw[:3328] != original_raw[:3328]:
        raise ValueError('The guest changed the reserved cylinder')
    data = bytearray(payload(source, 'fd'))
    for cylinder in range(77):
        start = (2+cylinder*33)*512
        data[start:start+3328] = raw[cylinder*3328:(cylinder+1)*3328]
    disk = out/'RT11FB.fd'
    disk.write_bytes(data)
    (out/'RT11FB.raw').write_bytes(raw)
    payload(disk, 'fd')
    proof = dict(source=str(source.resolve()), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                 image_sha256=hashlib.sha256(data).hexdigest(), raw_sha256=hashlib.sha256(raw).hexdigest(),
                 overlay_sectors=len(seen), model_discard_status=True, original_sector_flags_preserved=True,
                 commands=['DELETE DK1:QXMNSJ.SYS/SYS', 'DELETE DK1:QXMNSJ.SGM', 'DELETE DK1:QXMNFB.FGM',
                           'COPY QXMNFB.FTM DK1:QXMNFB.SYS/SYS', 'COPY/BOOT DK1:QXMNFB.SYS DK1:', 'DIR DK1:'],
                 preparation_result=str((out/'result.json').resolve()))
    (out/'disk.json').write_text(json.dumps(proof, indent=2)+'\n')
    return disk


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Original TERAK V3B Std Monitors IMD')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(build(args.source.resolve(), args.out.resolve()))
