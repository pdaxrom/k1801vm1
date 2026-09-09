#!/usr/bin/env python3
"""Build a read-only vector-boundary audit copy of the original DCJ11 core.

The done hook copies state after the successful vector frame. It neither
replaces ISA logic nor exits the emulator; post-abort C continuation is logged
separately instead of silently treating it as architectural trap entry.
"""
from pathlib import Path
from oracle_core import instrument
ROOT=Path(__file__).resolve().parents[1]
def instrument_faults(source):
    output=instrument(source,True)
    start=output.index('static INLINE void core_take_vector(regs *r, word vec, word old_pc,')
    end=output.index('\nstatic INLINE int dcj11_service_stack_trap',start)
    body=output[start:end]
    needle='    r->r[7] = new_pc;\n}'
    hook='\n    extern void uj11_oracle_vector_done(regs *r, unsigned vector);\n    uj11_oracle_vector_done(r, vec);'
    assert body.count(needle)==1
    body=body.replace(needle,'    r->r[7] = new_pc;'+hook+'\n}')
    result=output[:start]+body+output[end:]
    assert result.replace(hook,'')==output
    return result
if __name__=='__main__':
    (ROOT/'build/oracle_fault_core.c').write_text(instrument_faults((ROOT/'../core/core.c').read_text()))
    print('Fault audit: four address hooks, vector entry and successful frame boundary; original core unchanged')
