#!/usr/bin/env python3
"""CP38 board decode/read-mux candidates from the immutable CP37e board bus."""
import hashlib
import json
import re
import tarfile
from board_common import ROOT
from build_mmu_entry import change


def main():
    out = ROOT/'build/cp38-bus'
    out.mkdir(parents=True, exist_ok=True)
    path = 'boards/hc1200/uj11_board_bus.v'
    with tarfile.open(ROOT/'synth/reports/cp37e/source.tgz') as archive:
        base = archive.extractfile(path).read().decode()
    variants = {'baseline': base}
    prefix = change(base, '''boot_overlay_active && !write && word_address >= BOOT_BASE &&
		word_address <= BOOT_LAST;''', '''boot_overlay_active && !write &&
		word_address[15:9] == BOOT_BASE[15:9];''')
    prefix = change(prefix, "\tlocalparam [15:0] BOOT_LAST = 16'o004777;\n", '')
    prefix = change(prefix, '''word_address[12:0] >= KL11_BASE[12:0] &&
		word_address[12:0] <= 13'o17566;''', 'word_address[12:3] == KL11_BASE[12:3];')
    prefix = change(prefix, '''(word_address[12:0] == SD_BASE[12:0] ||
		 word_address[12:0] == SD_BASE[12:0] + 2);''', 'word_address[12:2] == SD_BASE[12:2];')
    # local_boot_selected already qualifies the high address bits. The ROM
    # value is observable only under that selector, so share its qualification.
    a = prefix.index('\talways @(*) begin\n\t\tcase (word_address)')
    b = prefix.index('\n\tgenerate if (UART_XO2)', a)
    rom = prefix[a:b].replace('case (word_address)', 'case (word_address[6:1])')
    rom = re.sub(r"16'o(000[0-7]{3})(?=:)", lambda m: f"6'o{int(m[1],8)//2:02o}", rom)
    prefix = prefix[:a]+rom+prefix[b:]
    variants['prefix'] = prefix
    a = prefix.index('\tassign rdata = ')
    b = prefix.index('\n\tassign acknowledge', a)
    term = '''(uart_rdata & {16{uart_selected}}) |
		(16'o000031 & {16{maint_selected}}) |
		(ltc_rdata & {16{ltc_selected}}) |
		(panel_rdata & {16{panel_selected}}) |
		(sd_rdata & {16{sd_selected}}) |
		((rk_ds_selected ? 16'o100701 : 16'o000200) & {16{rk_fixed_selected}}) |
		(local_rdata & {16{local_boot_selected}})'''
    variants['split'] = prefix[:a]+'''\twire [15:0] small_rdata = '''+term+''';
	assign rdata = firmware_selected ? boot_program_word :
		fram_selected ? fram_rdata : small_rdata;'''+prefix[b:]
    base_a, base_b = base.index('\tassign rdata'), base.index('\n\tassign acknowledge')
    variants['split-base'] = base[:base_a]+variants['split'][a:variants['split'].index('\n\tassign acknowledge')]+base[base_b:]
    variants['priority'] = prefix[:a]+'''	assign rdata = uart_selected ? uart_rdata :
		maint_selected ? 16'o000031 : ltc_selected ? ltc_rdata :
		panel_selected ? panel_rdata : sd_selected ? sd_rdata :
		rk_fixed_selected ? (rk_ds_selected ? 16'o100701 : 16'o000200) :
		local_boot_selected ? local_rdata : firmware_selected ? boot_program_word :
		fram_selected ? fram_rdata : 16'b0;'''+prefix[b:]
    for name, text in variants.items():
        folder = out/name; folder.mkdir(exist_ok=True)
        (folder/'uj11_board_bus.v').write_text(text)
        # Every sequential block, peripheral instance and side-effect write
        # condition is byte-identical; only the three declared regions differ.
        assert text[text.index('\tassign acknowledge'): ] == base[base.index('\tassign acknowledge'): ]
        assert text[text.index('\tgenerate if (UART_XO2)'):text.index('\tassign rdata')].split('\twire [15:0] small_rdata')[0].split('\t// Decodes are mutually')[0] == base[base.index('\tgenerate if (UART_XO2)'):base.index('\tassign rdata')].split('\t// Decodes are mutually')[0]
    inputs = ['tools/build_board_decode.py', 'tools/build_mmu_entry.py', 'synth/reports/cp37e/source.tgz']
    files = [out/name/'uj11_board_bus.v' for name in variants]
    (out/'inputs.json').write_text(json.dumps(dict(scope='CP38 exact-address decode and read mux; unchanged sequential/peripheral blocks',
        variants=list(variants), inputs_sha256={p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        outputs_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}), indent=2)+'\n')
    print('PASS CP38 build: frozen baseline, prefix, split, split-base and priority; state updates unchanged')


if __name__ == '__main__':
    main()
