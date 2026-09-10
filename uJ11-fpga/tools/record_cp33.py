#!/usr/bin/env python3
"""Archive CP33 APR/CSR results, retaining the exact scope of each experiment."""
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def synthesis(name, current=False):
    directory = ROOT/'synth/reports'/name
    result = json.loads((directory/'result.json').read_text())
    assert result['diamond_returncode'] == 0 and result['fully_routed'] and result['timing_pass']
    assert json.loads((directory/'inputs.json').read_text()) == result['inputs']
    with tarfile.open(directory/'source.tgz') as archive:
        for path, expected in result['inputs']['files'].items():
            generated = path.startswith('generated:')
            data = ((directory/'clock.lpf').read_bytes() if generated else
                    archive.extractfile(path).read())
            assert hashlib.sha256(data).hexdigest() == expected, (name, path)
            if current and not generated:
                assert digest(ROOT/path) == expected, (name, path, 'current differs')
    for suffix, expected in result['reports'].items():
        assert digest(directory/('design'+suffix)) == expected, (name, suffix)
    return {k: result[k] for k in ('lut4', 'ff', 'ebr', 'slices', 'fmax_mhz', 'timing_pass')}


def main():
    fits = {name: synthesis(name, name == 'cp33c') for name in ('cp33a','cp33b','cp33c')}
    production = synthesis('cp31c', True)
    stats = json.loads((ROOT/'microcode/generated/m0.stats.json').read_text())
    assert stats['used_words'] == 954 and stats['encoding_version'] == 12
    logs = ['cp33a-apr.log', 'cp33a-apr-vendor.log', 'cp33c-apr.log',
            'cp33c-apr-vendor.log', 'cp33-decode.log', 'cp33c-lint.log',
            'cp33c-apr-oracle.log', 'cp33c-apr-oracle-vendor.log']
    for name in logs:
        log = (ROOT/'build'/name).read_text()
        if 'oracle' in name:
            expected = '65536 cases / 196608 CSR commands, 4149 translated / 61387 faults'
        elif 'decode' in name:
            expected = '4194304 PA22 bytes, 192 selected'
        elif 'lint' in name:
            assert '%Warning' not in log and '%Error' not in log
            continue
        else:
            expected = '1144373 commands FULL=1'
        assert expected in log, (name, 'missing PASS')
    image = ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha = '9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(image) == xm_sha and image.stat().st_size == 27540480
    out = ROOT/'tb/reports/cp33'
    out.mkdir(parents=True, exist_ok=True)
    for name in logs:
        shutil.copyfile(ROOT/'build'/name, out/name)
    corpus = (ROOT/'build/mmu-apr-oracle.txt').read_bytes()
    assert len(corpus.splitlines()) == 65536
    (out/'mmu-apr-oracle.txt.gz').write_bytes(gzip.compress(corpus, mtime=0))
    sources = ['Makefile','rtl/uj11_mmu_apr.v','rtl/uj11_mmu_apr_ram.v',
               'rtl/uj11_mmu_apr_decode.v','rtl/uj11_mmu_translate.v',
               'tb/tb_mmu_apr.v','tb/tb_mmu_apr_decode.v','tb/tb_mmu_apr_oracle.v',
               'tb/reference_mmu_apr.c','tools/record_cp33.py',
               '../core/core.c','../core/core.h','../core/pdp11_fp.c',
               '../core/hardware.c','../core/hardware.h']
    report = dict(
        checkpoint='CP33: isolated EBR APR storage, physical CSR decode and serial translation test',
        date='2026-09-10', synthesis=fits, production_cp31c=production,
        production_inputs_unchanged=True, production_has_mmu=False,
        board_programmed=False, physical_board_revision='CP29a',
        fp11_removed=True, fis_retained=True, microcode_words=954, microcode_free=70,
        store=dict(physical_pairs=64, architectural_pairs=48, word_width=16,
                   ebrs=1, active_state_ff=3, main_rf_unchanged=True,
                   paired_w_clear=True, software_w_set=False,
                   powerup='FPGA configuration zero init; not a claimed DEC powerup value',
                   reset='abort uncommitted phases; preserve stored data and already committed writes'),
        measured_latency_clocks=dict(read=2, pdr_write=2, mark_written=2, par_write=3,
                                      includes_request_release=False, cpu_cpi_measured=False),
        tests=dict(apr_commands_per_variant=1144373, unit_variants=['cp33a portable','cp33a vendor','cp33c portable','cp33c vendor'],
                   decode_addresses=4194304, decode_hits=192, c_oracle_cases_per_variant=65536,
                   c_oracle_commands_per_variant=196608, c_oracle_successes=4149,
                   c_oracle_faults=61387, c_oracle_variants=['portable','vendor'],
                   strict_final_lint=True, vendor_models_modified=False),
        oracle=dict(source='../core/core.c', model='DCJ11', enable_mmu=1,
                    helpers=['mmu_io_write_word','mmu_io_write_byte','mmu_io_read_word',
                             'mmu_note_write_pdrw','translate_va_ex'],
                    compared='PAR/PDR readback, lane writes, explicit W actions, successful PA and read-fault acceptance',
                    corpus_sha256=hashlib.sha256(corpus).hexdigest()),
        rt11_xm=dict(path='lsi11/disks/rt11v5.3/system.dsk', sha256=xm_sha,
                     image_modified=False, boot_tested=False,
                     reason='No CPU/MMR/abort-restart or extended RK DMA integration'),
        manual=dict(path='../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf',
                    sha256=digest(ROOT/'../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf'),
                    sections=['4.5.1','4.5.2','4.9']),
        sources_sha256={p: digest(ROOT/p) for p in sources},
        vendor_sha256={p: digest(ROOT/'build/vendor'/p) for p in ('DP8KC.v','GSR.v','PUR.v')},
        archived_tests_sha256={str(p.relative_to(ROOT)): digest(p) for p in sorted(out.iterdir())},
        archived_synthesis_sha256={str(p.relative_to(ROOT)): digest(p)
                                  for name in fits for p in sorted((ROOT/'synth/reports'/name).iterdir())},
        limits=['Probe counts include stimulus/checksum FF and logic; not integrated MMU fit or CPU Fmax.',
                'CSR index covers K/S/U I/D tables; processor modes, SP banks and automatic selection not implemented.',
                'mark_written is an explicit command; automatic W timing/abort policy is not implemented.',
                'Serial translation test uses testbench PAR/PDR latches, not a synthesized CPU MMU pipeline.',
                'CSR privilege, odd-word priority, MMR0/1/2/3, abort/restart and physical DMA remain unimplemented.',
                'Production still CP31c: 28 LUT / 12 slices / 1 EBR free; additional logic needs measured area reduction.',
                'No RT-11XM or FPGA programming in CP33.'])
    (ROOT/'docs/verification-cp33.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(synthesis=fits, production_unchanged=True, xm_boot_tested=False), indent=2))


if __name__ == '__main__':
    main()
