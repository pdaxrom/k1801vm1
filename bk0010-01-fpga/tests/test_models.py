#!/usr/bin/env python3
import struct
import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from models import MODELS, bundle, crc16, disk_lba, parse_bundle, parse_settings, partitions, settings


class Models(unittest.TestCase):
    def test_settings(self):
        for model in range(5):
            for frequency in (3, 4, 6, 8, 12, 16, 24):
                for color in (0, 1):
                    data = settings(model, frequency, color)
                    config = parse_settings(data)
                    self.assertEqual((config['model'], config['frequency'], config['color']), (model, frequency, color))
                    for position in range(16):
                        corrupted = bytearray(data)
                        corrupted[position] ^= 1
                        with self.assertRaises(ValueError):
                            parse_settings(corrupted)
        for config in ((0, (2, 3, 4, 2)), (0, (2, 3, 0, 4)), (3, (2, 2, 0, 0)), (3, (1, 3, 0, 0)), (1, (2, 0, 0, 0))):
            with self.assertRaises(ValueError):
                settings(config[0], drives=config[1])
        self.assertEqual(crc16(b'123456789'), 0x31c3)

    def test_bundles(self):
        for model in range(5):
            data = bundle(model)
            ident, segments = parse_bundle(data, model)
            self.assertEqual(ident, model)
            self.assertEqual([len(segment) for _, segment in segments], [s[2] for s in MODELS[model]['segments']])
            for position in (0, 8, 12, 14, 16, 20, 24, 28, 32, 36, 40, 44, 511, 512, len(data) - 1):
                corrupted = bytearray(data)
                corrupted[position] ^= 1
                with self.assertRaises(ValueError):
                    parse_bundle(corrupted, model)
            # A forged CRC cannot authorize a different physical memory map.
            corrupted = bytearray(data)
            struct.pack_into('<I', corrupted, 32, 0)
            struct.pack_into('<I', corrupted, 28, 0)
            struct.pack_into('<I', corrupted, 28, zlib.crc32(corrupted[:512]))
            with self.assertRaises(ValueError):
                parse_bundle(corrupted)

    def test_partitions(self):
        mbr = bytearray(512)
        mbr[510:] = b'\x55\xaa'
        for index, (kind, start, length) in enumerate(((6, 2048, 32768), (1, 34816, 1600), (1, 36864, 4096), (1, 43008, 1600))):
            struct.pack_into('<B3sB3sII', mbr, 446 + index * 16, 0, bytes(3), kind, bytes(3), start, length)
        desc = partitions(mbr, 45000)
        self.assertEqual(disk_lba(desc, 2, 0, 0, 1), 34816)
        self.assertEqual(disk_lba(desc, 2, 79, 1, 10), 34816 + 1599)
        self.assertEqual(disk_lba(desc, 3, 79, 1, 10), 36864 + 1599)
        for args in ((1, 0, 0, 1), (2, 80, 0, 1), (2, 0, 2, 1), (2, 0, 0, 0), (2, 0, 0, 11)):
            with self.assertRaises(ValueError):
                disk_lba(desc, *args)
        for offset, value in ((462 + 8, 2000), (462 + 12, 0xffffffff), (478 + 8, 36000)):
            corrupted = bytearray(mbr)
            struct.pack_into('<I', corrupted, offset, value)
            with self.assertRaises(ValueError):
                partitions(corrupted, 45000)
        desc[1]['length'] = 1599
        with self.assertRaises(ValueError):
            disk_lba(desc, 2, 0, 0, 1)


    def test_logical_partitions(self):
        mbr = bytearray(512)
        mbr[510:] = b'\x55\xaa'
        for index,kind,base,size in ((0,6,2048,32768),(1,1,34816,1440),(2,1,36864,2880),(3,15,40960,7744)):
            struct.pack_into('<B3sB3sII',mbr,446+index*16,0,bytes(3),kind,bytes(3),base,size)
        ebrs = {base:bytearray(512) for base in (40960,45056)}
        for base,sector in ebrs.items():
            sector[510:] = b'\x55\xaa'
            struct.pack_into('<B3sB3sII',sector,446,0,bytes(3),0xda,bytes(3),2048,1600)
        struct.pack_into('<B3sB3sII',ebrs[40960],462,0,bytes(3),15,bytes(3),4096,3648)
        desc = partitions(mbr,50000,ebrs.__getitem__)
        self.assertEqual((desc[4]['start'],desc[5]['start']),(43008,47104))
        self.assertEqual(disk_lba(desc,5,79,1,10),44607)
        self.assertEqual(disk_lba(desc,6,79,1,10),48703)
        self.assertEqual(parse_settings(settings(model=3,drives=(6,5,0,0),readonly=3))['drives'],(6,5,0,0))
        with self.assertRaises(ValueError):
            disk_lba(desc,4,0,0,1)
        with self.assertRaises(ValueError):
            partitions(mbr,50000)
        for base,offset,fmt,value in ((40960,510,'H',0),(40960,454,'I',0),
                                      (40960,458,'I',8000),(40960,470,'I',3000),
                                      (40960,466,'B',0xda),(40960,478,'B',1),
                                      (45056,466,'B',15),(45056,458,'I',1700)):
            changed = {key:bytearray(value) for key,value in ebrs.items()}
            struct.pack_into('<'+fmt,changed[base],offset,value)
            with self.assertRaises(ValueError):
                partitions(mbr,50000,changed.__getitem__)

        # A valid continuation belongs to other machines; BK still maps only 5/6.
        shared = bytearray(mbr)
        struct.pack_into('<I',shared,506,14837)
        tail = {key:bytearray(value) for key,value in ebrs.items()}
        struct.pack_into('<B3sB3sII',tail[45056],462,0,bytes(3),15,bytes(3),8192,6645)
        self.assertEqual(partitions(shared,60000,tail.__getitem__)[4:],desc[4:])
        for offset,fmt,value in ((462,'B',0x80),(466,'B',0xda),(470,'I',4096),
                                 (474,'I',0),(474,'I',7000)):
            changed = {key:bytearray(value) for key,value in tail.items()}
            struct.pack_into('<'+fmt,changed[45056],offset,value)
            with self.assertRaises(ValueError):
                partitions(shared,60000,changed.__getitem__)



if __name__ == '__main__':
    unittest.main()
