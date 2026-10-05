"""Native disk ROM transfers through the real system bus and raw SD SPI host."""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def run_checks(executable, run):
    image = bytearray(b'\xa5'*(38912*512))
    image[446:510] = bytes(64)
    image[510:512] = b'\x55\xaa'
    for index, (kind,start,length) in enumerate([(6,2048,32768),(0xda,34816,1600),(0xda,36864,1600)]):
        struct.pack_into('<B3sB3sII',image,446+index*16,0,b'\xfe\xff\xff',kind,b'\xfe\xff\xff',start,length)
    for drive,base in enumerate((34816,36864)):
        for sector in range(1600):
            offset=(base+sector)*512
            image[offset:offset+512]=bytes((drive+sector*31+byte*13)&255 for byte in range(512))
    for name,flags in [('327-sdhc',['+mode=6']),('a16m-sdsc',['+mode=0','+a16m','+sdhc=0'])]:
        path=ROOT/f'build/fdd-sd-{name}.img'
        path.write_bytes(image)
        run(f'fdd-sd-{name}',[executable,f'+image={path}',*flags],timeout=1800)
        expected=bytearray(image)
        expected[34816*512:(34816+10)*512]=b'\x42'*(10*512)
        assert path.read_bytes()==expected, 'Native disk operations changed bytes outside drive A track 0'
        print(f'fdd-sd-{name}: side 0 formatted; side 1, MBR, boot partition, drive B and other tracks unchanged',flush=True)
