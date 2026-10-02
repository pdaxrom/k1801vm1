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

PROFILES = ('hc1200', 'hc7000-lcd-sram')


def sources(profile='hc1200'):
    """Explicit board inventory; legacy imports/defaults remain HC1200."""
    if profile == 'hc1200':
        return CORE.copy(), BOARD.copy(), TOP
    if profile != 'hc7000-lcd-sram':
        raise ValueError(f'Unknown board: {profile}')
    core = [p.replace('build/hardware/', 'build/hc7000-hardware/') for p in CORE]
    board = ['boards/hc7000/'+n+'.v' for n in
             ('uj11_board', 'uj11_board_bus', 'uj11_sram', 'uj11_hg_inputs',
              'uj11_sram_arbiter', 'uj11_disk', 'uj11_rk611', 'uj11_sector_engine')]
    board += ['boards/hc1200/'+n+'.v' for n in ('uj11_panel', 'uj11_tick', 'uj11_button')]
    board += ['rtl/peripherals/spi_byte_service.v', 'rtl/peripherals/wbc_uart_xo2.v',
              'build/hc7000-hardware/uj11_firmware_rom.v', 'build/hc7000-iop/uj11_iop_ram.v',
              'build/hc7000-iop/uj11_sector_ram.v']
    board += ['vendor/serv/rtl/'+name+'.v' for name in (
        'serv_bufreg', 'serv_bufreg2', 'serv_alu', 'serv_csr', 'serv_ctrl',
        'serv_decode', 'serv_immdec', 'serv_mem_if', 'serv_rf_if', 'serv_rf_ram_if',
        'serv_rf_ram', 'serv_state', 'serv_debug', 'serv_top', 'serv_rf_top',
        'serv_aligner', 'serv_compdec')]
    return core, board, 'boards/hc7000/uj11_microcomp.v'
