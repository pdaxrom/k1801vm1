#!/usr/bin/env python3
"""Private CP37 extensions to v12; no change to the production assembler.

APR_READ uses JUMP bit5, bit6 chooses PDR and A=24 selects saved memory A.
Bit4 still disables prefetch. D=0 means APR data only inside the helper.
"""
import importlib.util
from pathlib import Path


def assemble(source):
    spec = importlib.util.spec_from_file_location('uj11apr_base', Path(__file__).with_name('uj11asm.py'))
    base = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(base)
    native = base.encode

    def encode(line, address, labels):
        fields = [s.strip() for s in line.split(',')]
        if fields[0].upper() == 'MMU_RETURN':
            if len(fields) != 1:
                raise base.AssemblyError('MMU_RETURN accepts no fields')
            word, edges = native('JUMP, target=$3ff, prefetch=0', address, labels)
            return word | 2, edges
        if fields[0].upper() == 'APR_READ':
            args = {}
            for field in fields[1:]:
                key, sep, value = field.partition('=')
                key = key.strip().lower()
                if not sep or key in args:
                    raise base.AssemblyError('APR_READ requires unique kind and target')
                args[key] = value.strip()
            if set(args) != {'kind', 'target'} or args['kind'].upper() not in ('PAR', 'PDR'):
                raise base.AssemblyError('APR_READ requires kind=PAR/PDR, target=label')
            word, edges = native('JUMP, target='+args['target']+', prefetch=0', address, labels)
            return word | (24 << 26) | 32 | (64 if args['kind'].upper() == 'PDR' else 0), edges
        # Keep the alias local to an explicit D input, never a textual rewrite
        # of arbitrary labels. Native validation still checks all other fields.
        alias = [i for i, f in enumerate(fields) if f.lower().replace(' ', '') == 'd=apr']
        if alias:
            if len(alias) != 1 or not fields[0].lower().startswith('alu '):
                raise base.AssemblyError('APR is an ALU D source')
            fields[alias[0]] = 'd=ZERO'
        return native(', '.join(fields), address, labels)

    base.encode = encode
    image, listing, labels, stats = base.assemble(source)
    stats['experimental_extension'] = 'CP37 APR_READ + private D=APR + CP35 MMU_RETURN'
    return image, listing, labels, stats
