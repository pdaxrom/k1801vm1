#!/usr/bin/env python3
"""Place readable FIS source in unused baseline ROM slots; never emit hand hex.

Existing words/labels do not move. Long fall-through blocks are split with
ordinary JUMPs. The microassembler validates the resulting control flow.
"""
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'microasm'))
from uj11asm import assemble, AssemblyError


def link(base, extension):
    old_image, old_listing, old_labels, old_stats = assemble(base)
    used = {int(line.split()[0], 16) for line in old_listing.splitlines()}
    if 0x11 in used:
        raise AssemblyError('FIS entry 011 is occupied')
    code, labels = [], {}
    for raw in extension.splitlines():
        line = raw.split(';')[0].strip()
        if not line:
            continue
        if ':' in line:
            name, line = line.split(':', 1)
            name = name.strip().upper()
            if name in labels or name in old_labels:
                raise AssemblyError(f'duplicate extension label {name}')
            labels[name] = len(code)
            line = line.strip()
        if line:
            if line.startswith('.') or re.search(r'\b(seq\s*=\s*PAGE|CALL|RETURN)\b', line, re.I):
                raise AssemblyError('extension placement accepts no origins, PAGE or CALL/RETURN')
            code.append(line)
    if labels.get('FIS_ENTRY') != 0 or not code[0].upper().startswith('READ,'):
        raise AssemblyError('FIS_ENTRY must be the first, non-fall-through READ')
    blocks, start = [], 0
    for i, line in enumerate(code):
        head = line.split(',')[0].upper()
        if head in ('JUMP', 'READ', 'WRITE', 'TRAP', 'STOP') or re.search(r'\bseq\s*=\s*FETCH\b', line, re.I):
            blocks.append(list(range(start, i+1)))
            start = i+1
    if start != len(code):
        raise AssemblyError('extension falls off its end')
    placement, bridges = {0: 0x11}, {}
    used.add(0x11)
    assert blocks.pop(0) == [0]
    for block in sorted(blocks, key=lambda b: (-len(b), b[0])):
        while block:
            gaps = []
            for i in range(1024):
                if i not in used and (i == 0 or i-1 in used):
                    end = i
                    while end < 1024 and end not in used:
                        end += 1
                    gaps.append((i, end-i))
            fits = [(a, n) for a, n in gaps if n >= len(block)]
            if fits:
                address, _ = min(fits, key=lambda g: (g[1], g[0]))
                take = len(block)
            else:
                address, size = max(gaps, key=lambda g: (g[1], -g[0]))
                if size < 2:
                    raise AssemblyError('extension does not fit the remaining ROM holes')
                take = size - 1
                bridges[address+take] = block[take]
                used.add(address+take)
            for offset, index in enumerate(block[:take]):
                placement[index] = address+offset
                used.add(address+offset)
            block = block[take:]
    names = {}
    for name, index in labels.items():
        if index not in placement:
            raise AssemblyError(f'extension label {name} has no instruction')
        names.setdefault(placement[index], []).append(name)
    lines = ['; Generated placement of microcode/fis.uasm. Edit the source, not this file.']
    words = {placement[i]: instruction for i, instruction in enumerate(code)}
    for address, continuation in bridges.items():
        name = f'FIS_LINK_{continuation}'
        names.setdefault(placement[continuation], []).append(name)
        words[address] = f'JUMP, target={name}, prefetch=0'
    for address, instruction in sorted(words.items()):
        lines.append(f'.org ${address:03x}')
        lines.extend(name+':' for name in names.get(address, []))
        lines.append('    '+instruction)
    placed = '\n'.join(lines)+'\n'
    source = base+'\n'+placed
    image, listing, mapped_labels, stats = assemble(source)
    old_addresses = {int(line.split()[0], 16) for line in old_listing.splitlines()}
    assert all(image[a] == old_image[a] for a in old_addresses)
    assert all(mapped_labels[k] == v for k, v in old_labels.items())
    report = {'baseline_words': old_stats['used_words'], 'source_words': len(code),
              'bridge_words': len(bridges), 'used_words': stats['used_words'],
              'free_words': 1024-stats['used_words'], 'entry': mapped_labels['FIS_ENTRY'],
              'baseline_words_unchanged': True, 'baseline_labels_unchanged': True}
    return source, placed, report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base', type=Path, default=ROOT/'microcode/m0.uasm')
    p.add_argument('--extension', type=Path, default=ROOT/'microcode/fis.uasm')
    p.add_argument('--output', type=Path, default=ROOT/'build/fis-combined.uasm')
    args = p.parse_args()
    source, placed, report = link(args.base.read_text(), args.extension.read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(source)
    args.output.with_suffix('.placed.uasm').write_text(placed)
    args.output.with_suffix('.placement.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
