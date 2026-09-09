#!/usr/bin/env python3
"""Build/run FIS tests with independent simulators and portable/vendor ROMs."""
import argparse
import concurrent.futures
import csv
import io
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CORE = ['rtl/uj11_core.v','rtl/uj11_decode.v','rtl/uj11_engine.v',
        'rtl/uj11_microseq.v','rtl/uj11_datapath.v','rtl/uj11_regfile.v',
        'rtl/uj11_alu.v','rtl/uj11_psw.v','rtl/uj11_mem.v']
MEMORY = ['tb/uj11_ram.v','rtl/uj11_stream.v','rtl/uj11_prefetch_control.v',
          'rtl/uj11_prefetch.v','rtl/uj11_fram_transport.v','reference/lsi11/spi_fram_model.v']


def execute(command, tag, vectors, jobs=1):
    """Shard independent reset-per-case fixtures; retain original IDs and raw logs."""
    if jobs == 1:
        with (ROOT/f'build/{tag}.log').open('w') as log:
            subprocess.run(command+[f'+vectors={vectors}', f'+results=build/{tag}.csv'],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        return
    lines = (ROOT/vectors).read_text().splitlines()
    assert lines and all(line.strip() for line in lines)
    expected = [int(line.split()[0], 16) for line in lines]
    assert len(set(expected)) == len(expected)
    folder = ROOT/f'build/{tag}-shards'
    folder.mkdir(exist_ok=True)
    count = min(jobs, len(lines))
    size = (len(lines)+count-1)//count
    chunks = [lines[i:i+size] for i in range(0, len(lines), size)]

    def run(i, chunk):
        fixture = folder/f'{i}.txt'
        fixture.write_text('\n'.join(chunk)+'\n')
        with (folder/f'{i}.log').open('w') as log:
            subprocess.run(command+[f'+vectors={fixture}', f'+results={folder}/{i}.csv'],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        text = (folder/f'{i}.log').read_text()
        assert f': {len(chunk)} exact' in text
        data = (folder/f'{i}.csv').read_text()
        values = list(csv.DictReader(io.StringIO(data)))
        assert [int(r['case']) for r in values] == [int(s.split()[0], 16) for s in chunk]
        return text, data

    with concurrent.futures.ThreadPoolExecutor(max_workers=count) as pool:
        futures = [pool.submit(run, i, chunk) for i, chunk in enumerate(chunks)]
        results = [f.result() for f in futures]
    header = results[0][1].splitlines()[0]
    combined = [header]
    for _, data in results:
        parts = data.splitlines()
        assert parts[0] == header
        combined.extend(parts[1:])
    (ROOT/f'build/{tag}.csv').write_text('\n'.join(combined)+'\n')
    (ROOT/f'build/{tag}.log').write_text('\n'.join(t for t, _ in results)+
        f'PASS FIS aggregate: {len(lines)} exact state/PSW/bus/memory cases; {len(chunks)} independent shards\n')


def compile_test(mode=-1, simulator='verilator', vendor=False, candidate=False,
                 tag=None, replacements=None, top='tb_fis'):
    tag = tag or f'fis-{simulator}-{"vendor" if vendor else "portable"}-{mode}'
    flags = ['-DFIS_FRAM'] if mode >= 0 else []
    if vendor:
        lib = Path(os.environ.get('LATTICE_SIM_DIR', str(ROOT/'build/vendor')))
        rom = ['microcode/generated/uj11_m0_ebr.v']+[str(lib/(n+'.v')) for n in ('DP8KC','GSR','PUR')]
        flags += ['-DVENDOR_ROM','-DUJ11_VENDOR_ROM']
    else:
        rom = ['rtl/uj11_rom.v']
    sources = [f'tb/{top}.v'] + CORE + MEMORY + rom
    sources = [(replacements or {}).get(s, s) for s in sources]
    if simulator == 'verilator':
        # The unchanged legacy SPI model relies on implicit unsigned widening.
        # Make that exact extension explicit in a build copy for strict lint;
        # Icarus/vendor tests continue to use the original reference file.
        legacy = 'reference/lsi11/spi_fram_model.v'
        original = (ROOT/legacy).read_text()
        before = '(address << 8) | input_shift'
        after = "(address << 8) | {16'b0,input_shift}"
        assert original.count(before) == 1
        adapted = original.replace(before,after)
        assert adapted.replace(after,before) == original
        (ROOT/'build/spi_fram_model_verilator.v').write_text(adapted)
        sources = ['build/spi_fram_model_verilator.v' if s == legacy else s for s in sources]
        command = ['verilator','--binary','--timing','--top-module',top,'-j','4',
                   '--Mdir',f'build/obj-{tag}',f'-GMEMORY_MODE={mode}',
                   f'-GCANDIDATE_ROM={int(candidate)}'] + flags + sources
        binary = [f'build/obj-{tag}/V{top}']
    else:
        command = ['iverilog','-g2012','-Wall','-s',top,f'-P{top}.MEMORY_MODE={mode}',
                   f'-P{top}.CANDIDATE_ROM={int(candidate)}','-o',f'build/{tag}'] + flags + sources
        binary = ['vvp',f'build/{tag}']
    with (ROOT/f'build/{tag}-build.log').open('w') as log:
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    return binary


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--simulator', choices=('verilator','iverilog'))
    p.add_argument('--memory', type=int, choices=(-1,1,2), nargs='+', default=[-1,2])
    p.add_argument('--vendor', action='store_true')
    p.add_argument('--vectors', default='build/fis-vectors.txt')
    p.add_argument('--jobs', type=int, default=1, help='independent simulation shards (same compiled RTL)')
    p.add_argument('--tag-suffix', default='', help='separate output paths for a concurrent run')
    args = p.parse_args()
    args.simulator = args.simulator or ('iverilog' if args.vendor else 'verilator')
    if args.jobs < 1 or args.jobs > 16:
        p.error('--jobs must be between 1 and 16')
    if any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for c in args.tag_suffix):
        p.error('--tag-suffix must contain only letters, digits, hyphens and underscores')
    if args.vendor and args.simulator != 'iverilog':
        p.error('unmodified Lattice DP8KC uses procedural assign/deassign; use Icarus for vendor ROM')
    for mode in args.memory:
        tag = f'fis-{args.simulator}-{"vendor" if args.vendor else "portable"}-{mode}' + args.tag_suffix
        command = compile_test(mode,args.simulator,args.vendor,tag=tag)
        execute(command, tag, args.vectors, args.jobs)
        print((ROOT/f'build/{tag}.log').read_text().strip(),flush=True)


if __name__ == '__main__':
    main()
