#!/usr/bin/env python3
"""Require the same CPU miter to reject five build-only entry/return faults."""
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from board_common import ROOT
sys.path.insert(0, str(ROOT/'microasm'))
from uj11entryasm import assemble


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    sources = ['tb/tb_mmu_entry_core.v','tb/uj11_ram.v',
               'build/cp35/uj11_core.v','build/cp35/uj11_engine.v','build/cp35/uj11_microseq.v',
               'rtl/experimental/uj11_mmu_entry.v','rtl/uj11_alu.v','rtl/uj11_regfile.v',
               'rtl/uj11_datapath.v','rtl/uj11_psw.v','rtl/uj11_mem.v','rtl/uj11_decode.v',
               'rtl/uj11_decode_rom.v','build/cp35/uj11_decode_table.v','rtl/uj11_rom.v']+[
               f'build/cp35-reference/uj11_{name}_reference.v' for name in ('core','engine','microseq','rom')]
    inputs = sources+['tools/check_mmu_entry_negative.py','microasm/uj11entryasm.py','microasm/uj11asm.py',
                      'build/cp35/entry.uasm','build/cp35/entry.mem','microcode/generated/m0.mem',
                      'microcode/generated/decode.mem']
    hashes = {p: digest(ROOT/p) for p in inputs}
    mutations = [
        ('psw', 'microcode', 'alu PASSA, a=T5, flags=LOAD', 'alu PASSA, a=T5, flags=KEEP', 0, 'PSW restore mismatch'),
        ('link', 'build/cp35/uj11_microseq.v', 'if (fault_redirect) link_valid <= 0;',
         'if (fault_redirect || context_redirect) link_valid <= 0;', 0, 'CALL link mismatch'),
        ('fetch', 'build/cp35/uj11_engine.v', '.guest_advance(advance)',
         '.guest_advance(advance || fetch_capture)', 0, 'guest bus mismatch'),
        ('hold', 'build/cp35/uj11_engine.v', 'bus_fault==0 && !mmu_stall;',
         'bus_fault==0;', 0, 'routine changed state while held'),
        ('byte', 'build/cp35/uj11_engine.v', '!control && !mmu_active && byte_instruction',
         '!control && byte_instruction', 36864, 'helper word flags depend on guest byte opcode')]
    results = []
    for name, target, old, new, first, expected in mutations:
        folder = ROOT/f'build/cp35-negative-{name}'
        folder.mkdir(exist_ok=True)
        tag = 'cp35-negative-'+name
        if target == 'microcode':
            source = (ROOT/'build/cp35/entry.uasm').read_text()
            assert source.count(old) == 1
            image, _, _, _ = assemble(source.replace(old, new))
            rom = folder/'mutant.mem'
            rom.write_text(''.join(f'{w:09x}\n' for w in image))
            target = 'build/cp35/uj11_engine.v'
            original = (ROOT/target).read_text()
            altered = original.replace('build/cp35/entry.mem', str(rom.relative_to(ROOT)))
            assert altered != original
        else:
            original = (ROOT/target).read_text()
            assert original.count(old) == 1, name
            altered = original.replace(old, new)
        path = folder/Path(target).name
        path.write_text(altered)
        with (ROOT/f'build/{tag}-build.log').open('w') as log:
            subprocess.run(['iverilog','-g2012','-Wall','-s','tb_mmu_entry_core',
                            '-Ptb_mmu_entry_core.CASES=1024',f'-Ptb_mmu_entry_core.FIRST={first}',
                            '-o','build/'+tag+'.vvp']+[str(path) if p==target else p for p in sources],
                           cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (ROOT/f'build/{tag}.log').open('w') as log:
            result = subprocess.run(['vvp','build/'+tag+'.vvp'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        log = (ROOT/f'build/{tag}.log').read_text()
        assert result.returncode != 0 and expected in log and 'FATAL' in log, (name, log)
        results.append(dict(mutation=name,returncode=result.returncode,rejected=True,
                            failure=log.strip(),mutant_source_sha256=digest(path)))
        print('PASS entry mutation rejected: '+name,flush=True)
    source = (ROOT/'build/cp35/entry.uasm').read_text()
    assembler_cases = [source.replace('MMU_RETURN', 'MMU_RETURN, target=$020'),
                       source+'\n.org $400\nMMU_RETURN\n',
                       source+'\n.org $1cd\nMMU_RETURN\n']
    for bad in assembler_cases:
        try:
            assemble(bad)
        except ValueError:
            pass
        else:
            raise AssertionError('assembler accepted invalid fields/range/overlap')
    assert hashes == {p: digest(ROOT/p) for p in inputs}
    report = dict(scope='Build-only defects, unchanged production sources and positive candidate',
                  tests=results,assembler_rejections=len(assembler_cases),inputs_sha256=hashes)
    (ROOT/'build/cp35-negative.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS MMU_RETURN assembler: invalid fields, range and overlap rejected',flush=True)


if __name__=='__main__':
    main()
