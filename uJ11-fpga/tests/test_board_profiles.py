"""Keep the HC1200 source inventory independent of the HC7000 I/O processor."""
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from board_common import CORE,BOARD,TOP,sources

class BoardProfiles(unittest.TestCase):
    def test_hc1200_matches_released_source_inventory(self):
        files=json.loads((ROOT/'releases/hc1200/inputs.json').read_text())['files']
        released={p for p in files if p.endswith('.v')}
        self.assertEqual(set(CORE+BOARD+[TOP,'build/hardware/uj11_m0_ebr.v']),released)
        self.assertEqual(sources(),sources('hc1200'))

    def test_selecting_hc7000_does_not_mutate_hc1200(self):
        before=sources('hc1200')
        core,board,top=sources('hc7000-lcd-sram')
        self.assertIn('boards/hc7000/uj11_disk.v',board)
        self.assertIn('build/hc7000-iop/uj11_iop_ram.v',board)
        self.assertEqual(before,sources('hc1200'))
        self.assertFalse(any('iop' in p or 'vendor/serv' in p or 'hc7000' in p for p in before[0]+before[1]+[before[2]]))

if __name__=='__main__':unittest.main()
