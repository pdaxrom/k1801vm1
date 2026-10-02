"""Regression boundary: selecting MMU must not change released CPU images."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/'microasm'))
from board_common import sources
from build_mmu import CORE,MICROCODE,build,fpp_mode,clock_mhz,pipeline_mode
from build_mmu_board import diagnostics_enabled
from synthesis_mmu import display_constraints
from uj11mmuasm import assemble,AssemblyError
# These checks intentionally start independent configurations, including when
# invoked by `make ... FPP=off test`; do not inherit command-line overrides.
MAKE_ENV={k:v for k,v in os.environ.items() if k not in
          ('MAKEFLAGS','MFLAGS','MAKEOVERRIDES','MAKELEVEL','CPU','BOARD','FPP','IOP','UJ11_MMU_IOP','UJ11_MMU_CLOCK_MHZ','UJ11_MMU_PIPELINE','MMU_CLOCK_MHZ','HC7000_DIAGNOSTICS','UJ11_HC7000_DIAGNOSTICS','OUT')}

class Profiles(unittest.TestCase):
    def test_display_constraints_only_touch_display_when_enabled(self):
        original=(ROOT/'boards/hc7000/pins.lpf').read_text()
        self.assertEqual(display_constraints(original,False),original)
        modified=display_constraints(original,True)
        before=original.splitlines();after=modified.splitlines()
        self.assertEqual(len(before),len(after))
        changed=0
        for old,new in zip(before,after):
            if old==new:continue
            changed+=1
            self.assertTrue(old.startswith('IOBUF PORT "seg_led_'))
            self.assertEqual(new,old.replace(' ;',' DRIVE=4 SLEWRATE=SLOW ;'))
        self.assertEqual(changed,18)
        with self.assertRaises(AssertionError):display_constraints('IOBUF PORT "tx" ;',True)

    def test_diagnostics_default_off_and_hc7000_mmu_only(self):
        with patch.dict(os.environ,{},clear=True):
            self.assertFalse(diagnostics_enabled())
            os.environ['UJ11_HC7000_DIAGNOSTICS']='1'
            self.assertTrue(diagnostics_enabled())
            os.environ['UJ11_HC7000_DIAGNOSTICS']='yes'
            with self.assertRaises(ValueError):diagnostics_enabled()
        for args in (['HC7000_DIAGNOSTICS=1'],
                     ['BOARD=hc7000-lcd-sram','HC7000_DIAGNOSTICS=1'],
                     ['CPU=mmu','BOARD=hc7000-lcd-sram','HC7000_DIAGNOSTICS=2']):
            result=subprocess.run(['make','-n',*args],cwd=ROOT,capture_output=True,text=True,env=MAKE_ENV)
            self.assertNotEqual(result.returncode,0,result.stdout)
        result=subprocess.run(['make','-n','CPU=mmu','BOARD=hc7000-lcd-sram','HC7000_DIAGNOSTICS=1',
                               'synthesis'],cwd=ROOT,capture_output=True,text=True,env=MAKE_ENV)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('-diagnostics',result.stdout)
        result=subprocess.run(['make','-n'],cwd=ROOT,capture_output=True,text=True,env=MAKE_ENV)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('build_hardware.py',result.stdout)
        self.assertNotIn('test_diagnostics.py',result.stdout)

    def test_alu_return_encoding_and_abort_repair(self):
        image,_,_,_=assemble('alu ADD, a=R0, b=R1, pair=AD, d=IMM, imm=0xabcd, dst=RF, flags=NZVC, seq=RETURN')
        self.assertEqual(image[0]>>38&1,1)
        self.assertEqual(image[0]>>35&1,0)
        self.assertEqual(image[0]>>8&3,0)
        self.assertEqual((image[0]&255)|((image[0]>>45&255)<<8),0xabcd)
        self.assertEqual(image[0]>>13&3,2)
        for text in ('alu PASSA, seq=RETURN, next=0',
                     'alu PASSA, seq=RETURN, trace=RETURN',
                     'READ, a=R0, fault_inc=1\nalu ADD, a=R0, b=R0, pair=AD, d=STEP, dst=RF, seq=RETURN'):
            with self.subTest(text=text), self.assertRaises(AssemblyError):assemble(text)

    def test_clock_selects_pipeline_and_rejects_invalid_combinations(self):
        with patch.dict(os.environ,{},clear=True):
            self.assertEqual(clock_mhz(),24)
            self.assertFalse(pipeline_mode())
            os.environ['UJ11_MMU_CLOCK_MHZ']='50'
            self.assertEqual(clock_mhz(),50)
            self.assertTrue(pipeline_mode())
            os.environ['UJ11_MMU_PIPELINE']='off'
            with self.assertRaises(ValueError):pipeline_mode()
            os.environ['UJ11_MMU_CLOCK_MHZ']='49'
            with self.assertRaises(ValueError):clock_mhz()

    def test_mmuless_sources_unchanged(self):
        baseline=json.loads((ROOT/'tests/baseline/hc1200-serv.json').read_text())
        for path,digest in baseline['unchanged_sources'].items():
            with self.subTest(path=path):
                self.assertEqual(hashlib.sha256((ROOT.parent/path).read_bytes()).hexdigest(),digest)

    def test_microcode_build_does_not_touch_legacy_images(self):
        paths=[ROOT/'build/hardware'/name for name in
               ('m0.mem','decode.mem','firmware.mem','uj11_m0_ebr.v','uj11_decode_table.v','uj11_firmware_rom.v')]
        before={p:p.read_bytes() for p in paths if p.exists()}
        build()
        self.assertTrue(all(p.read_bytes()==data for p,data in before.items()))
        self.assertFalse(any('/mmu/' in p for p in sum((sources()[0],sources()[1]),[])))
        self.assertIn('rtl/mmu/uj11_mmu_cpu.v',CORE)

    def test_reject_unsupported_build_before_running_tools(self):
        for args in (['MMU_CLOCK_MHZ=50'],['CPU=mmu','BOARD=hc7000-lcd-sram','MMU_CLOCK_MHZ=49'],['CPU=oops'],['CPU=mmu','BOARD=hc1200'],['CPU=mmu','BOARD=hc7000-lcd-sram','software'],
                     ['CPU=mmu','BOARD=hc7000-lcd-sram','FPP=oops'],['FPP=off'],
                     ['CPU=mmu','BOARD=hc7000-lcd-sram','FPP=off','test-sd-image'],
                     ['IOP=storage'],['CPU=mmu','BOARD=hc7000-lcd-sram','IOP=storage'],
                     ['CPU=mmu','BOARD=hc7000-lcd-sram','FPP=off','IOP=bogus']):
            result=subprocess.run(['make','-n',*args],cwd=ROOT,capture_output=True,text=True,env=MAKE_ENV)
            self.assertNotEqual(result.returncode,0,result.stdout)

    def test_board_targets_use_only_mmu_runners(self):
        for goal,runner in (('hardware','build_mmu_board.py'),('synthesis','synthesis_mmu.py'),
                            ('test-rt11','test_mmu_board.py'),('export','export_jed.py'),
                            ('sd-image','build_sd_mmu.py'),('test-sd-image','test_sd_mmu.py'),
                            ('hg','build_hgx.py'),('test-hg-time','test_hgx.py')):
            result=subprocess.run(['make','-n','CPU=mmu','BOARD=hc7000-lcd-sram',goal],cwd=ROOT,capture_output=True,text=True,env=MAKE_ENV)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn(runner,result.stdout)
            self.assertNotIn('build_hardware.py',result.stdout)
            self.assertNotIn('run_rt11.py',result.stdout)

    def test_no_fpp_removes_microcode_bank_and_restores_enabled_build(self):
        from build_mmu import OUT
        enabled=build('microcode')
        original=(OUT/'microcode.mem').read_bytes()
        try:
            disabled=build('off')
            self.assertEqual(disabled['microcode_ebr'],9)
            self.assertNotIn('FPP_ENTRY',json.loads((OUT/'labels.json').read_text()))
            compact=(OUT/'uj11_mmu_rom.v').read_text()
            self.assertEqual(compact.count('DP8KC #('),6)
            self.assertEqual(compact.count('PDPW8KC #('),3)
            self.assertLess(disabled['microcode_words'],enabled['microcode_words'])
            with self.assertRaises(ValueError):fpp_mode('bogus')
            build('microcode')
            self.assertEqual((OUT/'microcode.mem').read_bytes(),original)
        finally:
            build()

class Microassembler(unittest.TestCase):
    def test_delta_requires_register_plus_or_minus_d(self):
        for op in ('ADD','SUB'):
            assemble(f'alu {op}, a=RS, b=RS, pair=AD, d=STEP, dst=RF, delta=1\nSTOP')
        for text in ('alu ADC, a=R0, b=R0, pair=AD, dst=RF, delta=1',
                     'alu ADD, a=R0, b=R1, pair=AD, dst=RF, delta=1',
                     'alu SUB, a=R0, b=R0, pair=AB, dst=RF, delta=1',
                     'alu ADD, a=R0, b=R0, pair=AD, dst=OPERAND, delta=1'):
            with self.subTest(text=text), self.assertRaises(AssemblyError):assemble(text)

    def test_high_target_and_immediate_do_not_overlap_flags(self):
        image,_,_,_=assemble('''JUMP, target=HIGH
            .org $700
            HIGH: alu PASSA, pair=DA, d=IMM, imm=0xcafe, flags=NZVC, uflags=1
            STOP
        ''')
        self.assertEqual((image[0]>>36 & 1)*1024+(image[0]>>11 & 1023),0x700)
        w=image[0x700]
        self.assertEqual((w>>45 & 255)*256+(w & 255),0xcafe)
        self.assertEqual(w>>40 & 1,1)

    def test_third_rom_bank_direct_reads_and_wide_operations(self):
        from uj11mmuasm import FPOPS
        image,_,labels,_=assemble("""JUMP, target=HIGH
            .org $aff
            HIGH: READ, space=INTERNAL, address=0x00e8, b=T6, load=1
            FOP, value=X_TO_Z
            STOP
        """)
        w=image[0]
        self.assertEqual(((w>>53&1)<<11)|((w>>36&1)<<10)|(w>>11&1023),0xaff)
        w=image[0xaff]
        self.assertEqual(w>>40&3,3)  # direct load, absolute internal address
        self.assertEqual((w&255)|((w>>45&255)<<8),0xe8)
        w=image[0xb00]
        self.assertEqual(w>>42&1,1)
        self.assertEqual(w&255,FPOPS['X_TO_Z'])
        with self.assertRaises(AssemblyError):assemble('JUMP, target=3072')

    def test_old_vm2_extensions_are_rejected(self):
        for text in ('READ, space=UPPER, target=0','JUMP, service=ENTER, target=0'):
            with self.assertRaises(AssemblyError):assemble(text)

    def test_complete_image_has_no_vm2_spaces(self):
        text='\n'.join((ROOT/p).read_text() for p in MICROCODE)
        image,_,labels,stats=assemble(text)
        self.assertEqual(len(image),3072)
        self.assertEqual(stats['word_bits'],54)
        self.assertEqual(labels['ODT_ENTRY'],0x500)
        self.assertEqual(labels['FPP_ENTRY'],0x700)
        for name in ('S_MFUS','S_MTUS','S_FP','S_VECTOR'):
            self.assertNotIn(name,labels)

if __name__=='__main__':unittest.main()
