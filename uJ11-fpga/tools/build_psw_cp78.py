#!/usr/bin/env python3
"""CPU-local PSW checkpoint over exact frozen CP67b; microcode unchanged."""
import hashlib
import json
import tarfile
from board_common import ROOT, CORE, BOARD
from build_fram_cp52 import replace_once as rep

OUT = ROOT/'build/cp78-psw'


def build():
    manifest = json.loads((ROOT/'synth/reports/cp67b/inputs.json').read_text())
    inputs = ['tools/build_psw_cp78.py', 'tools/board_common.py', 'tools/build_fram_cp52.py',
              'synth/reports/cp67b/inputs.json', 'synth/reports/cp67b/source.tgz',
              'rtl/cp78/uj11_core.v', 'rtl/cp78/uj11_psw.v']
    outputs = []
    prefix = 'build/cp67-modules/'
    with tarfile.open(ROOT/'synth/reports/cp67b/source.tgz') as archive:
        for name, digest in manifest['files'].items():
            if not name.startswith(prefix) or name == prefix+'inputs.json': continue
            data = archive.extractfile(name).read()
            assert hashlib.sha256(data).hexdigest() == digest, name
            suffix = name[len(prefix):]
            text = data.decode().replace(prefix, 'build/cp78-psw/')
            if suffix in ('src/rtl/uj11_core.v', 'src/rtl/uj11_psw.v'):
                text = (ROOT/'rtl/cp78'/suffix.rsplit('/', 1)[1]).read_text()
            if suffix == 'src/rtl/uj11_engine.v':
                text = rep(text, '    input wire mem_ack, mem_error,',
                           '    input wire mem_ack, mem_error, psw_access,')
                text = rep(text, '    uj11_psw status(', '''    // An explicit PSW destination wins over the instruction's later NZVC.
    // Keep the interlock through multiword flag continuations (e.g. SWAB).
    reg psw_written;
    wire psw_commit = writing && psw_access;
    always @(posedge clk) begin
        if(reset || fault_redirect) psw_written <= 0;
        else if(step) begin
            if(fetching || alu_boundary) psw_written <= 0;
            if(psw_commit) psw_written <= 1;
        end
    end
    uj11_psw status(''')
                text = rep(text, '.enable(step),.update(flags),',
                           '.enable(step),.update(psw_written && flags!=3 ? 2\'d0 : flags),')
                text = rep(text, '.nzvc(nzvc),.value(result),.psw(psw));',
                           '.nzvc(nzvc),.value(result),.psw(psw),\n'
                           '        .bus_write(psw_commit),.bus_byte(mem_byte),.bus_odd(read_a[0]),.bus_value(read_b));')
            path = OUT/suffix
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
            outputs.append(str(path.relative_to(ROOT)))
    sha = lambda p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest()
    record = dict(reference='cp67b', mmu=False, microcode_changed=False, expected_ebr=7,
                  inputs={p:sha(p) for p in inputs}, outputs={p:sha(p) for p in outputs})
    (OUT/'inputs.json').write_text(json.dumps(record, indent=2)+'\n')
    return record


def adapt():
    build()
    return ([str((OUT/'src'/p).relative_to(ROOT)) for p in CORE],
            [str((OUT/'src'/p).relative_to(ROOT)) for p in BOARD+['boards/hc1200/uj11_button.v']])


if __name__ == '__main__': print(json.dumps(build(), indent=2))
