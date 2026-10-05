#!/usr/bin/env python3
"""Package a routed, tested BK build; hardware verification remains explicit."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

from diamond_report import check, jed_record
from prepare_sd_files import prepare

ROOT = Path(__file__).resolve().parents[1]
GROUPS = ('audio','models','fast','reference','fdd','storage','profiles','bios','bios-logical','bios-ps2','ebr-bios',
          'soc','plm','mapped-ebr','mapped-buffers')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package(project, destination):
    project, destination = Path(project).resolve(), Path(destination).resolve()
    result = check(project)
    for name, expected in result['sources'].items():
        current = ROOT/name
        if current.is_file() and digest(current) != expected:
            raise ValueError(f'Routed source differs from workspace: {name}')
    jed = project/'impl1/bk0010_impl1.jed'
    if not jed.exists():
        jed = project/'bk0010_impl1.jed'
    result['jed'] = jed_record(jed)
    reports = {}
    logs = {}
    omitted_logs = []
    for group in GROUPS:
        path = ROOT/f'build/results-{group}.json'
        report = json.loads(path.read_text())
        if not report['passed']:
            raise ValueError(f'Incomplete or failed test group: {group}')
        logs[group] = []
        for entry in report.get('results', [])+report.get('additional_checks', []):
            if not entry['passed']:
                raise ValueError(f'Failed check: {group}/{entry["name"]}')
            if 'log' not in entry:
                continue
            log = ROOT/entry['log']
            actual = digest(log) if log.is_file() else None
            expected = entry.get('log_sha256')
            if actual is None or (expected is not None and expected != actual):
                if not entry['name'].startswith(('assemble-', 'compile-')):
                    raise ValueError(f'Test log missing or changed: {log}')
                omitted_logs.append({'group': group, 'name': entry['name'],
                                     'expected_sha256': expected, 'current_sha256': actual,
                                     'reason': 'Shared build log regenerated after this run; original report retained.'})
                continue
            logs[group].append(log)
        reports[group] = (path,report)
    edif = project/'impl1/bk0010_impl1.edi'
    for group in ('mapped-ebr', 'mapped-buffers'):
        if reports[group][1]['edif_sha256'] != digest(edif):
            raise ValueError(f'Primitive check does not match final EDIF: {group}')
    if reports['mapped-ebr'][1]['boot_hex_sha256'] != digest(ROOT/'build/boot.hex'):
        raise ValueError('Primitive ROM check does not match final BIOS')
    if destination.exists():
        raise FileExistsError(f'Use a new release directory: {destination}')
    destination.mkdir(parents=True)
    shutil.copy2(jed,destination/'bk0010_impl1.jed')
    sd = destination/'sd-files'
    files = prepare(sd)
    for name in ('boot.bin','boot.hex'):
        shutil.copy2(ROOT/'build'/name,destination/name)
    (destination/'diamond-result.json').write_text(json.dumps(result,indent=2)+'\n')
    evidence = destination/'checks'
    evidence.mkdir()
    verification = {}
    for group,(path,report) in reports.items():
        shutil.copy2(path,evidence/path.name)
        group_logs = evidence/group
        group_logs.mkdir()
        for log in logs[group]:
            shutil.copy2(log,group_logs/log.name)
        verification[group] = {'passed':True,'report_sha256':digest(path),
                               'logs':{log.name: digest(log) for log in logs[group]}}
    for suffix in ('.mrp','.twr','.par','.srr','.prf'):
        shutil.copy2(project/f'impl1/bk0010_impl1{suffix}',evidence/f'bk0010_impl1{suffix}')
    for name in ('phase.twr','phase.prf','build.exit','export.log'):
        if (project/name).exists():
            shutil.copy2(project/name,evidence/name)
    for name in ('mapped-ebr.log','mapped-buffers.log'):
        if (ROOT/'build'/name).exists():
            shutil.copy2(ROOT/'build'/name,evidence/name)
    files['BK.SET'] = {'bytes':(sd/'BK.SET').stat().st_size, 'sha256':digest(sd/'BK.SET')}
    manifest = {'physical_board_verified':False,'device':result['device'],
                'jed':result['jed'],'sd_files':files,'checks':verification,
                'boot_files':{name:{'bytes':(destination/name).stat().st_size,
                                   'sha256':digest(destination/name)} for name in ('boot.bin','boot.hex')},
                'omitted_build_logs':omitted_logs,
                'routed_sources':result['sources'],
                'validation_note':'Audio checks cover the AY oracle, BK register protocol, all seven CPU rates, stalled CPU, mixer and resets. Physical PS/2 BIOS entry, five-model navigation and selected-model boot are checked by bios-ps2. BIOS logical defaults, container skip and saving are checked by bios-logical; malformed EBR chains by ebr-bios. Earlier reports retain their source snapshots. Native music capture, disk OS cold boots and SD readback have separate evidence.',
                'pending':['physical five-model boot and BIOS save','raw SD partitions and BK disk OS on board',
                           'AY music and beeper on board','all seven frequencies on board','PAL voltage/speed calibration']}
    (destination/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    (destination/'README.md').write_text('''# БК: пять моделей и BIOS, hardware-lcd

Прошивка для LCMXO2-7000HC-4TG144I. Физическая проверка новой версии
ещё не выполнялась. JED прошёл MAP/PAR/TRACE и функциональные тесты;
состав и SHA-256 — manifest.json, отчёты и логи — checks/.

Скопируйте содержимое sd-files/ в корень существующего FAT16 раздела 1.
На рабочей карте сохраняйте существующий BK.SET, LOADER.BIN и P601.SET.
Предоставленный BK.SET — начальный режим 2, 3 МГц, MONO без дисков.
Пять ROM bundles и LOADERBK.BIN обновляются вместе с этой прошивкой.
Здесь нет разметки или файлов образов дискет: диски БК — сырые разделы
MBR 2…4 либо логические 5/6 в расширенном разделе 4, минимум 1600 секторов каждый.
На общей карте разделы Пълдина 2/3 сохраняются; БК использует логические 5/6.

Backspace во время отсчёта открывает BIOS. Стрелки также открывают меню
и выполняют первое действие. Короткая кнопка выбирает
3/4/6/8/12/16/24 МГц. Две секунды — тёплый RESET с возвратом частоты
из BIOS, десять — повторный вход в BIOS после завершения дисковой записи.
Описание конфигурации — ../../docs/bios.md и ../../README.md.
''')
    print(json.dumps({'directory':str(destination),'jed':result['jed'],
                      'physical_board_verified':False},indent=2))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--diamond',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    package(args.diamond,args.out)


if __name__ == '__main__':
    main()
