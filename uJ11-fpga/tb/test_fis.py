"""Hand-calculated FIS anchors and control-store linker invariants."""
from fractions import Fraction
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from fis_reference import decode, encode, reference
from link_fis import link
from uj11asm import assemble, encode as instruction, AssemblyError


class FisTests(unittest.TestCase):
    def test_format_and_dirty_zero(self):
        self.assertEqual(decode(0x40800000),1)
        self.assertEqual(decode(0xc0c00000),Fraction(-3,2))
        self.assertEqual(decode(0x00800000),Fraction(2)**-128)
        for bits in (0,0x80000000,0x7fffff,0x807fffff):
            self.assertEqual(decode(bits),0)

    def test_rounding_and_exponent_limits(self):
        for sign in (-1,1):
            bits = 0xc0800001 if sign < 0 else 0x40800001
            self.assertEqual(encode(sign*(1+Fraction(1,1<<24))),(bits,8 if sign<0 else 0,False))
        self.assertEqual(encode(1-Fraction(1,1<<25)),(0x40800000,0,False))
        self.assertEqual(encode(2-Fraction(1,1<<24)),(0x41000000,0,False))
        self.assertEqual(encode(Fraction(2)**127),(None,2,True))
        self.assertEqual(encode(Fraction(2)**-129),(None,10,True))
        self.assertEqual(encode(Fraction(2)**-129,True),(0,4,False))

    def test_operation_specific_errors(self):
        self.assertEqual(reference(1,0x00800001,0x00800000),(0,4,False))
        self.assertEqual(reference(2,0x00800000,0x40000000),(0x00800000,10,True))
        self.assertEqual(reference(3,0x40800000,0),(0x40800000,11,True))
        self.assertEqual(reference(3,0,0),(0,11,True))
        self.assertEqual(reference(3,0x40800000,0x40c00000),(0x402aaaab,0,False))

    def test_dq_and_legacy_zq(self):
        z,_ = instruction('alu SUB, pair=ZQ, dst=Q, flags=NZVC',0,{})
        d,_ = instruction('alu SUB, pair=DQ, dst=Q, flags=NZVC',0,{})
        self.assertEqual(z,d)
        d,_ = instruction('alu ADD, pair=DQ, d=IMM, imm=32, dst=Q, flags=NZVC',0,{})
        self.assertEqual((d>>18)&7,5)
        with self.assertRaisesRegex(AssemblyError,'ZQ requires'):
            instruction('alu ADD, pair=ZQ, d=ONE',0,{})

    def test_linker_preserves_baseline(self):
        base=(ROOT/'microcode/m0.uasm').read_text()
        extension=(ROOT/'microcode/fis.uasm').read_text()
        combined,placement,report=link(base,extension)
        self.assertEqual((combined,placement,report),link(base,extension))
        self.assertEqual(report['used_words'],954)
        self.assertEqual(report['source_words'],223)
        self.assertEqual(report['bridge_words'],31)
        old,listing,labels,_=assemble(base)
        new,_,mapped,_=assemble(combined)
        for line in listing.splitlines():
            address=int(line.split()[0],16)
            self.assertEqual(old[address],new[address])
        self.assertTrue(all(mapped[name]==value for name,value in labels.items()))
        self.assertEqual(mapped['FIS_ENTRY'],0x11)
        with self.assertRaisesRegex(AssemblyError,'duplicate'):
            link(base,extension+'\nFIS_ENTRY: STOP\n')
        with self.assertRaisesRegex(AssemblyError,'accepts no'):
            link(base,extension+'\n.org 0\nSTOP\n')


if __name__=='__main__':unittest.main()
