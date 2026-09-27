"""Menu envelope integrity, optional feature compatibility, and safe installation."""
from dataclasses import replace
from pathlib import Path
import struct
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import storage_menu
from sdcard import Label, MENU, main, publish, read_label

class MenuImage(unittest.TestCase):
    def test_envelope_and_corruption(self):
        data=storage_menu.pack(bytes(range(256))*5)
        self.assertEqual(storage_menu.unpack(data)['blocks'],3)
        for offset in range(len(data)):
            bad=bytearray(data);bad[offset]^=1
            with self.assertRaises(ValueError):storage_menu.unpack(bad)
        self.assertEqual(storage_menu.crc16(b'123456789'),0x31c3)
    def test_bounds(self):
        for data in (b'',b'x',bytes(8194)):
            with self.assertRaises(ValueError):storage_menu.pack(data)
        self.assertEqual(storage_menu.unpack(storage_menu.pack(bytes(8192)))['blocks'],16)
        with self.assertRaises(ValueError):Label(4096,features=MENU,reserved=18).validate()
        with self.assertRaises(ValueError):Label(4096,features=2).validate()
    def test_install_disable_and_verify_without_default(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td);card=d/'card.img';image=d/'menu.img'
            image.write_bytes(storage_menu.pack(bytes(range(256))*3))
            main([str(card),'init','--blocks','65536'])
            main([str(card),'add','rk07','7','--boot'])
            main([str(card),'boot-off'])
            main([str(card),'menu',str(image)])
            with card.open('rb') as f:
                label,_=read_label(f);self.assertEqual(label.features,MENU)
                self.assertFalse(any(p.flags & 1 for p in label.partitions))
                self.assertEqual(storage_menu.read(f)['blocks'],2)
                f.seek(2048*512);self.assertEqual(f.read(512),bytes(512))
            main([str(card),'menu-off'])
            with card.open('rb') as f:self.assertEqual(read_label(f)[0].features,0)
            before=card.read_bytes();image.write_bytes(b'invalid')
            with self.assertRaises(ValueError):main([str(card),'menu',str(image)])
            self.assertEqual(card.read_bytes(),before)
    def test_refuse_overlap_before_writing(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td);card=d/'card.img';image=d/'menu.img'
            image.write_bytes(storage_menu.pack(bytes(512)))
            with card.open('w+b') as f:f.truncate(4096*512);publish(f,Label(4096,reserved=2))
            before=card.read_bytes()
            with self.assertRaises(ValueError):main([str(card),'menu',str(image)])
            self.assertEqual(card.read_bytes(),before)

if __name__=='__main__':unittest.main()
