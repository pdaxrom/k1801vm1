"""Cold boot a BK disk OS from raw SD sectors, then compare native CPU VRAM with C."""
from pathlib import Path
import json
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from make_sd import fat16
from models import settings


def run_checks(source, executable, run, compare, a16m=False, second_source=None, disk_stop=False):
    source = Path(source)
    disk = source.read_bytes()
    if len(disk) != 1600*512:
        raise ValueError('Expected an 800 KiB BK source disk')
    second = Path(second_source).read_bytes() if second_source else None
    if second is not None and len(second) != 1600*512:
        raise ValueError('Expected an 800 KiB second BK source disk')
    image = bytearray((49152 if second is not None else 43008)*512)
    image[510:512] = b'\x55\xaa'
    for index,kind,base,size in [(0,6,2048,32768),
                               (3,0x0f if second is not None else 0xda,40960,7744 if second is not None else 1600)]:
        struct.pack_into('<B3sB3sII',image,446+index*16,0,b'\xfe\xff\xff',kind,b'\xfe\xff\xff',base,size)
    image[2048*512:(2048+32768)*512] = fat16({
        'LOADERBK.BIN':(ROOT/'build/LOADERBK.BIN').read_bytes(),
        'BK03.ROM':(ROOT/'build/BK03.ROM').read_bytes(),
        'BK.SET':settings(model=3,drives=(6,5,0,0) if second is not None else (4,0,0,0),
                          readonly=3 if second is not None else 1)})
    if second is not None:
        for base,link in ((40960,True),(45056,False)):
            offset=base*512
            image[offset+510:offset+512]=b'\x55\xaa'
            struct.pack_into('<B3sB3sII',image,offset+446,0,b'\xfe\xff\xff',0xda,b'\xfe\xff\xff',2048,1600)
            if link:
                struct.pack_into('<B3sB3sII',image,offset+462,0,b'\xfe\xff\xff',0x0f,b'\xfe\xff\xff',4096,3648)
        image[47104*512:(47104+1600)*512]=second
    first_base = 43008 if second is not None else 40960
    image[first_base*512:(first_base+1600)*512] = disk
    path = ROOT/'build/disk-os-sd.img'
    path.write_bytes(image)
    run('disk-os-c',['build/disk-os-c',path,'3','build/disk-os-model3','20000000','',str(first_base),
                     '47104' if second is not None else '0'])
    if a16m:
        run('disk-os-c-a16m',['build/disk-os-c',path,'0','build/disk-os-model0','20000000','T0~',
                             str(first_base),'47104' if second is not None else '0'])
    cases = [('model3',first_base,47104,['+bios-action=4'])] if second is not None else [('model3',first_base,0,[])]
    if second is not None:
        run('disk-os-c-second',['build/disk-os-c',path,'3','build/disk-os-second','20000000','',
                               '47104','43008'])
        cases.insert(0,('second',47104,43008,['+disk-stop'] if disk_stop else []))
    elif disk_stop:
        cases[0][3].append('+disk-stop')
    for label,base,other,flags in cases:
        screen = (ROOT/f'build/disk-os-{label}-vram.bin').read_bytes()
        screen_path = ROOT/f'build/disk-os-{label}-screen.hex'
        screen_path.write_text(''.join(f'{word:04x}\n' for word, in struct.iter_unpack('<H',screen)))
        run(f'disk-os-{label}-native-24MHz',[executable,f'+image={path}','+expected-model=3',
            f'+screen={screen_path}',
            f'+expected-scroll={json.loads((ROOT/f"build/disk-os-{label}.json").read_text())["scroll"]}',
            '+native-runtime','+runtime-mode=6','+disk-os','+no-dump',
            f'+disk-base={base}',f'+second-base={other}',f'+disk-present={3 if second is not None else 1}',
            f'+disk-readonly={3 if second is not None else 1}',f'+os-prefix=build/disk-os-{label}-native',
            *flags],timeout=18000)
    compare('disk-os-sd-unchanged',path.read_bytes(),bytes(image))
