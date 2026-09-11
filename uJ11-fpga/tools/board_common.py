"""Source inventory shared by CP28 full-board simulation and synthesis."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
CORE = ['rtl/uj11_core.v', 'rtl/uj11_decode_rom.v', 'microcode/generated/uj11_decode_table.v', 'rtl/uj11_engine.v', 'rtl/uj11_decode.v', 'rtl/uj11_microseq.v',
        'rtl/uj11_datapath.v', 'rtl/uj11_regfile.v', 'rtl/uj11_alu.v', 'rtl/uj11_psw.v', 'rtl/uj11_mem.v']
BOARD = ['boards/hc1200/uj11_board.v', 'boards/hc1200/uj11_board_bus.v', 'boards/hc1200/uj11_panel.v',
         'boards/hc1200/uj11_board_fram.v', 'boards/hc1200/uj11_tick.v', 'reference/lsi11/spi_byte_service.v',
         'reference/lsi11/wbc_uart_xo2.v', 'microcode/generated/uj11_firmware_rom.v']

# CP50: opt-in retained CP47c prototype. No MMU files in the default board.
MMU = ['rtl/experimental/uj11_mmu_relocate.v', 'rtl/uj11_mmu_apr_ram.v',
       'rtl/uj11_mmu_apr_decode.v', 'rtl/experimental/uj11_mmu_apr_shared.v',
       'rtl/experimental/uj11_mmr0_control.v', 'rtl/experimental/uj11_mmr3.v']


def profile_name(mmu=False):
    return 'mmu' if mmu else 'mmuless'


def profile_flags(mmu=False):
    # `ifdef tests presence, so never pass -DUJ11_MMU=0.
    return ['-DUJ11_MMU'] if mmu else []
