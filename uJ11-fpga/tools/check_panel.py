#!/usr/bin/env python3
"""Exercise the actual HC1200 top-level HCMS, RGB, keyboard and HG pins."""
from check_board_units import run
from board_common import CORE, BOARD
if __name__=='__main__':
    run('tb_board_panel','cp29-panel',CORE+BOARD+['rtl/uj11_rom.v','boards/hc1200/uj11_microcomp.v'])
