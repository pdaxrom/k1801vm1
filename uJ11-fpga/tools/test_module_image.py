#!/usr/bin/env python3
"""Malformed-file and range tests for the published CP67 module format."""
import struct
import unittest
from module_image import pack, decode, entry, bounds, allocation_entry, placed_image, relocate


class ModuleFormat(unittest.TestCase):
    def setUp(self):
        self.image=pack(0o10000,struct.pack('<3H',0o5000,0o207,0xffff))

    def test_literal_header_and_directory(self):
        h=struct.unpack('<16H',self.image[:32])
        self.assertEqual(h[:7],(0o45125,0o30461,2,3,0o10000,3,0o5206))
        self.assertEqual(sum(h)&65535,0)
        self.assertEqual(entry(0o10000,self.image[512:518]),struct.pack('<4H',0o10000,3,0o5206,0xc103))
        self.assertEqual(decode(self.image)['bytes'],6)

    def test_checksum_wraparound(self):
        self.assertEqual(decode(pack(0o6000,b'\xff\xff\x01\x00'))['checksum'],0)

    def test_bounds(self):
        self.assertEqual(bounds(0o6000,256),0o7000)
        self.assertEqual(bounds(0o10000,26624),0o160000)
        for base,words in ((0o5776,1),(0o6001,1),(0o6776,2),(0o7000,1),
                           (0o7776,2),(0o10000,0),(0o10000,32768),(0o157776,2),(0o160000,1)):
            with self.subTest(base=base,words=words),self.assertRaises(ValueError):bounds(base,words)

    def test_header_even_with_recomputed_checksum(self):
        for index,value in ((0,0),(1,0),(2,1),(3,2),(4,0o10001),(4,0o7000),(5,0),(5,32768),(8,1)):
            h=list(struct.unpack('<16H',self.image[:32]));h[index]=value;h[7]=0;h[7]=-sum(h)&65535
            with self.subTest(index=index,value=value),self.assertRaises(ValueError):decode(struct.pack('<16H',*h)+self.image[32:])

    def test_corruption_padding_and_extent(self):
        for index in (14,32,510,512,517,518,1023):
            data=bytearray(self.image);data[index]^=1
            with self.subTest(index=index),self.assertRaises(ValueError):decode(bytes(data))
        for data in (self.image[:-1],self.image[:-512],self.image+bytes(512)):
            with self.subTest(length=len(data)),self.assertRaises(ValueError):decode(data)

    def test_partial_word_or_empty_image(self):
        for image in (b'',b'\0',b'\0'*3):
            with self.subTest(length=len(image)),self.assertRaises(ValueError):pack(0o6000,image)

    def test_full_allocation(self):
        image=struct.pack('<3H',0o12700,0,0o207)
        module=pack(0o10000,image,memory_bytes=512)
        record=decode(module)
        self.assertEqual((record['version'],record['bytes'],record['memory_bytes']),(3,6,512))
        meta=struct.unpack('<8H',allocation_entry(0o10000,image,512))
        self.assertEqual(meta[1:6],(0o10000,3,0o13107,256,3))
        self.assertEqual(sum(meta)&65535,0)
        for memory_bytes in (0,4,7,65536):
            with self.subTest(memory_bytes=memory_bytes),self.assertRaises(ValueError):
                pack(0o10000,image,memory_bytes=memory_bytes)
        for words in (0,2,32768,65535):
            data=bytearray(module);h=list(struct.unpack('<16H',data[:32]))
            h[8]=words;h[7]=0;h[7]=-sum(h)&65535;data[:32]=struct.pack('<16H',*h)
            with self.subTest(words=words),self.assertRaises(ValueError):decode(data)

    def test_relocation(self):
        image=struct.pack('<4H',0o12700,0o1000,0o160,0o207)
        pairs=[(1,0o1000),(0x8002,0o160)]
        module=pack(0o10000,image,memory_bytes=128,relocations=pairs)
        self.assertEqual(decode(module)['relocations'],pairs)
        for base in (0o10000,0o44000,0o140000):
            expected=struct.pack('<4H',0o12700,base,(0o160-base+0o1000)&65535,0o207)
            self.assertEqual(placed_image(module,base),expected)
        for index in (18,20,22,24,1024,1026,1032,1535):
            data=bytearray(module);data[index]^=1
            with self.subTest(index=index),self.assertRaises(ValueError):decode(data)
        for bad in (pairs[::-1],pairs+[pairs[1]],[(4,0)],[(1,0)]):
            with self.subTest(pairs=bad),self.assertRaises(ValueError):relocate(image,bad,0o10000)
        for base in (0o7000,0o157776):
            with self.subTest(base=base),self.assertRaises(ValueError):placed_image(module,base)

    def test_zero_relocations(self):
        module=pack(0o6000,b'\x87\x00',memory_bytes=2,relocations=[])
        self.assertEqual(len(module),1024)
        self.assertEqual(placed_image(module,0o10000),b'\x87\x00')


if __name__=='__main__':unittest.main()
