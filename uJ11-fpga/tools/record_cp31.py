#!/usr/bin/env python3
"""Archive CP31 results; distinguish the board baseline from isolated MMU tests."""
import hashlib
import json
from pathlib import Path
import shutil
import tarfile
from board_common import ROOT


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    synthesis = {}
    for name in ('cp31a', 'cp31b', 'cp31c', 'cp31d'):
        directory = ROOT/'synth/reports'/name
        result = json.loads((directory/'result.json').read_text())
        inputs = result['inputs']['files']
        assert result['fully_routed'] and result['timing_pass'] and result['diamond_returncode'] == 0
        with tarfile.open(directory/'source.tgz') as archive:
            for path, expected in inputs.items():
                data = ((directory/'clock.lpf').read_bytes() if path.startswith('generated:')
                        else archive.extractfile(path).read())
                assert hashlib.sha256(data).hexdigest() == expected, (name, path)
                # Retained production/probe HDL and build generators match fit.
                if name in ('cp31c', 'cp31d') and not path.startswith('generated:'):
                    assert digest(ROOT/path) == expected, (name, path, 'current source differs')
        for suffix, expected in result['reports'].items():
            assert digest(directory/('design'+suffix)) == expected, (name, suffix)
        synthesis[name] = {k: result[k] for k in ('lut4', 'ff', 'ebr', 'slices', 'fmax_mhz', 'timing_pass')}
    previous = json.loads((ROOT/'synth/reports/cp29a/inputs.json').read_text())['files']
    ebr = 'microcode/generated/uj11_m0_ebr.v'
    assert digest(ROOT/ebr) == previous[ebr], 'FIS/integer microstore differs from hardware CP29'
    stats = json.loads((ROOT/'microcode/generated/m0.stats.json').read_text())
    assert stats['used_words'] == 954 and stats['encoding_version'] == 12
    logs = ['cp31-base-tests.log', 'cp31-fis-sync.log', 'cp31-board-units.log',
            'cp31-mmu18.log', 'cp31d-mmu18.log', 'cp31c-board-units.log',
            'cp31c-board-bus-vendor.log', 'cp31c-selectors.log', 'cp31-xm-directory.txt',
            'cp31-xm-bootblock.txt']
    runs = {}
    for name, clocks in (('cp31', 355132188), ('cp31c', 354938300)):
        log = (ROOT/f'build/{name}-board-rt11.log').read_text()
        assert f'PASS uJ11 cold RT-11 boot + DIR: {clocks} clocks' in log
        assert '3270 UART wire bytes, 162 SD reads/6 writes' in log
        manifest = json.loads((ROOT/f'build/{name}-board-inputs.json').read_text())
        assert manifest['image_sha256'] == digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')
        runs[name] = dict(monitor='RT-11FB', clocks=clocks, uart_wire_bytes=3270,
                         sd_reads=162, sd_writes=6, backing_image_sha256=manifest['image_sha256'])
        logs += [f'{name}-board-rt11.log', f'{name}-board-inputs.json', f'{name}-uart.txt']
    assert '1769472 checks' in (ROOT/'build/cp31d-mmu18.log').read_text()
    assert '16777216 combinations' in (ROOT/'build/cp31c-selectors.log').read_text()
    assert '23840 exact state/PSW/bus/memory cases' in (ROOT/'build/cp31-fis-sync.log').read_text()
    assert 'PASS board bus: 30 beats' in (ROOT/'build/cp31c-board-bus-vendor.log').read_text()
    image = ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha = '9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(image) == xm_sha and image.stat().st_size == 27540480
    directory = (ROOT/'build/cp31-xm-directory.txt').read_text()
    for required in ('RT11XM.SYS', 'DMX.SYS', 'VMX.SYS', 'STARTX.BAK'):
        assert required in directory, required
    out = ROOT/'tb/reports/cp31'
    out.mkdir(parents=True, exist_ok=True)
    for name in logs:
        shutil.copyfile(ROOT/'build'/name, out/name)
    sources = ['rtl/uj11_mmu_translate18.v', 'synth/machxo2/uj11_probe_mmu18.v',
               'tb/tb_mmu18.v', 'tb/tb_firmware_rom.v', 'tb/tb_board_bus.v',
               'tb/tb_board_rt11.v', 'tools/record_cp31.py']
    report = dict(
        checkpoint='CP31: remove FP11; RK CSR in EBR; isolated MMU18 datapath',
        date='2026-09-10', board_programmed=False, physical_board_revision='CP29a',
        production_has_mmu=False, fp11_removed=True, fis_retained=True,
        microcode_words=954, microcode_free=70, microcode_width=36, encoding_version=12,
        microstore_matches_cp29=True, synthesis=synthesis,
        tests=dict(integer_cases=12928, python_tests=21, fis_cases=23840, fis_faults=3072,
                   decode_cases_per_rom=65536, decode_holds_per_rom=65536,
                   firmware_storage_reads_and_holds_per_rom=17920,
                   board_beats_per_rom=30, board_selector_combinations=16777216,
                   mmu18_checks_per_variant=1769472, mmu18_variants=2),
        rt11_fb=runs,
        rt11_xm=dict(user_path='disks/rt11v5.3/system.dsk',
                     repository_path='lsi11/disks/rt11v5.3/system.dsk', bytes=image.stat().st_size,
                     sectors=53790, sha256=xm_sha, directory_audited=True, image_modified=False,
                     boot_tested=False, memory_above_64k_tested=False,
                     reason='PAR/PDR tables, MMR state, CPU translation/abort and extended DMA not integrated yet'),
        manual=dict(local_path='../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf',
                    sha256=digest(ROOT/'../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf'),
                    ocr_url='https://dusted.dk/pages/computers/J-11/datasheet-DEC-DCJ11_Microprocessor_Users_Guide_OCR.pdf',
                    ocr_sha256=digest(ROOT/'build/mmu-doc/j11-ocr.pdf')),
        test_sources_sha256={name: digest(ROOT/name) for name in sources},
        archived_logs_sha256={str(p.relative_to(ROOT)): digest(p) for p in sorted(out.iterdir())},
        additional_area_reports_sha256={f'synth/reports/{name}/design.areasrr':digest(ROOT/f'synth/reports/{name}/design.areasrr') for name in ('cp31b','cp31d')},
        limits=['No integrated MMU or RT-11XM claim.',
                'Probe FF and Fmax include measurement harness; not CPU MMU Fmax.',
                'Final board has only 28 LUT / 12 slices / 1 EBR free; full MMU fit unproven.',
                'PAR/PDR/MMR, restart, mode/SP/I-D and extended RK DMA remain future gates.',
                'No external pin-delay or OSCH worst-case tolerance timing closure claim.'])
    (ROOT/'docs/verification-cp31.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(synthesis=synthesis, log_files=len(logs), xm_boot_tested=False), indent=2))


if __name__ == '__main__':
    main()
