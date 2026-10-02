#!/usr/bin/env python3
"""Build a fresh HC7000 SD file with BSD, RSX, RT-11 V4 and XM compilers."""
import argparse
import contextlib
from datetime import date
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import struct
import subprocess

from board_common import ROOT
from build_sd_compilers import build as build_compilers
from build_sd_image import directory, text_bytes
from build_storage_menu import build as build_menu
from sdcard import BOOT, MENU, Label, add_partition, copy_bytes, publish, read_label
import storage_menu

NAME = 'uj11-hc7000-multi-os.img'
CARD_BLOCKS = 2097152  # 1 GiB; same partition positions as the installed card.
RELEASE_DATE = '2026-10-02'
# media, unit, start LBA, label, source below lsi11/disks or prepared RT-11 media
DISKS = (
    ('rl02', 0, 2048, 'bsd-root', 'bsd2.9/2.9BSD-root.rl02'),
    ('rl02', 1, 22528, 'bsd-swap', 'bsd2.9/swap.rl02'),
    ('rm05', 0, 43008, 'bsd-usr', 'bsd2.9/2.9BSD-usr.rm05'),
    ('mscp', 0, 1048576, 'RSX-SYS', 'rsx11/rsxm11sys.dsk'),
    ('mscp', 1, 1361920, 'RSX-PBL', 'rsx11/rsx11mpbl87.dsk'),
    ('rk05', 0, 1673216, 'RT11V4', 'rt11v400.dsk'),
    ('rk07', 0, 1679360, 'RT11XM', '@system.dsk'),
    ('rk07', 1, 1734656, 'RT11VOL', '@storage.dsk'),
    ('rk05', 1, 1789952, 'BASIC', '@basic.dsk'),
    ('rk05', 2, 1796096, 'PASCAL', '@pascal.dsk'),
    ('rk05', 3, 1802240, 'FORTRAN', '@fortran.dsk'),
)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def region_digest(stream, size):
    result = hashlib.sha256()
    while size:
        data = stream.read(min(size, 1024 * 1024))
        if not data:
            raise ValueError('short SD partition')
        result.update(data)
        size -= len(data)
    return result.hexdigest()


def add_system_utilities(out):
    """Retain the stock XM disk and add the verified HG kit and SD help."""
    kit = ROOT / 'releases/sd-hc7000/software'
    metadata = kit / 'manifest.json'
    native = json.loads(metadata.read_text())
    if native['fixtures']:
        raise ValueError('HG test fixtures cannot be packaged')
    for source, expected in native['sources'].items():
        if digest(ROOT / source) != expected:
            raise ValueError(f'{source}: HG kit does not match current sources')
    inputs = {str(metadata.relative_to(ROOT)): digest(metadata)}
    inputs.update(native['sources'])
    additions = {}
    staging = out / 'files'
    staging.mkdir()
    for name in ('HGX.SYS', 'HGTIME.SAV', 'TMRATE.SAV'):
        path = kit / name
        if digest(path) != native['outputs'][name]:
            raise ValueError(f'{name}: HG release hash differs')
        additions[name] = path.read_bytes()
        inputs[str(path.relative_to(ROOT))] = digest(path)
    for name in ('HGSYNC.COM', 'XMCHK.COM'):
        path = ROOT / 'demos/rt11/sd-hc7000' / name
        additions[name] = text_bytes(path)
        inputs[str(path.relative_to(ROOT))] = digest(path)
    help_file = ROOT / 'demos/rt11/sd-hc7000-multi/SDHELP.TXT'
    additions[help_file.name] = text_bytes(help_file)
    inputs[str(help_file.relative_to(ROOT))] = digest(help_file)
    image = out / 'media/system.dsk'
    original = image.read_bytes()
    before, _, _ = directory(original)
    tool = ROOT.parent / 'lsi11/rt11tool'
    for name, payload in sorted(additions.items()):
        if name in before:
            raise ValueError(f'{name}: utility would replace an existing file')
        path = staging / name
        path.write_bytes(payload)
        subprocess.run([str(tool), 'add', str(image), str(path), name],
                       check=True, capture_output=True)
    blob = bytearray(image.read_bytes())
    entries, _, _ = directory(blob)
    stamp = date.fromisoformat(RELEASE_DATE)
    age = stamp.year - 1972
    word = ((age // 32) << 14) | (stamp.month << 10) | (stamp.day << 5) | (age % 32)
    for name in additions:
        struct.pack_into('<H', blob, entries[name]['entry_offset'] + 12, word)
    image.write_bytes(blob)
    after, _, _ = directory(blob)
    if blob[:3072] != original[:3072]:
        raise ValueError('utility installation changed boot/home blocks')
    for name, info in before.items():
        if after[name]['sha256'] != info['sha256']:
            raise ValueError(f'{name}: stock file changed during utility installation')
    files = {}
    for name, payload in additions.items():
        padded = payload + bytes((-len(payload)) % 512)
        expected = hashlib.sha256(padded).hexdigest()
        if after[name]['sha256'] != expected:
            raise ValueError(f'{name}: utility readback differs')
        files[name] = dict(bytes=len(payload), blocks=len(padded) // 512, sha256=expected)
    for path, expected in inputs.items():
        if digest(ROOT / path) != expected:
            raise ValueError(f'{path}: utility input changed')
    return dict(inputs=inputs, files=files, release_date=RELEASE_DATE,
                tool_sha256=digest(tool), stock_files_preserved=True, boot_blocks_preserved=True)


def build(out, compress=True):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    distribution = ROOT.parent / 'lsi11/disks'
    with contextlib.redirect_stdout(io.StringIO()):
        compilers = build_compilers(out / 'media')
        menu_record = build_menu(out / 'menu')
    utilities = add_system_utilities(out)
    menu = (out / 'menu/menu.img').read_bytes()
    storage_menu.unpack(menu)
    label = Label(CARD_BLOCKS, features=MENU)
    copies = []
    sources = {}
    for disk in compilers['disks']:
        sources[disk['source']] = {
            'bytes': disk['source_bytes'], 'sha256': disk['source_sha256']}
    for media, unit, start, name, source in DISKS:
        path = out / 'media' / source[1:] if source.startswith('@') else distribution / source
        size = path.stat().st_size
        if not size or size % 512:
            raise ValueError(f'{path}: empty or misaligned disk')
        label = add_partition(label, media, unit, blocks=size // 512 if media == 'mscp' else None,
                              start=start, boot=name == 'bsd-root', name=name)
        partition = label.partitions[-1]
        if size > partition.blocks * 512:
            raise ValueError(f'{path}: disk exceeds media geometry')
        source_hash = digest(path)
        if not source.startswith('@'):
            sources[str(path.relative_to(ROOT.parent))] = {'bytes': size, 'sha256': source_hash}
        copies.append((partition, path, size, source_hash))

    image = out / NAME
    with image.open('x+b') as stream:
        stream.truncate(label.blocks * 512)
        stream.seek(1024)
        stream.write(menu)
        for partition, path, size, _ in copies:
            stream.seek(partition.start * 512)
            with path.open('rb') as src:
                copy_bytes(src, stream, size)
        publish(stream, label)

    partitions = []
    with image.open('rb') as stream:
        actual, lba = read_label(stream)
        if actual != label or lba != 0:
            raise ValueError('incorrect primary SD label')
        stream.seek(0)
        if stream.read(512) != label.pack() or stream.read(512) != label.pack():
            raise ValueError('SD label copies differ')
        if storage_menu.read(stream) != menu_record['format']:
            raise ValueError('incorrect menu descriptor')
        stream.seek(1024)
        if stream.read(len(menu)) != menu:
            raise ValueError('menu readback differs')
        for partition, path, size, source_hash in copies:
            stream.seek(partition.start * 512)
            if region_digest(stream, size) != source_hash:
                raise ValueError(f'{partition.label}: disk readback differs')
            padding = partition.blocks * 512 - size
            if any(stream.read(padding)):
                raise ValueError(f'{partition.label}: nonzero geometry padding')
            stream.seek(partition.start * 512)
            partitions.append(dict(vars(partition), source_bytes=size,
                                   zero_padding=padding, payload_sha256=source_hash,
                                   partition_sha256=region_digest(stream, partition.blocks * 512)))

    boot = [p for p in label.partitions if p.flags & BOOT]
    if len(boot) != 1 or (boot[0].kind, boot[0].unit) != (5, 0):
        raise ValueError('default boot must be RL0')
    record = dict(
        board='hc7000-lcd-sram', cpu='mmu', fpp='off', iop='storage',
        format='UJ11SD v1; duplicate labels, SD menu, 11 disk partitions',
        default_boot='rl0', boot_command='rl(0,0)rlunix',
        partitions=partitions, sources=sources, compiler_preparation=compilers,
        system_utilities=utilities, menu=menu_record,
        image=dict(name=NAME, bytes=image.stat().st_size, sha256=digest(image)),
        builder_sha256=digest(Path(__file__)),
        verification=dict(label_copies_match=True, menu_crc_and_readback=True,
                          partition_payloads_match=True, geometry_padding_zero=True,
                          default_boot_rl0=True, originals_unchanged=True),
        scope='Fresh distribution image; not a backup of mutable physical SD or a new OS boot test')
    if compress:
        compressed = out / (NAME + '.gz')
        with image.open('rb') as src, compressed.open('xb') as dst:
            with gzip.GzipFile(filename='', fileobj=dst, mode='wb', mtime=0, compresslevel=9) as gz:
                shutil.copyfileobj(src, gz)
        with gzip.open(compressed, 'rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != record['image']['sha256']:
                raise ValueError('decompressed SD hash differs')
        record['compressed'] = dict(name=compressed.name, bytes=compressed.stat().st_size,
                                    sha256=digest(compressed))
        record['verification']['gzip_roundtrip'] = True
    for source, info in sources.items():
        if digest(ROOT.parent / source) != info['sha256']:
            raise ValueError(f'{source}: distribution changed during build')
    (out / 'manifest.json').write_text(json.dumps(record, indent=2) + '\n')
    (out / 'SHA256SUMS').write_text(''.join(
        f"{record[key]['sha256']}  {record[key]['name']}\n"
        for key in ('image', 'compressed') if key in record))
    print(json.dumps(record['image'], indent=2))
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/sd-hc7000-multi')
    parser.add_argument('--no-compress', action='store_true')
    args = parser.parse_args()
    build(args.out, not args.no_compress)
