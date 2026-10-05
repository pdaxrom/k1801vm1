#!/usr/bin/env python3
"""Verify synthesized ROM INITVALs and line EBR using installed Lattice models."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def expression(text, start):
    depth = 0
    for token in re.finditer(r'"[^"\\]*(?:\\.[^"\\]*)*"|[()]', text[start:]):
        if token[0] == '(':
            depth += 1
        elif token[0] == ')':
            depth -= 1
            if depth == 0:
                return text[start:start+token.end()]
    raise ValueError('Incomplete EDIF expression')


def props(text):
    return {key: value for key, value in re.findall(
        r'\(property (\w+) \((?:integer|string) ("[^"\n]*"|\d+)\)\)', text)}


def params(values):
    return ',\n'.join(f'.{key}({value})' for key, value in sorted(values.items()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('edif', type=Path)
    parser.add_argument('models', type=Path, help='Diamond cae_library/simulation/verilog/machxo2')
    args = parser.parse_args()
    edif = args.edif.read_text()
    sp_defaults = props(expression(edif, edif.index('(cell SP8KC ')))
    pdp_defaults = props(expression(edif, edif.index('(cell PDPW8KC ')))
    boot = expression(edif, edif.index('(cell bk_boot_rom '))
    instances = re.findall(r'\(instance (data_2_0_\d+) ', boot)
    assert sorted(instances) == [f'data_2_0_{n}' for n in range(8)]
    lines = ['`timescale 1ns/1ps', 'module tb_mapped_ebr;',
             'reg clk=0, video_clk=0; always #20.833 clk=!clk; always #7.8125 video_clk=!video_clk;',
             'reg gsr=1; GSR GSR_INST(gsr); PUR PUR_INST(1\'b1);',
             'reg [11:0] address=0; wire [15:0] q; reg [15:0] expected[0:4095];',
             'reg [5:0] wa=0, ra=0; reg we=0; reg [15:0] data=0; wire [15:0] line_q; integer i;']
    for n, name in enumerate(sorted(instances)):
        instance = expression(boot, boot.index('(instance '+name+' '))
        values = dict(sp_defaults, **props(instance))
        assert values['DATA_WIDTH'] == '2' and values['GSR'] == '"DISABLED"'
        connections = {f'DI{b}': "1'b0" for b in range(9)}
        connections.update({f'DO{b}': f'q[{n*2+b}]' if b < 2 else '' for b in range(9)})
        connections.update({f'AD{b}': f'address[{b-1}]' if b else "1'b0" for b in range(13)})
        connections.update(CLK='clk', CE="1'b1", OCE="1'b1", WE="1'b0", RST="1'b0",
                           CS0="1'b0", CS1="1'b0", CS2="1'b0")
        # Check output bit wiring against the actual EDIF, rather than assuming
        # the compiler's instance suffix gives the original bit order.
        for b in range(2):
            net = expression(boot, boot.index(f'(net (rename boot_data_{n*2+b} '))
            assert f'(portRef DO{b} (instanceRef {name}))' in net
            assert f'(portRef (member boot_data {15-n*2-b}))' in net
        lines.append('SP8KC #(\n'+params(values)+'\n) rom'+str(n)+'(\n'+
                     ','.join(f'.{pin}({value})' for pin, value in connections.items())+');')
    start = edif.index('(instance line_ram_line_ram_0_0 ')
    values = dict(pdp_defaults, **props(expression(edif, start)))
    assert values['DATA_WIDTH_R'] == '18' and values['GSR'] == '"DISABLED"'
    connections = {f'DI{b}': f'data[{b}]' if b < 16 else "1'b0" for b in range(18)}
    connections.update({f'DO{b}': '' for b in range(18)})
    video = expression(edif, edif.index('(cell bk_pal_video_'))
    # PDPW8KC's 18-bit read mode returns the two nine-bit banks in a different
    # pin order. Use the compiler's actual output wiring, including padding.
    for b in range(16):
        net = expression(video, video.index(f'(net (rename packed_word_{b} '))
        pin = re.search(r'\(portRef (DO\d+) \(instanceRef line_ram_line_ram_0_0\)\)', net)[1]
        connections[pin] = f'line_q[{b}]'
        net = expression(video, video.index(f'(net (rename mem_read_{b} '))
        assert f'(portRef DI{b} (instanceRef line_ram_line_ram_0_0))' in net
    connections.update({f'ADW{b}': f'wa[{b}]' if b < 6 else "1'b0" for b in range(9)})
    connections.update({f'ADR{b}': f'ra[{b-4}]' if 4 <= b < 10 else "1'b0" for b in range(13)})
    connections.update(CLKW='clk', CLKR='video_clk', CEW='we', CER="1'b1", OCER="1'b1",
                       BE0="1'b1", BE1="1'b1", RST="1'b0", CSW0="1'b0", CSW1="1'b0",
                       CSW2="1'b0", CSR0="1'b0", CSR1="1'b0", CSR2="1'b0")
    lines.append('PDPW8KC #(\n'+params(values)+'\n) line_buffer(\n'+
                 ','.join(f'.{pin}({value})' for pin, value in connections.items())+');')
    lines.append('''
initial begin
    $readmemh("build/boot.hex",expected); #100;
    for(i=0;i<4096;i=i+1) begin
        @(negedge clk); address=i; @(posedge clk); #2;
        if(q!==expected[i]) $fatal(1,"Mapped ROM address=%0d got=%h expected=%h",i,q,expected[i]);
    end
    for(i=0;i<64;i=i+1) begin
        @(negedge clk); wa=i; data=(i*2705)^16'ha55a; we=1;
    end
    @(negedge clk); we=0;
    for(i=0;i<64;i=i+1) begin
        @(negedge video_clk); ra=i; @(posedge video_clk); #2;
        if(line_q!==(((i*2705)^16'ha55a)&16'hffff)) $fatal(1,"Mapped line EBR address=%0d got=%h expected=%h",i,line_q,((i*2705)^16'ha55a)&16'hffff);
    end
    gsr=0; #100; gsr=1; #100;
    for(i=0;i<4096;i=i+1) begin
        @(negedge clk); address=i; @(posedge clk); #2;
        if(q!==expected[i]) $fatal(1,"Mapped ROM changed after GSR at %0d",i);
    end
    for(i=0;i<64;i=i+1) begin
        @(negedge video_clk); ra=i; @(posedge video_clk); #2;
        if(line_q!==(((i*2705)^16'ha55a)&16'hffff)) $fatal(1,"Mapped line EBR changed after GSR at %0d",i);
    end
    $display("PASS mapped EBR: 4096 ROM words, 64 dual-clock line words, retained through GSR");
    $finish;
end
endmodule
''')
    tb = ROOT/'build/tb_mapped_ebr.v'
    tb.write_text('\n'.join(lines)+'\n')
    models = [args.models/name for name in ['SP8KC.v', 'PDPW8KC.v', 'DP8KC.v', 'GSR.v', 'PUR.v']]
    executable = ROOT/'build/mapped-ebr.vvp'
    compile_command = ['iverilog', '-g2012', '-s', 'tb_mapped_ebr', '-o', str(executable), str(tb), *map(str, models)]
    subprocess.run(compile_command, check=True, cwd=ROOT)
    result = subprocess.run(['vvp', str(executable)], cwd=ROOT, capture_output=True, text=True)
    (ROOT/'build/mapped-ebr.log').write_text(result.stdout+result.stderr)
    assert result.returncode == 0 and 'PASS mapped EBR' in result.stdout and 'FATAL:' not in result.stdout, result.stdout+result.stderr
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    record = {'passed': True, 'rom_words': 4096, 'line_words': 64, 'gsr_retention': True,
              'scope': 'Official primitive models with synthesized parameters/ROM INITVALs; no post-route SDF simulation',
              'edif_sha256': digest(args.edif), 'boot_hex_sha256': digest(ROOT/'build/boot.hex'),
              'test_sha256': digest(Path(__file__)), 'generated_test_sha256': digest(tb),
              'models': {p.name: digest(p) for p in models}, 'compile_command': compile_command}
    (ROOT/'build/results-mapped-ebr.json').write_text(json.dumps(record, indent=2)+'\n')
    print(result.stdout.strip())


if __name__ == '__main__':
    main()
