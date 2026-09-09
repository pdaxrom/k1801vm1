#!/usr/bin/env python3
"""Add read-only address auditing to a build copy of the existing DCJ11 core.

Internal CPU CSRs bypass the public bus callbacks. Record every memory API
address before this dispatch, so out-of-scope I/O fixtures are excluded too.
No ISA, memory value, flags, or control-flow semantics are replaced.
"""
import re
import argparse
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
def instrument(source, vectors=False):
    pattern=r'(static INLINE (?:byte|word|void) core_(?:load|store)_(?:byte|word)_ex\([^{}]+?\)\n\{)'
    addition='\n    extern void uj11_oracle_access(unsigned address);\n    uj11_oracle_access(offset);'
    output,count=re.subn(pattern,lambda m:m[0]+addition,source)
    if count!=4:raise ValueError(f'Expected exactly four access entry points, found {count}')
    assert output.replace(addition,'')==source
    if vectors:
        pattern=r'(static INLINE void core_take_vector\([^{}]+?\)\n\{)'
        vector_hook='\n    extern void uj11_oracle_vector(unsigned vector);\n    uj11_oracle_vector(vec);'
        output,count=re.subn(pattern,lambda m:m[0]+vector_hook,output)
        if count!=1:raise ValueError(f'Expected one vector entry, found {count}')
        assert output.replace(vector_hook,'').replace(addition,'')==source
    return output


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vectors',action='store_true')
    args=parser.parse_args()
    dest='oracle_control_core.c' if args.vectors else 'oracle_core.c'
    (ROOT/'build'/dest).write_text(instrument((ROOT/'../core/core.c').read_text(),args.vectors))
    print(f'Audited DCJ11 build copy: four address hooks, {int(args.vectors)} vector hook; source unchanged')
