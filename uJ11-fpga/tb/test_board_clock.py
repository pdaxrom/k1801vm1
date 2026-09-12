"""Actual CP59a exposes MAP's OSCH frequency substitution; reject that gate."""
import copy
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from report_synthesis import extract,check_clock


class BoardClock(unittest.TestCase):
    def setUp(self):
        folder=ROOT/'synth/reports/cp59a'
        self.trace=(folder/'design.twr').read_text()
        self.report=extract((folder/'design.mrp').read_text(),self.trace,(folder/'design.par').read_text())

    def test_actual_nominal_pass(self):
        self.assertTrue(check_clock(copy.deepcopy(self.report),self.trace,29.56)['timing_pass'])

    def test_map_substitution_rejected(self):
        r=check_clock(copy.deepcopy(self.report),self.trace,31.824)
        self.assertFalse(r['constraint_matched']);self.assertFalse(r['timing_pass'])
        self.assertTrue(r['trace_timing_pass'])

    def test_missing_or_mixed_clock_rejected(self):
        for text in ('',self.trace+'\nPreference: FREQUENCY NET "clk" 31.824 MHz ;'):
            self.assertFalse(check_clock(copy.deepcopy(self.report),text,31.824)['timing_pass'])

    def test_frequency_limit_cannot_be_ignored(self):
        r=copy.deepcopy(self.report);r['fmax_mhz']=29.0
        self.assertFalse(check_clock(r,self.trace,29.56)['timing_pass'])


if __name__=='__main__':unittest.main()
