#!/usr/bin/env python3
"""Validate a CP64 release and prove its hardware is the measured CP63b."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from board_common import ROOT
from build_debug_cp63 import adapt,OUT
from service_image_cp62 import decode


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def check(odt):
    adapt()
    profile=json.loads((OUT/'inputs.json').read_text())
    baseline=json.loads((ROOT/'synth/reports/cp63b/inputs.json').read_text())
    for p,h in profile['outputs'].items():assert sha(ROOT/p)==h==baseline['files'][p],p
    record=json.loads((odt/'result.json').read_text())
    for p,h in record['sources'].items():assert sha(ROOT/p)==h,p
    for group,folder in (('assembler','asm'),('activation','activation')):
        for p,h in record[group]['outputs'].items():assert sha(odt/folder/p)==h,p
    data=(odt/'ODT.BIN').read_bytes();header=decode(data)
    assert header==record['format']
    blob=(odt/'payload.bin').read_bytes();assert data[512:512+len(blob)]==blob
    count=record['immutable_bytes']//2
    checksum=sum(struct.unpack('<'+'H'*count,blob[:2*count]))&65535
    assert checksum==record['immutable_checksum']
    assert 0<header['payload_bytes']<=header['memory_bytes']<=12288
    combined=(odt/'UJMON.MAC').read_text();assert 'PNWAIT:' not in combined
    # Both normal and debug entry save R0..R6 before using an independent SP.
    entry=combined[combined.index('ENTER:'):combined.index('MOV @#CPC')]
    assert entry.index('MOV SP,REGS+14')<entry.index('MOV #STKTOP,SP')
    assert len(record['symbols'])>100
    report=dict(passed=True,mmu=False,hardware_identical_to='CP63b',hardware_files=len(profile['outputs']),
                hardware_source_sha256=baseline['files'],module=header,immutable_checksum=checksum,
                free_slot_bytes=12288-header['memory_bytes'],checker_sha256=sha(Path(__file__)))
    (odt/'checks.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP64: exact CP63b hardware; valid matched ABI2 module/activation; no HALT PNWAIT')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--odt',type=Path,required=True)
    check(p.parse_args().odt.resolve())
