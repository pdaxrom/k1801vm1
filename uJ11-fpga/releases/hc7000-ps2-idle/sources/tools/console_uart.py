"""Generate the HC7000 input merger without editing the HC1200 UART."""

def console_uart(source):
    replacements = [
        ('module wbc_uart_xo2 #(parameter REFCLK=29560000)',
         'module uj11_console_uart #(parameter REFCLK=29560000, LOCAL_ENABLE=0)'),
        ('\tinput wire rx_dat_i,', '\tinput wire rx_dat_i,\n'
         '\tinput wire local_valid,\n\tinput wire [7:0] local_data,\n\toutput wire local_ready,'),
        ('\tassign rx_dtr_o = rx_full;', '\tassign rx_dtr_o = rx_full;\n'
         '\t// Serial reception has priority; never replace an unread RBUF.\n'
         '\tassign local_ready = LOCAL_ENABLE && !wb_rst_i && !rx_full && !rx_busy &&\n'
         '\t\trx_sync == 2\'b11 && !rx_buffer_read;'),
        ('\t\t\tif (!rx_busy) begin\n\t\t\t\tif (!rx_data) begin',
         '\t\t\tif (local_valid && local_ready) begin\n'
         '\t\t\t\trx_full <= 1;\n\t\t\t\trx_buffer <= local_data;\n'
         '\t\t\t\trx_overflow <= 0;\n\t\t\t\trx_break <= 0;\n'
         '\t\t\tend\n\n\t\t\tif (!rx_busy) begin\n\t\t\t\tif (!rx_data) begin')]
    for old, new in replacements:
        assert source.count(old) == 1, old
        source = source.replace(old, new)
    return '// Generated HC7000-only extension; do not edit.\n' + source
