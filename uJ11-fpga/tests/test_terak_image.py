import hashlib
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from terak_image import BYTES, COUNT, card, imd, floppy, decode_imd
from sdcard import Label, main, read_label, add_partition


def fixture():
    data = bytearray(b'IMD 1.18: independent Terak fixture\r\n\x1a')
    for track in range(77):
        order = list(range(1, 27))[::-1]
        data.extend(bytes([0, track, 0, 26, 0])+bytes(order))
        for sector in order:
            # Deliberately reverse the sector map; a sequential flatten is wrong.
            index = track*26+sector-1
            kind = 4 if index == 4 else 6 if index == 5 else 0 if index == 6 else 1
            data.append(kind)
            if kind == 1:
                data.extend(bytes(((sector*17+j)&255) for j in range(128)))
            elif kind:
                data.append(0x5a)
    return bytes(data)


class TerakImageTests(unittest.TestCase):
    def test_physical_order_and_status(self):
        payload, flags = imd(fixture())
        self.assertEqual((len(payload), len(flags)), (BYTES, COUNT))
        for number in range(1, 5):
            self.assertEqual(payload[(number-1)*128:number*128],
                             bytes(((number*17+j)&255) for j in range(128)))
        self.assertEqual(flags[4:7], bytes([3, 5, 0]))
        self.assertEqual(payload[4*128:6*128], b'\x5a'*256)
        self.assertEqual(payload[6*128:7*128], bytes(128))

    def test_rejects_incomplete_and_wrong_geometry(self):
        data = fixture()
        for broken in (data[:-1], data[:data.index(b'\x1a')+1]):
            with self.assertRaises(ValueError):
                imd(broken)
        position = data.index(b'\x1a')+1
        for field, value in ((0, 3), (2, 1), (3, 25), (4, 1)):
            bad = bytearray(data)
            bad[position+field] = value
            with self.assertRaises(ValueError):
                imd(bad)

    def test_sd_offsets_crc_backup_and_readonly(self):
        with tempfile.TemporaryDirectory() as directory:
            disk = Path(directory)/'test.IMD'
            disk.write_bytes(fixture())
            before = hashlib.sha256(disk.read_bytes()).hexdigest()
            output = Path(directory)/'card.img'
            card([disk]*4, output, [1, 3])
            data = output.read_bytes()
            self.assertEqual(data[:512], data[512:1024])
            label = Label.unpack(data[:512])
            for unit, p in enumerate(label.partitions):
                self.assertEqual((p.kind, p.unit, p.media, p.blocks, p.flags), (7, unit, 9, 2542, (unit&1)*2))
                payload = data[p.start*512:(p.start+p.blocks)*512]
                self.assertEqual(payload, floppy(disk.read_bytes())[0])
            self.assertEqual(before, hashlib.sha256(disk.read_bytes()).hexdigest())

    def test_mixed_density_both_sides(self):
        data, heads = floppy(double_fixture())
        self.assertEqual(heads, 2)
        self.assertEqual(len(data), 5083*512)
        for c, h, count, n, mfm in [(0,0,26,0,0),(0,1,26,1,1),(1,0,15,2,1),(1,1,15,2,1)]:
            start = (1+(c*2+h)*33)*512
            meta = data[start:start+512]
            self.assertEqual(meta[4:8], bytes([count,n,mfm,0]))
            self.assertEqual(struct.unpack_from('<I',meta,508)[0],zlib.crc32(meta[:508]))
            self.assertEqual(meta[32:37],bytes([c,h,1,n,1]))
            self.assertEqual(data[start+512:start+512+(128<<n)],pattern(c,h,1,128<<n))
        heads, tracks = decode_imd(double_fixture())
        self.assertEqual(tracks[1,0][1][4][4],3)
        self.assertEqual(tracks[1,0][1][5][4],5)
        self.assertEqual(tracks[1,0][1][6][4],0)

    def test_existing_partition_tools_preserve_other_disks(self):
        with tempfile.TemporaryDirectory() as directory:
            d=Path(directory); image=d/'mixed.img'; disk=d/'dd.fd'; exported=d/'copy.fd'
            disk.write_bytes(floppy(double_fixture())[0])
            main([str(image),'init','--blocks','32768'])
            main([str(image),'add','mscp','1','--blocks','100','--boot'])
            with image.open('r+b') as f:
                label,_=read_label(f); old=label.partitions[0]
                f.seek(old.start*512);f.write(b'OTHER DISK'*5120)
            main([str(image),'add','terak-ds','3','--readonly'])
            main([str(image),'import','qb','3',str(disk)])
            main([str(image),'export','qb','3',str(exported)])
            self.assertEqual(disk.read_bytes(),exported.read_bytes())
            with image.open('rb') as f:
                label,_=read_label(f)
                self.assertEqual(label.partitions[0],old)
                f.seek(old.start*512);self.assertEqual(f.read(51200),b'OTHER DISK'*5120)


def pattern(c,h,r,length):
    return bytes((c*11+h*37+r*17+i)&255 for i in range(length))


def double_fixture():
    data=bytearray(b'IMD 1.18: mixed density independent fixture\r\n\x1a')
    for c in range(77):
        for h in range(2):
            count,n,mode=(26,0,0) if (c,h)==(0,0) else (26,1,3) if c==0 else (15,2,3)
            numbers=list(range(1,count+1))[::-1]
            data.extend(bytes([mode,c,h,count,n])+bytes(numbers))
            for r in numbers:
                kind=4 if (c,h,r)==(1,0,5) else 6 if (c,h,r)==(1,0,6) else 0 if (c,h,r)==(1,0,7) else 1
                data.append(kind)
                if kind==1:data.extend(pattern(c,h,r,128<<n))
                elif kind:data.append(0x5a)
    return bytes(data)


def boot_fixture():
    _, tracks = decode_imd(double_fixture())
    words = [int(line,16) for line in (Path(__file__).parent/'terak/qb_boot_fixture.mem').read_text().splitlines()]
    code = struct.pack('<'+'H'*len(words),*words).ljust(512,b'\0')
    data = bytearray(b'IMD 1.18: cold QB DMA bootstrap fixture\r\n\x1a')
    for (c,h),(mfm,sectors) in sorted(tracks.items()):
        data.extend(bytes([3 if mfm else 0,c,h,len(sectors),sectors[0][3]]))
        data.extend(bytes(s[2] for s in sectors))
        for _,_,r,n,_,_ in sectors:
            payload = bytearray(pattern(c,h,r,128<<n))
            if (c,h,r)==(0,0,7):payload[71],payload[75]=0xd4,0xf2
            if (c,h,r)==(1,0,1):payload[:]=code
            data.append(1);data.extend(payload)
    return bytes(data)
