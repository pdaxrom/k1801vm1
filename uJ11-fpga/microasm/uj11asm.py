#!/usr/bin/env python3
"""uJ11 semi-horizontal backend v13. Private FP state cycles and reset hook; v12 datapath preserved."""
import argparse
import json
import re
from pathlib import Path

OPS = {s: i for i, s in enumerate(
    'PASSA PASSB ADD ADC SUB SBC AND OR XOR BIC NOTA LSL LSR ASR ROL ROR'.split())}
COMMANDS = {s: i for i, s in enumerate(
    'JUMP CJUMP FETCH RETURN DISPATCH OR_MS OR_MD OR_RR OR_BT OR_R67 CALL READ WRITE STOP WAIT TRAP'.split())}
PAIRS = {s: i for i, s in enumerate('AB AQ AD DB ZB ZQ DA BA'.split())}
PAIRS['DQ'] = PAIRS['ZQ']  # v12: D+Q; legacy ZQ explicitly requires D=ZERO.
DESTS = {s: i for i, s in enumerate('NONE RF Q RFQ_L RFQ_R OPERAND MOV'.split())}
FLAGS = {s: i for i, s in enumerate('KEEP NZV NZVC LOAD'.split())}
DINPUT = {s: i for i, s in enumerate('ZERO ONE TWO STEP MDR DISP IMM PSW'.split())}
SEQS = {'NEXT': 0, 'PAGE': 1, 'FETCH': 2, 'FETCH_A1': 3}
CONDS = {s: i for i, s in enumerate('ALWAYS C V Z N Q0 LOOPZ ERROR'.split())}
CONDS.update({'NOT_' + k: v + 8 for k, v in list(CONDS.items())})
REGS = {**{f'R{i}': i for i in range(8)}, **{f'T{i}': 8+i for i in range(8)},
        'RS': 16, 'RD': 17, 'RS1': 18, 'RD1': 19}
FETCH_ADDRESS = 0x020
STOP_ADDRESS = 0x3ff
STOP_WORD = (1 << 35) | (COMMANDS['STOP'] << 31)
MASKS = {'OR_MS': 7, 'OR_MD': 7, 'OR_RR': 3, 'OR_BT': 3, 'OR_R67': 1}


class AssemblyError(ValueError):
    pass


def number(s, labels=None):
    s = s.strip().upper()
    if labels is not None and s in labels:
        return labels[s]
    try:
        return int(s[1:], 16) if s.startswith('$') else int(s, 0)
    except ValueError as e:
        raise AssemblyError(f'unknown symbol or number: {s}') from e


def enum(value, table):
    if value.upper() not in table:
        raise AssemblyError(f'invalid value {value}; expected {", ".join(table)}')
    return table[value.upper()]


def bounded(value, bits, name):
    if not 0 <= value < (1 << bits):
        raise AssemblyError(f'{name} outside {bits}-bit range: {value}')
    return value


def fields(parts, allowed):
    result = {}
    for part in parts:
        if '=' not in part:
            raise AssemblyError(f'expected field=value: {part}')
        key, value = (x.strip() for x in part.split('=', 1))
        key = key.lower()
        if key not in allowed:
            raise AssemblyError(f'unknown field {key}')
        if key in result:
            raise AssemblyError(f'duplicate/conflicting field {key}')
        if not value:
            raise AssemblyError(f'empty field {key}')
        result[key] = value
    return result


def encode(line, addr, labels):
    head, *args = [s.strip() for s in line.split(',')]
    headparts = head.upper().split()
    if not headparts:
        raise AssemblyError('missing instruction before fields')
    edges = []
    if headparts[0] == 'ALU':
        if len(headparts) != 2:
            raise AssemblyError('expected alu OP, field=value')
        f = fields(args, {'a','b','pair','dst','flags','d','seq','next','imm','trace'})
        seq = enum(f.get('seq', 'NEXT'), SEQS)
        din = enum(f.get('d', 'ZERO'), DINPUT)
        if f.get('pair', 'AB').upper() == 'ZQ' and din != DINPUT['ZERO']:
            raise AssemblyError('ZQ requires d=ZERO; use DQ for a D/Q operation')
        low = 0
        if seq == SEQS['PAGE']:
            if 'next' not in f or 'imm' in f or din == DINPUT['IMM']:
                raise AssemblyError('PAGE needs next and conflicts with IMM')
            target = bounded(number(f['next'], labels), 10, 'next')
            if target >> 8 != addr >> 8:
                raise AssemblyError('PAGE target crosses page; use control JUMP')
            low = target & 255
            edges.append(target)
        elif 'next' in f:
            raise AssemblyError('next field requires seq=PAGE')
        else:
            edges.append(FETCH_ADDRESS if seq == SEQS['FETCH'] else (addr + 1) & 1023)
            if seq == SEQS['FETCH_A1']:
                edges.append(FETCH_ADDRESS)
        if din == DINPUT['IMM']:
            if 'imm' not in f:
                raise AssemblyError('d=IMM requires imm')
            low = bounded(number(f['imm'], labels), 8, 'imm')
        elif 'imm' in f:
            raise AssemblyError('imm requires d=IMM')
        if 'trace' in f:
            enum(f['trace'], {'RETURN': 1})
            if seq != SEQS['FETCH'] or din == DINPUT['IMM'] or f.get('flags','KEEP').upper() != 'KEEP':
                raise AssemblyError('trace=RETURN requires seq=FETCH, flags=KEEP and non-IMM input')
            low = 1
        word = (enum(headparts[1], OPS) << 31 |
                enum(f.get('a','R0'), REGS) << 26 |
                enum(f.get('b','R0'), REGS) << 21 |
                enum(f.get('pair','AB'), PAIRS) << 18 |
                enum(f.get('dst','NONE'), DESTS) << 15 |
                enum(f.get('flags','KEEP'), FLAGS) << 13 |
                din << 10 | seq << 8 | low)
    else:
        if len(headparts) != 1:
            raise AssemblyError('expected COMMAND, field=value')
        cmd = headparts[0]
        ci = enum(cmd, COMMANDS)
        allowed = {'prefetch'}
        if cmd in {'JUMP','CJUMP','CALL','READ','WRITE','TRAP'} or cmd in MASKS:
            allowed.add('target')
        if cmd == 'CJUMP':
            allowed.add('cond')
        if cmd == 'JUMP':
            allowed.update({'init','fp_init'})
        if cmd in {'READ','WRITE'}:
            allowed.update({'a','b','byte','private'})
        if cmd == 'READ':
            allowed.update({'stream','fault_inc'})
        if cmd == 'OR_R67':
            allowed.add('a')
        f = fields(args, allowed)
        init = bounded(number(f.get('init','0')), 1, 'init')
        if init and bounded(number(f.get('prefetch','1')),1,'prefetch'):
            raise AssemblyError('init requires prefetch=0')
        byte_auto = f.get('byte','0').upper() == 'IR'
        byte_fixed = 0 if byte_auto else bounded(number(f.get('byte','0')), 1, 'byte')
        stream = bounded(number(f.get('stream','0')), 1, 'stream')
        fault_inc = bounded(number(f.get('fault_inc','0')), 1, 'fault_inc')
        private = bounded(number(f.get('private','0')), 1, 'private')
        fp_init = bounded(number(f.get('fp_init','0')),1,'fp_init')
        if fp_init and (init or f.get('target','').upper()!='FETCH'):
            raise AssemblyError('fp_init requires JUMP to FETCH without peripheral init')
        if private and (byte_auto or byte_fixed or stream or fault_inc or
                        bounded(number(f.get('prefetch','1')),1,'prefetch')):
            raise AssemblyError('private READ/WRITE requires word, prefetch=0 and no stream/fault_inc')
        if stream and (f.get('a','R0').upper() not in {'R7','RS','RD'} or byte_fixed):
            raise AssemblyError('stream READ requires a=R7/RS/RD and byte=0/IR')
        if 'target' in allowed and 'target' not in f:
            raise AssemblyError(f'{cmd} requires target')
        if cmd == 'CJUMP' and 'cond' not in f:
            raise AssemblyError('CJUMP requires cond')
        target = bounded(number(f.get('target','0'), labels), 10, 'target')
        if cmd in MASKS:
            mask = MASKS[cmd]
            if target & mask:
                raise AssemblyError(f'{cmd} target overlaps OR bits 0x{mask:x}')
            edges += [target | k for k in range(mask + 1)]
        elif 'target' in f:
            edges.append(target)
        if cmd in {'CJUMP', 'CALL'}:
            edges.append((addr + 1) & 1023)
        a_default = 'R7' if cmd == 'FETCH' else 'R0'
        b_default = 'R7' if cmd == 'FETCH' else 'R0'
        word = (1 << 35 | ci << 31 | target << 11 |
                enum(f.get('cond','ALWAYS'), CONDS) << 7 |
                enum(f.get('a', a_default), REGS) << 26 |
                enum(f.get('b', b_default), REGS) << 21 |
                byte_fixed << 6 | int(byte_auto) << 3 | stream << 5 | fault_inc << 2 | (private | fp_init) << 1 |
                (1-bounded(number(f.get('prefetch','1')),1,'prefetch')) << 4 | init)
        if cmd == 'FETCH':
            # command 2 == ALU ADD; target/condition are unused by FETCH.
            # Reuse those bits as ordinary AD, RF, KEEP, TWO datapath fields.
            word |= PAIRS['AD'] << 18 | DESTS['RF'] << 15 | DINPUT['TWO'] << 10
    return word, edges


def assemble(source):
    labels, instructions = {}, {}
    addr = 0
    for lineno, raw in enumerate(source.splitlines(), 1):
        text = raw.split(';', 1)[0].strip()
        if not text:
            continue
        try:
            if ':' in text:
                name, text = text.split(':', 1)
                name = name.strip().upper()
                if not re.fullmatch(r'[A-Z_][A-Z_0-9]*', name) or name in labels:
                    raise AssemblyError(f'invalid or duplicate label {name}')
                bounded(addr, 10, 'label address')
                labels[name] = addr
                text = text.strip()
                if not text:
                    continue
            if text.lower().startswith('.org '):
                addr = bounded(number(text[5:], labels), 10, 'origin')
                continue
            bounded(addr, 10, 'microaddress')
            if addr in instructions:
                raise AssemblyError(f'overlap at {addr:03x}')
            instructions[addr] = (lineno, text)
            addr += 1
        except AssemblyError as e:
            raise AssemblyError(f'line {lineno}: {e}') from e
    if not instructions:
        raise AssemblyError('empty microcode')
    image = [STOP_WORD] * 1024
    listing = []
    for addr, (lineno, text) in sorted(instructions.items()):
        try:
            word, edges = encode(text, addr, labels)
            for target in edges:
                if target not in instructions:
                    raise AssemblyError(f'control flow to unassembled address {target:03x}')
            image[addr] = bounded(word, 36, 'microinstruction')
        except AssemblyError as e:
            raise AssemblyError(f'line {lineno}: {e}') from e
        listing.append(f'{addr:03x} {word:09x} {lineno:4d}  {text}')
    # A repair executes once after an unsuccessful READ, then traps. Restrict
    # it to a same-register +1/+2 with no flags/control/Q side effects.
    for addr in instructions:
        w = image[addr]
        if w >> 35 and (w >> 31 & 15) == COMMANDS['READ'] and w & 4:
            target = w >> 11 & 1023
            c = image[target]
            a = w >> 26 & 31
            if not (c >> 35 == 0 and c >> 31 & 15 == OPS['ADD'] and
                    c >> 26 & 31 == a and c >> 21 & 31 == a and
                    c >> 18 & 7 == PAIRS['AD'] and c >> 15 & 7 == DESTS['RF'] and
                    c >> 13 & 3 == FLAGS['KEEP'] and c >> 10 & 7 in (DINPUT['TWO'],DINPUT['STEP']) and
                    c & 1023 == 0):
                raise AssemblyError(f'fault_inc at {addr:03x} needs a same-register ADD STEP/TWO continuation without flags or sequencing')
    groups = sorted((addr, name) for name, addr in labels.items())
    routines = {}
    for index, (start, name) in enumerate(groups):
        end = groups[index+1][0] if index+1 < len(groups) else 1024
        routines[name] = {'address': start,
                          'words_until_next_label': sum(start <= a < end for a in instructions)}
    stats = {'encoding_version': 13, 'word_bits': 36, 'physical_words': 1024,
             'used_words': len(instructions), 'occupancy_percent': len(instructions)*100/1024,
             'highest_address': max(instructions), 'routines': routines,
             'note': 'Label spans are static word counts, not dynamic instruction CPI.'}
    return image, '\n'.join(listing) + '\n', labels, stats


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('-o', '--output', type=Path, required=True)
    p.add_argument('--list', dest='listing', type=Path)
    p.add_argument('--labels', type=Path)
    p.add_argument('--stats', type=Path)
    args = p.parse_args()
    try:
        image, listing, labels, stats = assemble(args.source.read_text())
    except (AssemblyError, OSError) as e:
        p.exit(1, f'{args.source}: {e}\n')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(''.join(f'{w:09x}\n' for w in image))
    for path, data in [(args.listing, listing),
                       (args.labels, json.dumps(labels, indent=2, sort_keys=True)+'\n'),
                       (args.stats, json.dumps(stats, indent=2, sort_keys=True)+'\n')]:
        if path:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(data)
    print(f'uJ11: {stats["used_words"]}/1024 microinstructions, 36 bits, '
          f'{stats["occupancy_percent"]:.2f}% occupied')


if __name__ == '__main__':
    main()
