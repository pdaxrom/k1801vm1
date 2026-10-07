"""Terak selects its own runners and cannot overwrite the ordinary CPU ROM."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from build_mmu import build, OUT


class TerakProfiles(unittest.TestCase):
    def make(self, *args):
        env = {k: v for k, v in os.environ.items() if not k.startswith('UJ11_') and k not in
               ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'MAKELEVEL', 'MACHINE', 'BOARD', 'CPU',
                'FPP', 'IOP', 'OUT', 'MMU_CLOCK_MHZ', 'HC7000_VIDEO', 'HC7000_TERMINAL',
                'HC7000_KEYBOARD', 'HC7000_DIAGNOSTICS')}
        return subprocess.run(['make', '-n', *args], cwd=ROOT, env=env, capture_output=True, text=True)

    def test_native_targets_and_default(self):
        for goal, runner in (('hardware', 'build_terak.py'), ('sd-image', 'build_terak_card.py'),
                             ('sd-image-mbr', 'build_terak_card.py'),
                             ('test', 'test_terak.py'), ('prepare-synthesis', 'synthesis_terak.py'),
                             ('synthesis', 'synthesis_terak.py'), ('export', 'export_jed.py')):
            result = self.make('MACHINE=terak', goal)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(runner, result.stdout)
            self.assertNotIn('build_mmu_board.py', result.stdout)
            self.assertNotIn('test_mmu_board.py', result.stdout)
        self.assertIn('build_terak.py', self.make('MACHINE=terak').stdout)
        self.assertIn('build_hardware.py', self.make().stdout)
        self.assertIn('hc7000-terak-mbr-synthesis', self.make('MACHINE=terak', 'export').stdout)
        self.assertIn('custom-terak', self.make('MACHINE=terak', 'TERAK_SYNTHESIS=custom-terak', 'export').stdout)

    def test_conflicting_profiles_and_runners_rejected(self):
        for choice in ('CPU=mmuless', 'FPP=microcode', 'IOP=legacy', 'BOARD=hc1200',
                       'MMU_CLOCK_MHZ=24', 'HC7000_VIDEO=0', 'HC7000_KEYBOARD=0',
                       'HC7000_TERMINAL=1', 'test-rt11', 'test-storage', 'software'):
            self.assertNotEqual(self.make('MACHINE=terak', choice).returncode, 0, choice)

    def test_isolated_cpu_generation(self):
        before = {p: p.read_bytes() for p in OUT.glob('*') if p.is_file()}
        (ROOT/'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT/'build', prefix='test-terak-cpu-') as directory:
            target = Path(directory)
            result = build('off', target)
            self.assertEqual(result['microcode_ebr'], 6)
            self.assertIn(str(target.relative_to(ROOT)/'microcode.mem'),
                          (target/'uj11_mmu_rom.v').read_text())
        self.assertEqual(before, {p: p.read_bytes() for p in OUT.glob('*') if p.is_file()})
