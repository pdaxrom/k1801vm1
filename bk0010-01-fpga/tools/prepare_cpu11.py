#!/usr/bin/env python3
"""Make explicit PLM masks for two-state simulation and FPGA synthesis.

Upstream cmp(ai, mi) carries X don't-care bits through a function argument.
Verilator treats that argument as data and loses the pattern's wildcard mask.
Keep the upstream files untouched; derive ordinary masked comparisons.
Copyright of the input: 1801BM1@gmail.com, CC-BY-3.0.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def prepare_registers():
    """Keep cpu11 register timing; use two explicit x9 EBR byte lanes."""
    source = (ROOT / 'vendor/cpu11/vm1_reg.v').read_text()
    image = [0] * 1024
    for address, value in re.findall(r"ram\[\s*(\d+)\] = 16'O([0-7]+);", source):
        image[int(address)] = int(value, 8)
    assert len(re.findall(r"ram\[\s*(\d+)\] = 16'O([0-7]+);", source)) == 64
    body = source[source.index('reg [15:0] ram[63:0];'):]
    body = body.replace('q_a <=', 'model_a <=').replace('q_b <=', 'model_b <=')
    body = body.rsplit('endmodule', 1)[0]
    lines = ['// Generated from unchanged cpu11/vm1_reg.v; original copyright applies.',
        '// Explicit MachXO2 EBR retains synchronous read-before-write on both ports.',
        'module vm1_vcram(input wire [5:0] address_a,address_b,',
        ' input wire [1:0] byteena_a,input wire clock,',
        ' input wire [15:0] data_a,data_b,input wire wren_a,wren_b,',
        ' output wire [15:0] q_a,q_b);',
        '`ifdef SYNTHESIS', '`define BK_VM1_EBR',
        '`elsif BK_VM1_VENDOR_RAM', '`define BK_VM1_EBR', '`endif',
        '''`ifdef BK_VM1_EBR''',
        'wire [15:0] raw_a,raw_b;', 'reg [1:0] cross_a=0,cross_b=0;',
        'always @(posedge clock) begin',
        ' cross_a <= {2{address_a==address_b && !wren_b && wren_a}} & byteena_a;',
        ' cross_b <= {2{address_a==address_b && wren_b}} & ~({2{wren_a}} & byteena_a);',
        'end']
    for lane in range(2):
        lines += ['DP8KC #(.DATA_WIDTH_A(9),.DATA_WIDTH_B(9),',
            '.REGMODE_A("NOREG"),.REGMODE_B("NOREG"),',
            '.CSDECODE_A("0b000"),.CSDECODE_B("0b000"),',
            '.WRITEMODE_A("READBEFOREWRITE"),.WRITEMODE_B("READBEFOREWRITE"),',
            '.GSR("DISABLED"),.RESETMODE("SYNC"),.ASYNC_RESET_RELEASE("SYNC"),',
            '.INIT_DATA("STATIC"),']
        for block in range(32):
            value = sum(((image[block*32 + word] >> (lane*8)) & 255) <<
                        (20*(word//2) + 9*(word % 2)) for word in range(32))
            lines.append(f'.INITVAL_{block:02X}("0x{value:080X}"){"," if block < 31 else ""}')
        lines.append(f') lane_{lane} (')
        ports = []
        for side, suffix in [('A', 'a'), ('B', 'b')]:
            for bit in range(9):
                ports += [(f'DI{side}{bit}', f'data_{suffix}[{lane*8+bit}]' if bit<8 else "1'b0"),
                          (f'DO{side}{bit}', f'raw_{suffix}[{lane*8+bit}]' if bit<8 else '')]
            for bit in range(13):
                val = f'address_{suffix}[{bit-3}]' if 3 <= bit < 9 else ("1'b1" if bit == 0 else "1'b0")
                ports.append((f'AD{side}{bit}', val))
            ports += [(f'CLK{side}', 'clock'), (f'CE{side}', "1'b1"), (f'OCE{side}', "1'b1"),
                      (f'WE{side}', f'wren_a && byteena_a[{lane}]' if side == 'A' else 'wren_b'),
                      (f'RST{side}', "1'b0")]
            ports += [(f'CS{side}{bit}', "1'b0") for bit in range(3)]
        lines += [f'.{name}({value}){"," if i < len(ports)-1 else ""}' for i,(name,value) in enumerate(ports)]
        lines.append(');')
    lines += ['assign q_a = {cross_b[1]?raw_b[15:8]:raw_a[15:8],cross_b[0]?raw_b[7:0]:raw_a[7:0]};',
              'assign q_b = {cross_a[1]?raw_a[15:8]:raw_b[15:8],cross_a[0]?raw_a[7:0]:raw_b[7:0]};',
              '`else', 'reg [15:0] model_a,model_b;', 'assign q_a=model_a; assign q_b=model_b;',
              body, '`endif', '`undef BK_VM1_EBR', 'endmodule', '']
    (ROOT / 'build/vm1_reg_block.v').write_text('\n'.join(lines))


def prepare():
    source = (ROOT / 'vendor/cpu11/vm1_plm.v').read_text()
    pattern = r"cmp\(\{ir, mr\}, \{16'b([01x]{16}), 15'b([01x]{15})\}\)"

    # Explicitly share instruction/state predicates. Splitting their supports
    # retains the same truth table while exposing repeated comparisons to synthesis.
    def factor_module(module):
        predicates = {'ir_hi': {}, 'ir_lo': {}, 'mr_hi': {}, 'mr_lo': {}}
        slices = {'ir_hi': ('ir[15:8]', 8), 'ir_lo': ('ir[7:0]', 8),
                  'mr_hi': ('mr[14:7]', 8), 'mr_lo': ('mr[6:0]', 7)}
        def factored(match):
            terms = []
            for signal, bits in [('ir_hi', match[1][:8]), ('ir_lo', match[1][8:]),
                                 ('mr_hi', match[2][:8]), ('mr_lo', match[2][8:])]:
                if set(bits) == {'x'}:
                    continue
                table = predicates[signal]
                if bits not in table:
                    table[bits] = f'plm_{signal}_{len(table)}'
                terms.append(table[bits])
            return '(' + ' && '.join(terms or ["1'b1"]) + ')'
        result, count = re.subn(pattern, factored, module)
        if not count:
            return result
        declarations = []
        for signal, table in predicates.items():
            expression, width = slices[signal]
            for bits, name in table.items():
                mask = int(''.join('0' if bit == 'x' else '1' for bit in bits), 2)
                value = int(bits.replace('x', '0'), 2)
                declarations.append(f"wire {name} = ({expression} & {width}'h{mask:02x}) == {width}'h{value:02x};")
        # Reuse OR groups consumed by several microcode outputs. Groups have at
        # most four inputs, matching the FPGA LUT, and only replace identical
        # unions in each original sum of products.
        sums = re.findall(r'assign pl\[(\d+)\]\s*=\s*(.*?);', result, re.S)
        if len(sums) != 34:
            raise RuntimeError('Unexpected PLM output count')
        inverted = set()
        for bit, expression in sums:
            expression = expression.strip()
            if expression.startswith('~(') and expression.endswith(')'):
                inverted.add(int(bit))
                expression = expression[2:-1]
            if not re.fullmatch(r"\s*(?:p\[\d+\](?:\s*\|\s*p\[\d+\])*|1'b[01])\s*", expression):
                raise RuntimeError(f'Unexpected PLM sum expression at output {bit}')
        outputs = {int(bit): set(re.findall(r'p\[\d+\]', expression)) for bit, expression in sums}
        join_number = 0
        while True:
            consumers = {}
            for bit, terms in outputs.items():
                for term in terms:
                    consumers[term] = consumers.get(term, 0) | (1 << bit)
            items = sorted(consumers)
            intersections = {consumers[a] & consumers[b] for i, a in enumerate(items) for b in items[i+1:]}
            best = None
            for mask in sorted(intersections):
                frequency = mask.bit_count()
                if frequency < 2:
                    continue
                common = [term for term in items if consumers[term] & mask == mask]
                group = tuple(sorted(common, key=lambda term: (consumers[term].bit_count(), term))[:4])
                score = frequency*(len(group)-1)-3
                candidate = (score, frequency, group)
                if len(group) >= 2 and score > 0 and (best is None or candidate > best):
                    best = candidate
            if best is None:
                break
            group = set(best[2])
            name = f'plm_union_{join_number}'
            join_number += 1
            declarations.append('wire ' + name + ' = ' + ' | '.join(sorted(group)) + ';')
            for terms in outputs.values():
                if group <= terms:
                    terms.difference_update(group)
                    terms.add(name)
        def sum_expression(match):
            terms = outputs[int(match[1])]
            if not terms:
                return match[0]
            expression = ' | '.join(sorted(terms))
            if int(match[1]) in inverted:
                expression = '~(' + expression + ')'
            return 'assign pl[' + match[1] + '] = ' + expression + ';'
        result = re.sub(r'assign pl\[(\d+)\]\s*=\s*(.*?);', sum_expression, result, flags=re.S)
        header = result.index(';') + 1
        return result[:header] + '\n' + '\n'.join(declarations) + result[header:]
    count = len(re.findall(pattern, source))
    parts = re.split(r'(?=module \w+\b)', source)
    result = ''.join(factor_module(part) if part.startswith('module ') else part for part in parts)
    if count != 497:
        raise RuntimeError(f'Unexpected upstream PLM product count: {count}')
    result = re.sub(r'function cmp\b.*?endfunction\s*', '', result, flags=re.S)
    destination = ROOT / 'build/vm1_plm_masked.v'
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('// Generated by tools/prepare_cpu11.py; do not edit.\n' + result)
    prepare_registers()
    return destination


if __name__ == '__main__':
    print(prepare())
