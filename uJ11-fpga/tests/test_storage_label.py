"""On-disk format compatibility, corruption boundaries and actual CLI copies."""
import ctypes
from dataclasses import replace
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from sdcard import Label, Partition, add_partition, main, read_label, publish

class Part(ctypes.Structure):
    _fields_=[('start',ctypes.c_uint32),('blocks',ctypes.c_uint32)]+[(n,ctypes.c_uint8) for n in ('kind','unit','media','flags','mode')]
class Decoded(ctypes.Structure):
    _fields_=[('blocks',ctypes.c_uint32),('features',ctypes.c_uint32),('count',ctypes.c_uint),('part',Part*14)]

def fixcrc(raw):
    b=bytearray(raw);b[20:24]=bytes(4)
    struct.pack_into('<I',b,20,zlib.crc32(b))
    return bytes(b)

class StorageLabel(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory()
        lib=Path(cls.temp.name)/'label.so'
        subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-shared','-fPIC',str(ROOT/'firmware/storage/label.c'),'-o',str(lib)],check=True)
        cls.lib=ctypes.CDLL(str(lib))
        cls.decode=cls.lib.sd_label_decode
        cls.decode.argtypes=[ctypes.c_char_p,ctypes.c_uint32,ctypes.POINTER(Decoded)]
        cls.decode.restype=ctypes.c_int
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def accepted(self,raw,capacity=1000000):
        result=Decoded()
        valid=bool(self.decode(raw,capacity,ctypes.byref(result)))
        try:
            label=Label.unpack(raw)
            expected=label.blocks<=capacity
        except (ValueError,UnicodeError):expected=False
        self.assertEqual(valid,expected)
        if valid:
            self.assertEqual(result.count,len(label.partitions))
            self.assertEqual(result.blocks,label.blocks)
            self.assertEqual(result.features,label.features)
            for c,p in zip(result.part,label.partitions):
                for key,_ in Part._fields_:self.assertEqual(getattr(c,key),getattr(p,key))
        return valid
    def label(self):
        a=add_partition(Label(262144),'rk07',0,boot=True,name='RT11XM')
        return add_partition(a,'rk07',7,readonly=True)
    def test_roundtrip_all_media_and_rh70(self):
        a=Label(1000000)
        for m,u,b in [('rk05',0,None),('rk06',0,None),('rk07',7,None),('rm05',0,None),('mscp',3,50000),('rl01',0,None),('rl02',3,None),('tk50',7,50000)]:
            a=add_partition(a,m,u,blocks=b,mode=int(m in ('rk06','rk07')))
        self.assertTrue(self.accepted(a.pack()))
        self.assertEqual(Label.unpack(a.pack()),a)
    def test_menu_feature_in_c_decoder(self):
        label=replace(self.label(),features=1)
        self.assertTrue(self.accepted(label.pack()))
        raw=bytearray(label.pack());struct.pack_into('<I',raw,28,18)
        self.assertFalse(self.accepted(fixcrc(raw)))
    def test_every_single_bit_corruption(self):
        raw=self.label().pack()
        for bit in range(4096):
            b=bytearray(raw);b[bit//8]^=1<<(bit%8)
            self.assertFalse(self.accepted(bytes(b)))
    def test_valid_crc_structural_corruption(self):
        raw=self.label().pack()
        cases=[(8,2,2),(14,15,2),(24,1000001,4),(28,0,4),(32,2,4),(36,2,4),(40,1,1),
               (64,7,1),(65,1,1),(66,8,1),(67,9,1),(68,4,1),(69,1,1),(70,2,1),(71,1,1),
               (72,1,4),(76,53789,4),(80,0,4),(84,1,4),(88,255,1),
               (96+2,0,1),(96+4,3,1),(96+6,1,1),(96+8,2049,4),(128,1,1)]
        for pos,value,size in cases:
            b=bytearray(raw);b[pos:pos+size]=value.to_bytes(size,'little')
            self.assertFalse(self.accepted(fixcrc(b)),(pos,value))
        self.assertFalse(self.accepted(raw,capacity=262143))
    def test_random_crc_valid_fuzz(self):
        rng=random.Random(11);raw=self.label().pack()
        for _ in range(1000):
            b=bytearray(raw)
            for _ in range(rng.randrange(1,5)):b[rng.randrange(512)]=rng.randrange(256)
            self.accepted(fixcrc(b))
    def test_cli_import_export_and_backup(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td);card=d/'card.img';src=d/'rk.img';dst=d/'copy.img'
            src.write_bytes(bytes(range(256))*9744)
            main([str(card),'init','--blocks','65536'])
            main([str(card),'add','rk05','0','--boot'])
            main([str(card),'import','rk','0',str(src)])
            main([str(card),'export','rk','0',str(dst)])
            self.assertEqual(src.read_bytes(),dst.read_bytes())
            with card.open('r+b') as f:
                label,_=read_label(f);f.seek(0);f.write(b'broken!!');f.flush()
                recovered,lba=read_label(f)
                self.assertEqual((recovered,lba),(label,1))
            with self.assertRaises(FileExistsError):main([str(card),'init','--blocks','65536'])
            # Input mismatch is rejected before touching the data area.
            old=card.read_bytes();src.write_bytes(b'x')
            with self.assertRaises(ValueError):main([str(card),'import','rk','0',str(src)])
            self.assertEqual(card.read_bytes(),old)
    def test_tape_byte_length(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td);src=d/'t.tap';card=d/'card.img';dst=d/'copy.tap'
            src.write_bytes(b'\0\0\0\0\xff\xff\xff\xff')
            main([str(card),'init','--blocks','4096'])
            main([str(card),'add','tk50','0','--blocks','100'])
            main([str(card),'import','tq','0',str(src)])
            main([str(card),'export','tq','0',str(dst)])
            self.assertEqual(dst.read_bytes(),src.read_bytes())

if __name__=='__main__':unittest.main()
