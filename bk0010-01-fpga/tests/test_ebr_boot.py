"""Reject damaged extended-partition chains before any operating ROM is loaded."""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from make_sd import fat16


def run_checks(executable, run):
    image = bytearray(49152*512)
    image[510:512] = b'\x55\xaa'
    for index,kind,start,length in ((0,6,2048,32768),(1,0xda,34816,1600),
                                    (2,0xda,36864,1600),(3,0x0f,40960,7744)):
        struct.pack_into('<B3sB3sII',image,446+index*16,0,b'\xfe\xff\xff',kind,b'\xfe\xff\xff',start,length)
    image[2048*512:34816*512] = fat16({'SENTINEL.TXT':b'EBR rejection test'})
    first,second = 40960*512,45056*512
    for offset in (first,second):
        image[offset+510:offset+512] = b'\x55\xaa'
        struct.pack_into('<B3sB3sII',image,offset+446,0,b'\xfe\xff\xff',0xda,b'\xfe\xff\xff',2048,1600)
    struct.pack_into('<B3sB3sII',image,first+462,0,b'\xfe\xff\xff',0x0f,b'\xfe\xff\xff',4096,3648)
    cases = [('signature',first+510,b'\0'),('second-signature',second+510,b'\0'),
             ('zero-offset',first+454,struct.pack('<I',0)),
             ('outside',first+458,struct.pack('<I',8000)),
             ('overflow',first+454,struct.pack('<I',0xffffffff)),
             ('overlap-link',first+470,struct.pack('<I',2048)),
             ('link-type',first+466,b'\xda'),('extra-entry',first+478,b'\x80'),
             ('boot-flag',first+446,b'\x01'),
             ('backward-second-link',second+462,image[first+462:first+478])]
    shared = bytearray(image)
    shared.extend(bytes(57344*512-len(shared)))
    struct.pack_into('<I',shared,506,14837)
    struct.pack_into('<B3sB3sII',shared,second+462,0,b'\xfe\xff\xff',0x0f,b'\xfe\xff\xff',8192,6645)
    for label,offset,value in (
        ('second-link-flag',second+462,b'\x80'),
        ('second-link-type',second+466,b'\xda'),
        ('second-link-overlap',second+470,struct.pack('<I',4096)),
        ('second-link-empty',second+474,struct.pack('<I',0)),
        ('second-link-outside',second+474,struct.pack('<I',7000))):
        changed = bytearray(shared)
        changed[offset:offset+len(value)] = value
        path = ROOT/f'build/ebr-bad-{label}.img'
        path.write_bytes(changed)
        run(f'ebr-bad-{label}',[executable,f'+image={path}','+error=11','+no-dump'])
        assert path.read_bytes() == changed, 'Rejected EBR test modified its media'
    for label,offset,value in cases:
        changed = bytearray(image)
        changed[offset:offset+len(value)] = value
        path = ROOT/f'build/ebr-bad-{label}.img'
        path.write_bytes(changed)
        run(f'ebr-bad-{label}',[executable,f'+image={path}','+error=11','+no-dump'])
        assert path.read_bytes() == changed, 'Rejected EBR test modified its media'
