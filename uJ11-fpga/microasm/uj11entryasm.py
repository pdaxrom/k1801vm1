#!/usr/bin/env python3
"""Experimental CP35 backend: native v12 plus the private MMU_RETURN word.

Load a private backend instance; production uj11asm and its images stay intact.
MMU_RETURN is a JUMP to STOP with reserved control bit1 set. Only an active
entry controller overrides that target, so an orphan return fails closed.
"""
import importlib.util
from pathlib import Path


def assemble(source):
    spec = importlib.util.spec_from_file_location('uj11entry_base', Path(__file__).with_name('uj11asm.py'))
    base = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(base)
    native = base.encode

    def encode(line, address, labels):
        if line.split(',')[0].strip().upper().startswith('MMU_RETURN'):
            if line.strip().upper() != 'MMU_RETURN':
                raise base.AssemblyError('MMU_RETURN accepts no fields')
            word, edges = native('JUMP, target=$3ff, prefetch=0', address, labels)
            assert not word & 2
            return word | 2, edges
        return native(line, address, labels)

    base.encode = encode
    image, listing, labels, stats = base.assemble(source)
    stats['experimental_extension'] = 'CP35 control bit1: private MMU_RETURN'
    return image, listing, labels, stats
