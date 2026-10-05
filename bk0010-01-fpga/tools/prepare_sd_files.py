#!/usr/bin/env python3
"""Copy BK-owned boot files into a directory, preserving existing BK.SET and P601 files."""
import argparse
import hashlib
import json
from pathlib import Path

from models import parse_bundle, settings

ROOT = Path(__file__).resolve().parents[1]


def prepare(destination, replace=False, model=2):
    destination = Path(destination)
    payloads = {'LOADERBK.BIN': (ROOT/'build/LOADERBK.BIN').read_bytes()}
    if not 0 < len(payloads['LOADERBK.BIN']) <= 4096:
        raise ValueError('LOADERBK.BIN must fit the secondary loader RAM window')
    for ident in range(5):
        data = (ROOT/f'build/BK{ident:02}.ROM').read_bytes()
        parse_bundle(data, ident)
        payloads[f'BK{ident:02}.ROM'] = data
    # Check every collision before changing the directory.
    for name, data in payloads.items():
        path = destination/name
        if path.is_symlink() or (path.exists() and not path.is_file()):
            raise ValueError(f'Not a regular destination file: {path}')
        if path.exists() and path.read_bytes() != data and not replace:
            raise FileExistsError(f'{path}: pass --replace to update this BK-owned file')
    destination.mkdir(parents=True, exist_ok=True)
    for name, data in payloads.items():
        path = destination/name
        if not path.exists() or path.read_bytes() != data:
            temporary = path.with_name(name+'.new')
            with temporary.open('xb') as output:
                output.write(data)
            temporary.replace(path)
    config = destination/'BK.SET'
    if not config.exists():
        with config.open('xb') as output:
            output.write(settings(model=model))
    return {name: {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
            for name, data in payloads.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination',type=Path,default=ROOT/'build/sd-files')
    parser.add_argument('--replace',action='store_true',help='Update differing BK-owned loader and ROM files')
    parser.add_argument('--default-model',type=int,choices=range(5),default=2,
                        help='Used only when BK.SET is absent')
    args = parser.parse_args()
    files = prepare(args.destination,args.replace,args.default_model)
    print(json.dumps({'directory':str(args.destination),'files':files,
                      'existing_settings':'preserved'},ensure_ascii=False,indent=2))


if __name__ == '__main__':
    main()
