"""ABI1 format boundaries and malformed-file rejection, independent of RTL."""
import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from service_image import pack,decode,SLOTS


class ServiceImage(unittest.TestCase):
    def setUp(self): self.image=pack(bytes(1026),1,2048,0o400,0o402)

    def mutate(self,index,value):
        data=bytearray(self.image)
        struct.pack_into('<H',data,2*index,value)
        struct.pack_into('<H',data,22,0)
        struct.pack_into('<H',data,22,-sum(struct.unpack('<16H',data[:32]))&65535)
        return data

    def test_layout_and_endianness(self):
        self.assertEqual(self.image[:4],b'UJ11')
        self.assertEqual(decode(self.image)['file_blocks'],4)
        self.assertEqual(sum(struct.unpack('<16H',self.image[:32]))&65535,0)

    def test_full_physical_upper_end(self):
        image=pack(bytes(49152),2,49152,0o40000,0o157776)
        self.assertEqual(decode(image)['base']+decode(image)['memory_bytes'],65536)

    def test_bss_larger_than_signed_word(self):
        self.assertEqual(decode(pack(bytes(2),2,49152,0o40000,0o40000))['memory_bytes'],49152)

    def test_fields_with_valid_header_checksum(self):
        for index,value in [(0,0),(1,0),(2,2),(3,2),(4,0),(4,3),(5,0),(5,0o402),
                            (6,0),(6,1025),(7,1024),(7,0o40000),(7,2049),
                            (8,0o376),(8,0o401),(8,0o400+1026),(8,0o160000),
                            (9,0o376),(9,0o403),(12,1),(13,1),(14,1),(15,1)]:
            with self.subTest(index=index,value=value),self.assertRaises(ValueError):decode(self.mutate(index,value))

    def test_extent_padding_and_checksums(self):
        for image in [self.image[:-512],self.image+bytes(512),self.image[:-1],
                      self.image[:510]+b'\x01\x00'+self.image[512:],
                      self.image[:22]+b'\0\0'+self.image[24:],
                      self.image[:600]+b'\x01'+self.image[601:],self.image[:-1]+b'\x01']:
            with self.subTest(size=len(image)),self.assertRaises(ValueError):decode(image)

    def test_pack_rejects_unrepresentable_fields(self):
        for payload,kind,memory,entry,fault in [(b'1',1,2,0o400,0o400),
                    (bytes(65536),2,65536,0o40000,0o40000),
                    (bytes(2),3,2,0o400,0o400),(bytes(2),1,-1,0o400,0o400)]:
            with self.subTest(kind=kind,memory=memory),self.assertRaises(ValueError):pack(payload,kind,memory,entry,fault)


if __name__=='__main__':unittest.main()
