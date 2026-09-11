#!/usr/bin/env python3
"""Verify both preprocessor profiles against frozen gates and real board tests."""
import hashlib
import json
import re
import subprocess
import tarfile
from pathlib import Path
from board_common import ROOT, CORE, BOARD, MMU, profile_flags

OUT = ROOT/'build/cp50-profiles'
RELOCATED = {
    'boards/hc1200/uj11_board.v': 'build/cp44-direct/uj11_board.v',
    'boards/hc1200/uj11_board_bus.v': 'build/cp45-bus/narrow-rom/uj11_board_bus.v',
    'boards/hc1200/uj11_board_fram.v': 'build/cp47-fram/shared-rx/uj11_board_fram.v',
    'rtl/experimental/uj11_mmu_apr_shared.v': 'build/cp39-csr/uj11_mmu_apr_shared.v',
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tokens(text):
    # Preserve strings, escaped identifiers and token boundaries; ignore only
    # comments/whitespace. Character tokens are enough for punctuation here.
    pattern = r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/|\\\S+|[a-zA-Z_$][\w$]*|\d[\w\x27]*|[^\s]'
    return [t for t in re.findall(pattern, text, re.S) if not t.startswith(('//','/*'))]


def preprocessed(path, flags):
    return subprocess.check_output(['iverilog','-g2012','-E','-o','-']+flags+[str(path)], cwd=ROOT).decode()


def equivalence():
    results = []
    for mmu, gate in [(False,'cp40h'), (True,'cp47c')]:
        sources = CORE+BOARD+(MMU if mmu else [])+[
            'boards/hc1200/uj11_microcomp.v', 'microcode/generated/uj11_m0_ebr.v']
        frozen = json.loads((ROOT/f'synth/reports/{gate}/inputs.json').read_text())['files']
        with tarfile.open(ROOT/f'synth/reports/{gate}/source.tgz') as archive:
            for current in sources:
                old = RELOCATED.get(current,current) if mmu else current
                data = archive.extractfile(old).read()
                assert hashlib.sha256(data).hexdigest()==frozen[old], old
                ref = OUT/'reference.v'; ref.write_bytes(data)
                for vendor in (False,True):
                    flags = ['-DSYNTHESIS'] if vendor else []
                    actual = preprocessed(ROOT/current, flags+profile_flags(mmu))
                    expected = preprocessed(ref, flags)
                    assert tokens(actual)==tokens(expected), (gate,current,vendor)
                    results.append(dict(mmu=mmu, synthesis=vendor, source=current,
                        reference=old, reference_sha256=frozen[old]))
    # Even accidentally listing the retained APR wrapper cannot instantiate it
    # without the macro. All other MMU units are excluded from default lists.
    assert not tokens(preprocessed(ROOT/'rtl/experimental/uj11_mmu_apr_shared.v', []))
    return results


def simulate(top, tag, sources, flags=(), verilator=False):
    compiled = ['tb/'+top+'.v']+sources
    if verilator:
        for i, name in enumerate(compiled):
            if name.startswith('reference/'):
                path = OUT/Path(name).name
                path.write_text('/* verilator lint_off WIDTH */\n'+(ROOT/name).read_text()+
                                '\n/* verilator lint_on WIDTH */\n')
                compiled[i] = str(path)
        command = ['verilator','--binary','--timing','-j','4','--top-module',top,
                   '--Mdir',str(OUT/('obj-'+tag))]+list(flags)+compiled
        exe = [str(OUT/('obj-'+tag)/('V'+top))]
    else:
        command = ['iverilog','-g2012','-s',top,'-o',str(OUT/tag)]+list(flags)+compiled
        exe = ['vvp',str(OUT/tag)]
    with (OUT/(tag+'-build.log')).open('w') as log:
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (OUT/(tag+'.log')).open('w') as log:
        subprocess.run(exe,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    passed = [line for line in (OUT/(tag+'.log')).read_text().splitlines() if line.startswith('PASS')]
    assert passed,tag
    print(tag+': '+'; '.join(passed),flush=True)
    return dict(tag=tag,flags=list(flags),pass_lines=passed)


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    inputs = set(CORE+BOARD+MMU+['boards/hc1200/uj11_microcomp.v',
        'microcode/generated/uj11_m0_ebr.v','microcode/generated/m0.mem',
        'microcode/generated/decode.mem','microcode/generated/firmware.mem',
        'tools/check_board_profiles.py','tools/board_common.py','rtl/uj11_rom.v',
        'reference/lsi11/spi_fram_model.v','tb/tb_board_bus.v','tb/tb_board_fram.v',
        'tb/tb_relocate_cpu_cp44.v','tb/tb_relocate_edges_cp44.v'])
    inputs.update('build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR'))
    hashes = {p:digest(ROOT/p) for p in sorted(inputs)}
    compared = equivalence()
    print(f'PASS {len(compared)} source/profile comparisons with archived CP40h/CP47c',flush=True)
    # Explicit source lists catch accidentally unguarded instances and ports.
    runs = [simulate('tb_board_bus','mmuless-bus',BOARD+['reference/lsi11/spi_fram_model.v'],
                     ['-Ptb_board_bus.CHECK_SELECTORS=1'])]
    for mmu in (False,True):
        for divisor in (1,3):
            runs.append(simulate('tb_board_fram',f'fram-{int(mmu)}-{divisor}',
                ['boards/hc1200/uj11_board_fram.v','reference/lsi11/spi_fram_model.v'],
                profile_flags(mmu)+[f'-Ptb_board_fram.CLK_DIV={divisor}']))
    for vendor in (False,True):
        for edges in (False,True):
            top = 'tb_relocate_edges_cp44' if edges else 'tb_relocate_cpu_cp44'
            sources = CORE+BOARD+MMU+['reference/lsi11/spi_fram_model.v']
            flags = profile_flags(True)
            if vendor:
                flags+=['-DUJ11_VENDOR_ROM']
                sources+=['microcode/generated/uj11_m0_ebr.v']+[
                    'build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')]
                if not edges:flags+=['-P'+top+'.WORDS=4']
            else:
                sources+=['rtl/uj11_rom.v']
                if not edges:flags+=['-GWORDS=32']
            runs.append(simulate(top,f'mmu-{int(vendor)}-{int(edges)}',sources,flags,not vendor))
    assert hashes=={p:digest(ROOT/p) for p in hashes}, 'inputs changed during tests'
    report = dict(comparisons=compared,runs=runs,inputs_sha256=hashes,
        test_logs_sha256={str(p.relative_to(ROOT)):digest(p) for p in sorted(OUT.glob('*.log'))},
        cpu_alu_microcode_unchanged=True, default_mmu_sources=0, mmu_complete=False,
        rt11_xm_tested=False)
    (OUT/'verified.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP50 profiles: default without MMU, retained CP47c with UJ11_MMU',flush=True)


if __name__=='__main__':main()
