#!/usr/bin/env python3
"""Check synthesized disk/PAL/keyboard memories with official Lattice models."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from test_mapped_ebr import ROOT, expression, params, props


def cell(text, name):
    match = re.search(r'\(cell ' + re.escape(name) + r'\b', text)
    assert match, name
    return expression(text, match.start())


def pins(text, name):
    result = {}
    labels = dict(re.findall(r'\(instance \(rename (\w+) "([^"]+)"\)', text))
    for match in re.finditer(r'\(net ', text):
        net = expression(text, match.start())
        label = re.match(r'\(net (?:\(rename \w+ "([^"]+)"\)|(\w+))', net)
        label = label[1] or label[2]
        # Synplify may give a mux bit an anonymous N_* net while retaining
        # its vector/bit identity on the driving LUT instance. Resolve that
        # identity from the EDIF connection, never from the RAM pin number.
        if re.fullmatch(r'N_\d+', label):
            drivers = re.findall(r'\(portRef Z \(instanceRef (\w+)\)\)', net)
            if len(drivers) == 1 and drivers[0] in labels:
                candidate = labels[drivers[0]]
                if re.fullmatch(r'\w+\[\d+\]', candidate):
                    label = candidate
        for pin in re.findall(r'\(portRef (\w+) \(instanceRef ' + re.escape(name) + r'\)\)', net):
            assert pin not in result
            result[pin] = label
    return result


def instantiate(edif, parent, name, primitive, bindings, instance_name):
    parent = cell(edif, parent)
    instance = expression(parent, parent.index('(instance ' + name + ' '))
    values = dict(props(cell(edif, primitive)), **props(instance))
    assert values['GSR'] == '"DISABLED"'
    connections = []
    for pin, net in sorted(pins(parent, name).items()):
        if net in ('GND', 'VCC'):
            value = "1'b0" if net == 'GND' else "1'b1"
        elif net in bindings:
            value = bindings[net]
        elif pin.startswith('DO') and re.fullmatch(re.escape(name) + r'_DO[A-B]?\d+', net):
            value = ''
        else:
            vector = re.fullmatch(r'(\w+)\[(\d+)\]', net)
            assert vector and vector[1] in bindings, (name, pin, net)
            value = f'{bindings[vector[1]]}[{vector[2]}]'
        connections.append(f'.{pin}({value})')
    return primitive + '#(\n' + params(values) + '\n) ' + instance_name + '(\n' + ','.join(connections) + ');'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('edif', type=Path)
    parser.add_argument('models', type=Path)
    args = parser.parse_args()
    edif = args.edif.read_text()
    lines = ['`timescale 1ns/1ps', 'module tb_mapped_buffers;',
             'reg clk=0; always #20.833 clk=!clk;',
             "reg gsr=1; GSR GSR_INST(gsr); PUR PUR_INST(1'b1);",
             'reg [11:0] track_ra=0,track_wa=0; reg [16:0] track_data=0; reg track_we=0; wire [16:0] track_q;',
             'reg [4:0] part_ra=0,part_rb=0,part_wa=0; reg [15:0] part_data=0; reg part_we=0;',
             'wire [15:0] part_a,part_b;',
             'reg [8:0] sd_ra=0,sd_wa=0; reg [7:0] sd_data=0; reg sd_we=0; wire [7:0] sd_q;',
             'reg [9:0] pal_address=0; wire [5:0] pal_q; reg [5:0] pal_expected[0:1023];',
             'reg [7:0] key_address=0; wire [7:0] key_q; reg [7:0] key_expected[0:255];',
             'integer i,j,pass;']
    for stripe in range(9):
        lines.append(instantiate(edif, 'bk_fdd_track', f'words_words_0_{stripe}', 'DP8KC',
                     {'clk': 'clk', 'read_address': 'track_ra', 'un1_word_address': 'track_wa',
                      'un1_word_value': 'track_data', 'un1_dirty7_g': 'track_we', 'raw_word': 'track_q'},
                     f'track{stripe}'))
    for name, q, address, net in [('words_words_0_0', 'part_a', 'part_ra', 'left_address'),
                                ('words_1_words_1_0_0', 'part_b', 'part_rb', 'un1_right_q')]:
        lines.append(instantiate(edif, 'bk_sd_partitions', name, 'PDPW8KC',
                     {'clk': 'clk', 'un1_config_store_4_g': 'part_we', 'un1_config_read': 'part_data',
                      'un1_clear_index_1': 'part_wa', net: address, 'left_q': q, 'right_q': q}, q + '_ram'))
    lines.append(instantiate(edif, 'bk_sd_host', 'buffer_buffer_0_0', 'PDPW8KC',
                 {'clk': 'clk', 'un1_receive_store_g': 'sd_we', 'un1_buffer': 'sd_data',
                  'un4_buffer': 'sd_wa', 'un1_buffer_q': 'sd_ra', 'disk_buffer_read': 'sd_q',
                  'un1_buffer_i_m2': 'sd_data', 'un4_buffer_i_m2': 'sd_wa',
                  'un1_buffer_q_i_m2': 'sd_ra'}, 'sd_buffer'))
    lines.append(instantiate(edif, 'bk_pal_encoder', 'sample1_2_0_0', 'SP8KC',
                 {'video_clk': 'clk', 'address': 'pal_address', 'sample1': 'pal_q'}, 'pal_rom'))
    lines.append(instantiate(edif, 'bk_keymap', 'shifted_symbol_2_0_0', 'SP8KC',
                 dict({'clk': 'clk', 'shifted_symbol': 'key_q'},
                      **{f'decoded_20_{bit}': f'key_address[{bit}]' for bit in range(8)}), 'key_rom'))
    key_table = list(range(256))
    source = (ROOT/'rtl/bk_keymap.v').read_text().split('function [7:0] shift_symbol;')[1].split('endfunction')[0]
    for address, value in re.findall(r"8'h([0-9a-f]+): shift_symbol = 8'h([0-9a-f]+);", source):
        key_table[int(address, 16)] = int(value, 16)
    (ROOT/'build/shift-symbols.hex').write_text(''.join(f'{value:02x}\n' for value in key_table))
    lines.append('''
function [16:0] track_pattern(input integer address);
    reg [15:0] data;
    begin data=(address*2705)^16'ha55a;track_pattern={address[0],data}; end
endfunction
initial begin
    $readmemh("build/pal_samples.hex",pal_expected);
    $readmemh("build/shift-symbols.hex",key_expected); #100;
    for(i=0;i<4096;i=i+1)begin
        @(negedge clk);track_wa=i;track_data=track_pattern(i);track_we=1;
    end
    @(negedge clk);track_we=0;
    for(i=0;i<32;i=i+1)begin
        @(negedge clk);part_wa=i;part_data=(i*2705)^16'h5aa5;part_we=1;
    end
    @(negedge clk);part_we=0;
    for(i=0;i<512;i=i+1)begin
        @(negedge clk);sd_wa=i;sd_data=(i*39)^8'ha5;sd_we=1;
    end
    @(negedge clk);sd_we=0;
    for(pass=0;pass<2;pass=pass+1)begin
        for(i=0;i<4096;i=i+1)begin
            @(negedge clk);track_ra=i;@(posedge clk);#2;
            if(track_q!==track_pattern(i))$fatal(1,"Mapped track word/marker %0d got=%h",i,track_q);
        end
        for(i=0;i<32;i=i+1)for(j=0;j<32;j=j+1)begin
            @(negedge clk);part_ra=i;part_rb=j;@(posedge clk);#2;
            if(part_a!==(((i*2705)^16'h5aa5)&16'hffff)||part_b!==(((j*2705)^16'h5aa5)&16'hffff))
                $fatal(1,"Mapped independent descriptor ports %0d/%0d",i,j);
        end
        for(i=0;i<512;i=i+1)begin
            @(negedge clk);sd_ra=i;@(posedge clk);#2;
            if(sd_q!==(((i*39)^8'ha5)&8'hff))$fatal(1,"Mapped SD buffer byte %0d",i);
        end
        for(i=0;i<1024;i=i+1)begin
            @(negedge clk);pal_address=i;@(posedge clk);#2;
            if(pal_q!==pal_expected[i])$fatal(1,"Mapped PAL ROM sample %0d",i);
        end
        for(i=0;i<256;i=i+1)begin
            @(negedge clk);key_address=i;repeat(2)@(posedge clk);#2;
            if(key_q!==key_expected[i])$fatal(1,"Mapped keyboard OUTREG symbol %0d",i);
        end
        gsr=0;#100;gsr=1;#100;
    end
    $display("PASS mapped buffers: 4096 track words+markers, 1024 descriptor read pairs, 512 SD bytes, 1024 PAL samples, 256 shifted symbols, GSR retention");
    $finish;
end
endmodule
''')
    tb = ROOT/'build/tb_mapped_buffers.v'
    tb.write_text('\n'.join(lines)+'\n')
    models = [args.models/name for name in ['SP8KC.v', 'PDPW8KC.v', 'DP8KC.v', 'GSR.v', 'PUR.v']]
    executable = ROOT/'build/mapped-buffers.vvp'
    command = ['iverilog', '-g2012', '-s', 'tb_mapped_buffers', '-o', str(executable), str(tb), *map(str, models)]
    subprocess.run(command, check=True, cwd=ROOT)
    result = subprocess.run(['vvp', str(executable)], cwd=ROOT, capture_output=True, text=True)
    (ROOT/'build/mapped-buffers.log').write_text(result.stdout+result.stderr)
    assert result.returncode == 0 and 'PASS mapped buffers' in result.stdout, result.stdout+result.stderr
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    record = {'passed': True, 'track_words': 4096, 'track_marker_bits': True, 'descriptor_pairs': 1024,
              'sd_bytes': 512, 'pal_samples': 1024, 'shifted_symbols': 256, 'gsr_retention': True,
              'scope': 'Official models with synthesized parameters, INITVALs and pin/bit wiring; no routed SDF',
              'edif_sha256': digest(args.edif), 'test_sha256': digest(Path(__file__)),
              'generated_test_sha256': digest(tb), 'models': {p.name: digest(p) for p in models}}
    (ROOT/'build/results-mapped-buffers.json').write_text(json.dumps(record, indent=2)+'\n')
    print(result.stdout.strip())


if __name__ == '__main__':
    main()
