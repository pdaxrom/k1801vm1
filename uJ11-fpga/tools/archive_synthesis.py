#!/usr/bin/env python3
"""Archive an existing isolated Diamond gate and verify every source hash."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('names', nargs='+')
    args = p.parse_args()
    for name in args.names:
        source = ROOT/'build'/name
        destination = ROOT/'synth/reports'/name
        if destination.exists():
            raise SystemExit(f'refusing to overwrite {destination}')
        manifest = json.loads((source/'inputs.json').read_text())
        for path, expected in manifest['files'].items():
            data = (source/path.split(':')[1]).read_bytes() if path.startswith('generated:') else (ROOT/path).read_bytes()
            assert hashlib.sha256(data).hexdigest() == expected, path
        destination.mkdir(parents=True)
        for filename in ('inputs.json','result.json','clock.lpf','build.tcl','diamond.log'):
            shutil.copyfile(source/filename, destination/filename)
        result = json.loads((source/'result.json').read_text())
        for suffix, expected in result['reports'].items():
            path = source/'impl1'/f'{name}_impl1{suffix}'
            assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, suffix
            shutil.copyfile(path, destination/('design'+suffix))
        with tarfile.open(destination/'source.tgz','w:gz') as archive:
            for path in manifest['files']:
                if not path.startswith('generated:'):
                    archive.add(ROOT/path, arcname=path)
        print(f'Archived {name}: source and raw report hashes verified')


if __name__ == '__main__':
    main()
