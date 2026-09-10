#!/usr/bin/env python3
"""CP45 exact CP44 physical bus decode/read-mux area alternatives."""
import hashlib
import json
import re
from board_common import ROOT
from build_mmu_entry import change
from build_direct_relocate_cp44 import adapt as direct_adapt

VARIANTS = ('baseline', 'factored', 'parallel', 'prefix', 'factored-prefix',
            'paired', 'io-mux', 'narrow-rom', 'ram-first', 'firmware-first', 'firmware-ram')
OUT = ROOT/'build/cp45-bus'


def adapt(core, board, variant):
    core, board = direct_adapt(core, board)
    return core, [f'build/cp45-bus/{variant}/uj11_board_bus.v'
                  if p.endswith('/uj11_board_bus.v') else p for p in board]


def main():
    path = 'build/cp44-direct/uj11_board_bus.v'
    base = (ROOT/path).read_text()
    frozen = json.loads((ROOT/'synth/reports/cp44e/inputs.json').read_text())['files'][path]
    assert hashlib.sha256(base.encode()).hexdigest() == frozen, 'CP44 baseline changed'
    variants = {'baseline': base}
    # All I/O read terms share cpu_io_page. Keep the full selects for writes
    # and ACK; factor that common term only in the data cone.
    a = base.index('\twire [15:0] small_rdata')
    b = base.index('\n\tassign rdata', a)
    expr = base[a:b].split(' = ', 1)[1].removesuffix(';')
    local = '(local_rdata & {16{local_boot_selected}})'
    assert expr.endswith(local)
    io = expr[:-len(local)].rstrip().removesuffix('|').rstrip()
    selectors = ('mmr0_selected', 'mmr3_selected', 'uart_selected', 'maint_selected',
                 'ltc_selected', 'panel_selected', 'sd_selected', 'rk_selected')
    declarations = []
    for name in selectors:
        match = re.search(r'wire '+name+r'\s*=\s*cpu_io_page &&\s*(.*?);', base, re.S)
        assert match, name
        declarations.append('    wire read_'+name+' = '+match[1]+';')
        io = re.sub(r'\b'+name+r'\b', 'read_'+name, io)
    for name in ('rk_cs1_selected', 'rk_ds_selected', 'rk_fixed_selected'):
        match = re.search(r'wire '+name+r'\s*=\s*(.*?);', base, re.S)
        assert match, name
        expression = match[1]
        for n in ('rk_selected', 'rk_cs1_selected', 'rk_ds_selected'):
            expression = re.sub(r'\b'+n+r'\b', 'read_'+n, expression)
        declarations.append('    wire read_'+name+' = '+expression+';')
        io = re.sub(r'\b'+name+r'\b', 'read_'+name, io)
    variants['factored'] = base[:a]+'\n'.join(declarations)+'''
    wire [15:0] io_rdata = '''+io+''';
    wire [15:0] small_rdata = (io_rdata & {16{cpu_io_page}}) |
        (local_rdata & {16{local_boot_selected}});'''+base[b:]
    old = '''assign rdata = apr_selected ? apr_data : firmware_selected ? boot_program_word :
		fram_selected ? fram_rdata : small_rdata;'''
    variants['parallel'] = change(base, old, '''assign rdata = (apr_data & {16{apr_selected}}) |
        (boot_program_word & {16{firmware_selected}}) |
        (fram_rdata & {16{fram_selected}}) | small_rdata;''')

    def prefix(text):
        text = change(text, '''virtual_address>=SERVICE_BASE && virtual_address<=SERVICE_RTI+1''',
                      '''virtual_address[15:9]==SERVICE_BASE[15:9] &&
        (!virtual_address[8] || virtual_address[7:6]==0)''')
        text = change(text, '''word_address >= BOOT_BASE &&
		word_address <= BOOT_LAST''', 'word_address[15:9] == BOOT_BASE[15:9]')
        # The factored form has a second copy of read-only low-address cones.
        text = text.replace('''word_address[12:0] >= KL11_BASE[12:0] &&
		word_address[12:0] <= 13'o17566''', 'word_address[12:3] == KL11_BASE[12:3]')
        text = text.replace('''(word_address[12:0] == SD_BASE[12:0] ||
		 word_address[12:0] == SD_BASE[12:0] + 2)''', 'word_address[12:2] == SD_BASE[12:2]')
        return text
    variants['prefix'] = prefix(base)
    variants['factored-prefix'] = prefix(variants['factored'])
    best = variants['factored-prefix']
    variants['paired'] = change(best, old, '''wire [15:0] ebr_rdata = (apr_data & {16{apr_selected}}) |
        (boot_program_word & {16{firmware_selected}});
    assign rdata = (apr_selected || firmware_selected) ? ebr_rdata :
        fram_selected ? fram_rdata : small_rdata;''')
    variants['io-mux'] = change(best, '''(io_rdata & {16{cpu_io_page}}) |
        (local_rdata & {16{local_boot_selected}})''',
        '''cpu_io_page ? io_rdata : (local_rdata & {16{local_boot_selected}})''')
    a = best.index('\talways @(*) begin\n\t\tcase (word_address)')
    b = best.index('\n\tgenerate if (UART_XO2)', a)
    rom = best[a:b].replace('case (word_address)', 'case (word_address[6:1])')
    rom = re.sub(r"16'o(000[0-7]{3})(?=:)", lambda m: f"6'o{int(m[1],8)//2:02o}", rom)
    variants['narrow-rom'] = best[:a]+rom+best[b:]
    for name, order in [('ram-first', ('fram', 'firmware', 'apr')),
                        ('firmware-first', ('firmware', 'apr', 'fram')),
                        ('firmware-ram', ('firmware', 'fram', 'apr'))]:
        data = dict(fram='fram_rdata', firmware='boot_program_word', apr='apr_data')
        mux = 'assign rdata = '+' : '.join(f'{n}_selected ? {data[n]}' for n in order)+' : small_rdata;'
        variants[name] = change(variants['narrow-rom'], old, mux)
    outputs = {}
    for variant, text in variants.items():
        # Every state update, ACK and peripheral instance remains identical.
        assert text[text.index('\tassign acknowledge'):] == base[base.index('\tassign acknowledge'):]
        path = OUT/variant/'uj11_board_bus.v'; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text); outputs[str(path.relative_to(ROOT))] = hashlib.sha256(text.encode()).hexdigest()
    inputs = ['tools/build_bus_cp45.py', 'tools/build_mmu_entry.py',
              'build/cp44-direct/uj11_board_bus.v', 'synth/reports/cp44e/inputs.json']
    (OUT/'inputs.json').write_text(json.dumps(dict(variants=list(variants),
        inputs_sha256={p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        outputs_sha256=outputs), indent=2)+'\n')
    print('PASS CP45 build:', len(variants), 'exact-address candidates; unchanged ACK/state/peripheral blocks')


if __name__ == '__main__': main()
