import importlib.util
import random
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


asm = module('asm', ROOT/'microasm/uj11asm.py')
ebr = module('ebr', ROOT/'tools/make_ebr.py')


class AssemblerTests(unittest.TestCase):
    def fail_source(self, source, error):
        with self.assertRaisesRegex(asm.AssemblyError, error):
            asm.assemble(source)

    def test_actual_program(self):
        image, listing, labels, stats = asm.assemble((ROOT/'microcode/checkpoint_seq.uasm').read_text())
        self.assertEqual(len(image), 1024)
        self.assertEqual(labels['CALLSITE'], 0x12)
        self.assertIn('110 ', listing)
        self.assertEqual(image[0x3ff], asm.STOP_WORD)
        self.assertEqual(image[0x333], asm.STOP_WORD)
        self.assertEqual(stats['used_words'], len(listing.splitlines()))

    def test_rr_encoding(self):
        word, edges = asm.encode('alu ADD, a=RS, b=RD, dst=RF, flags=NZVC, seq=FETCH', 0x160, {})
        self.assertEqual(word >> 35, 0)
        self.assertEqual(word >> 31 & 15, 2)
        self.assertEqual(word >> 26 & 31, 16)
        self.assertEqual(word >> 21 & 31, 17)
        self.assertEqual(word >> 15 & 7, 1)
        self.assertEqual(word >> 13 & 3, 2)
        self.assertEqual(word & 0x3ff, 0x200)
        self.assertEqual(edges, [0x20])

    def test_conflicts(self):
        for source, message in [
            (', a=R0', 'missing instruction'),
            ('alu ADD, a=R0, a=R1', 'duplicate/conflicting'),
            ('STOP, nonsense=1', 'unknown field'),
            ('STOP, target=0', 'unknown field'),
            ('FETCH, byte=1', 'unknown field'),
            ('alu ADD, a=R8', 'invalid value'),
            ('alu ADD, pair=DZ', 'invalid value'),
            ('alu ADD, seq=PAGE, next=0, d=IMM, imm=1', 'conflicts'),
            ('alu ADD, d=ZERO, imm=1', 'requires d=IMM'),
            ('alu ADD, d=IMM, imm=256', '8-bit range'),
            ('alu ADD, d=IMM, imm=-1', '8-bit range'),
            ('READ, target=0, byte=2', '1-bit range'),
            ('READ, target=0, a=R0, stream=1', 'requires a=R7'),
            ('READ, target=0, a=R7, stream=1, byte=1', 'requires a=R7'),
            ('READ, target=0, a=R7, stream=2', '1-bit range'),
            ('WRITE, target=0, a=R7, stream=1', 'unknown field'),
            ('READ, target=0, prefetch=2', '1-bit range'),
            ('alu ADD, prefetch=0', 'unknown field'),
            ('CJUMP, target=0', 'requires cond'),
            ('JUMP, target=1024', '10-bit range'),
            ('JUMP, target=MISSING', 'unknown symbol'),
            ('STOP\n.org 0\nSTOP', 'overlap'),
            ('a: STOP\na: STOP', 'duplicate label'),
            ('.org 1024\nSTOP', '10-bit range'),
            ('JUMP, target=1', 'unassembled'),
            ('alu ADD, seq=PAGE, next=$100', 'crosses page'),
            ('OR_MS, target=1', 'overlaps OR'),
            ('OR_MD, target=8', 'unassembled'),
            ('', 'empty'),
            ('.org $3ff\nSTOP\nSTOP', '10-bit range'),
        ]:
            with self.subTest(source=source):
                self.fail_source(source, message)

    def test_control_selectors_are_aligned(self):
        word, edges = asm.encode('READ, a=RS, b=RD1, target=$2a5, byte=1',0,{})
        self.assertEqual(word >> 26 & 31,16)
        self.assertEqual(word >> 21 & 31,19)
        self.assertEqual(word >> 11 & 1023,0x2a5)
        self.assertEqual(word >> 6 & 1,1)
        self.assertEqual(edges,[0x2a5])
        word, _ = asm.encode('CJUMP, cond=NOT_C, target=$3ff',0,{})
        self.assertEqual(word >> 7 & 15,9)
        self.assertEqual(word >> 11 & 1023,1023)
        word, _ = asm.encode('FETCH',0,{})
        self.assertEqual(word >> 26 & 31,7)
        self.assertEqual(word >> 21 & 31,7)
        self.assertEqual(word >> 31 & 15,asm.OPS['ADD'])
        self.assertEqual(word >> 18 & 7,asm.PAIRS['AD'])
        self.assertEqual(word >> 15 & 7,asm.DESTS['RF'])
        self.assertEqual(word >> 13 & 3,asm.FLAGS['KEEP'])
        self.assertEqual(word >> 10 & 7,asm.DINPUT['TWO'])

    def test_page_and_immediate(self):
        w, _ = asm.encode('alu PASSB, d=IMM, imm=$ff, seq=NEXT', 0, {})
        self.assertEqual(w & 255, 255)
        w, edges = asm.encode('alu ADD, seq=PAGE, next=$1fe', 0x1a0, {})
        self.assertEqual(w & 0x3ff, 0x1fe)
        self.assertEqual(edges, [0x1fe])

    def test_stream_extension(self):
        word, edges=asm.encode('READ, a=R7, stream=1, target=$31',0,{})
        self.assertEqual(word >> 5 & 1,1)
        self.assertEqual(word >> 6 & 1,0)
        self.assertEqual(word >> 26 & 31,7)
        self.assertEqual(word >> 31 & 15,11)
        self.assertEqual(edges,[0x31])
        for sel in ['RS','RD']:
            word,_=asm.encode(f'READ, a={sel}, stream=1, target=$31',0,{})
            self.assertEqual(word >> 5 & 1,1)

    def test_prefetch_policy(self):
        a,edges=asm.encode('CALL, target=$200, prefetch=0',0,{})
        b,other=asm.encode('CALL, target=$200',0,{})
        self.assertEqual(a^b,1<<4)
        self.assertEqual(edges,other)
        image,_,_,stats=asm.assemble((ROOT/'microcode/m0.uasm').read_text())
        self.assertEqual(stats['used_words'],700)
        self.assertEqual(stats['encoding_version'],12)
        self.assertEqual(image[0x118]>>4&1,1) # EA entry suppresses speculation
        self.assertEqual(image[0x288]>>4&1,0) # register destination resumes it

    def test_reverse_operands(self):
        word,_=asm.encode('alu SUB, a=RS, b=RD, pair=BA, dst=RF, flags=NZVC, seq=FETCH',0,{})
        self.assertEqual(word>>18&7,7)
        self.assertEqual(word>>21&31,17)
        self.assertEqual(word>>26&31,16)

    def test_wait_control(self):
        word, _ = asm.encode('WAIT, prefetch=0', 0x12, {})
        self.assertEqual(word >> 31, 30)
        self.assertEqual(word & 16, 16)
        with self.assertRaises(asm.AssemblyError):
            asm.encode('WAIT, target=0x20', 0x12, {})

    def test_conditional_retirement(self):
        word,edges=asm.encode('alu SUB, a=RS, b=RS, pair=AD, d=ONE, dst=RF, seq=FETCH_A1',0x98,{})
        self.assertEqual(word>>8&3,3)
        self.assertEqual(word>>13&3,0) # PSW is independent of the A-equals-one predicate
        self.assertEqual(edges,[0x99,0x20])
        self.fail_source('.org $98\nalu SUB, seq=FETCH_A1\nSTOP','unassembled')
        self.fail_source('alu SUB, seq=FETCH_A1, next=0','requires seq=PAGE')

    def test_physical_packing_roundtrip(self):
        rng = random.Random(0x1801)
        words = [rng.getrandbits(36) for _ in range(1024)]
        packed = [ebr.lane_initvals(words, lane) for lane in range(4)]
        # Independent address decomposition, including all 9-bit lane boundaries.
        for addr, expected in enumerate(words):
            actual = 0
            for lane in range(4):
                block = packed[lane][addr >> 5]
                slot = (block >> ((addr >> 1 & 15)*20)) & ((1 << 20)-1)
                self.assertEqual(slot >> 18, 0)
                actual |= ((slot >> ((addr & 1)*9)) & 511) << (lane*9)
            self.assertEqual(actual, expected)
        generated = ebr.generate(words)
        self.assertEqual(generated.count('DP8KC #('), 4)
        self.assertEqual(generated.count('.INITVAL_'), 128)
        self.assertIn('.ADA12(address[9])', generated)
        self.assertIn('.ADA0(1\'b1)', generated)
        self.assertIn('.DOA8(data[35])', generated)

    def test_invalid_physical_image(self):
        for words in [[0]*1023, [1 << 36]*1024, [-1]*1024]:
            with self.assertRaises(ValueError):
                ebr.generate(words)


    def test_operand_width(self):
        for name, code in [('OPERAND', 5), ('MOV', 6)]:
            word, _ = asm.encode(f'alu PASSA, b=RD, dst={name}, flags=NZV, seq=FETCH', 0, {})
            self.assertEqual((word >> 15) & 7, code)
        word, _ = asm.encode('READ, a=RS, byte=IR, stream=1, target=0, prefetch=0', 0, {})
        self.assertEqual(word & 0x78, 0x38)
        fixed, _ = asm.encode('READ, a=RS, byte=1, target=0', 0, {})
        self.assertEqual(fixed & 0x48, 0x40)
        self.fail_source('FETCH, byte=IR', 'unknown field')
        self.fail_source('READ, byte=IR, byte=0, target=0', 'duplicate/conflicting')


    def test_fault_increment(self):
        source = "READ, a=RS, target=1, fault_inc=1\nalu ADD, a=RS, b=RS, pair=AD, d=STEP, dst=RF\nSTOP\n.org 0x020\nSTOP"
        image,_,_,_=asm.assemble(source)
        self.assertEqual(image[0] & 4,4)
        trap,_=asm.encode('TRAP, target=2, prefetch=0',0,{})
        self.assertEqual(trap >> 31 & 15,15)
        for bad in [source.replace('b=RS','b=R0'), source.replace('d=STEP','d=MDR'),
                    source.replace('dst=RF','dst=Q'), source.replace('alu ADD','alu SUB'),
                    source.replace('dst=RF','dst=RF, flags=NZV'), source.replace('dst=RF','dst=RF, seq=FETCH')]:
            self.fail_source(bad,'fault_inc')
        for bad in ['READ, target=0, fault_inc=2', 'WRITE, target=0, fault_inc=1', 'TRAP']:
            with self.assertRaises(asm.AssemblyError):asm.assemble(bad)


    def test_return_trace_field(self):
        base,_=asm.encode('alu PASSA, a=T3, b=R7, dst=RF, seq=FETCH',0,{})
        w,_=asm.encode('alu PASSA, a=T3, b=R7, dst=RF, seq=FETCH, trace=RETURN',0,{})
        self.assertEqual(w^base,1)
        for s in ['alu PASSA, trace=RETURN', 'alu PASSA, seq=FETCH_A1, trace=RETURN',
                  'alu PASSA, seq=FETCH, d=IMM, imm=1, trace=RETURN',
                  'alu PASSA, seq=FETCH, flags=LOAD, trace=RETURN', 'WAIT, trace=RETURN']:
            with self.assertRaises(asm.AssemblyError):asm.encode(s,0,{})

    def test_peripheral_reset_jump(self):
        word, edges = asm.encode('JUMP, target=$22, init=1, prefetch=0', 0x21, {})
        self.assertEqual(word, (1<<35) | (0x22<<11) | 0x11)
        self.assertEqual(edges, [0x22])
        for bad in ['JUMP, target=0, init=2, prefetch=0', 'JUMP, target=0, init=1',
                    'TRAP, target=0, init=1', 'alu PASSA, init=1']:
            with self.assertRaises(asm.AssemblyError): asm.encode(bad, 0, {})

if __name__ == '__main__':
    unittest.main()
