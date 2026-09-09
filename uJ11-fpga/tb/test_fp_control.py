"""Encoding guards and preserved integer/FIS image for FP control extension."""
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/'microasm'))
from uj11asm import assemble,encode,AssemblyError
from link_fis import link

class FPControlTests(unittest.TestCase):
    def test_old_words_and_private_tags(self):
        source=(ROOT/'microcode/m0.uasm').read_text()
        old=source.replace(', fp_init=1','')
        old_source,_,_=link(old,(ROOT/'microcode/fis.uasm').read_text())
        new_source,_,_=link(source,(ROOT/'microcode/fis.uasm').read_text())
        new_source+='\n'+(ROOT/'microcode/fp11_control.uasm').read_text()
        a=assemble(old_source);b=assemble(new_source)
        old_addresses={int(line.split()[0],16) for line in a[1].splitlines()}
        self.assertEqual(len(old_addresses),954)
        self.assertEqual([i for i in sorted(old_addresses) if a[0][i]!=b[0][i]],[0x10])
        self.assertEqual(a[0][0x10]^b[0][0x10],2)
        self.assertTrue(all(b[2][k]==v for k,v in a[2].items()))
        self.assertEqual(b[3]['used_words'],987)
        for line in b[1].splitlines():
            address=int(line.split()[0],16);word=b[0][address]
            if word>>35 and word&2:
                self.assertIn((word>>31)&15,(0,11,12))
                if (word>>31)&15==0:
                    self.assertEqual(address,0x10)
                    self.assertEqual((word>>21)&1023,0) # both RF selectors R0
                    self.assertEqual((word>>11)&1023,0x20)
    def test_private_conflicts(self):
        for command in ('READ','WRITE'):
            for suffix in ('byte=1','byte=IR','prefetch=1','private=2'):
                line=f'{command}, a=T0, target=$020, private=1, prefetch=0, {suffix}'
                with self.assertRaises(AssemblyError):encode(line,0,{})
        for line in ('READ, a=R7, target=$020, private=1, prefetch=0, stream=1',
                     'READ, a=R7, target=$020, private=1, prefetch=0, fault_inc=1',
                     'JUMP, target=$020, private=1',
                     'CJUMP, target=$020, cond=C, fp_init=1',
                     'JUMP, target=FETCH, fp_init=1, init=1, prefetch=0'):
            with self.assertRaises(AssemblyError):encode(line,0,{'FETCH':0x20})
    def test_private_encoding(self):
        for command in ('READ','WRITE'):
            base,_=encode(f'{command}, a=T0, b=T1, target=$020, prefetch=0',0,{})
            private,_=encode(f'{command}, a=T0, b=T1, target=$020, prefetch=0, private=1',0,{})
            self.assertEqual(base^private,2)
if __name__=='__main__':unittest.main()
