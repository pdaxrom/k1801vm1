"""Production source inventory for the HC1200 computer; no archive extraction."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
CORE = ['rtl/uj11_core.v', 'rtl/uj11_decode_rom.v', 'build/hardware/uj11_decode_table.v',
        'rtl/uj11_engine.v', 'rtl/uj11_decode.v', 'rtl/uj11_microseq.v',
        'rtl/uj11_datapath.v', 'rtl/uj11_regfile.v', 'rtl/uj11_alu.v',
        'rtl/uj11_psw.v', 'rtl/uj11_mem.v']
BOARD = ['boards/hc1200/'+n+'.v' for n in (
    'uj11_board', 'uj11_board_bus', 'uj11_panel', 'uj11_board_fram', 'uj11_tick', 'uj11_button')]
BOARD += ['rtl/peripherals/spi_byte_service.v', 'rtl/peripherals/wbc_uart_xo2.v',
          'build/hardware/uj11_firmware_rom.v']
TOP = 'boards/hc1200/uj11_microcomp.v'
