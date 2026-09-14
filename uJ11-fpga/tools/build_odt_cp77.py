#!/usr/bin/env python3
"""Build CP77 ODT and relocated FP11 as one nonoverlapping FRAM release."""
import json
from board_common import ROOT
from build_software_cp67 import odt
from build_fp11_cp77 import build as fp_build

def build():
    monitor=odt(ROOT/'build/cp77-odt');fp=fp_build()
    assert monitor['symbols']['MEMEND']<=fp['symbols']['INIT']
    # The relocation/header must not silently change the FP numerical engine.
    old=(ROOT/'demos/rt11/service/cp76/FP11.MAC').read_text()
    new=(ROOT/'firmware/fp11/FP11.MAC').read_text()
    assert old[old.index('COLD:'):]==new[new.index('COLD:'):]
    s=fp['symbols'];assert s['DBGH']==0o60010 and s['FEC']==s['FPS']+2 and s['FEA']==s['FPS']+4 and s['ACS']==s['FPS']+6
    print(json.dumps(dict(odt_allocation=monitor['allocation_bytes'],odt_spare=monitor['free_bytes'],
                         fp_allocation=fp['allocation_bytes'],fp_spare=fp['free_halt_bytes']),indent=2))

if __name__=='__main__':build()
