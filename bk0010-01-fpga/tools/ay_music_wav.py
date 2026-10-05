#!/usr/bin/env python3
"""Validate native VM1 music capture and render simulation PCM as a WAV."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import wave


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, default=Path('build/ay-music'))
    args = parser.parse_args()
    prefix = args.prefix
    samples_path = Path(str(prefix)+'-pcm.txt')
    trace_path = Path(str(prefix)+'-registers.txt')
    log_path = Path(str(prefix)+'-native.log')
    text = log_path.read_text()
    match = re.search(r'PASS native VM1 system disk AY music: writes=(\d+) samples=(\d+) range=(\d+)\.\.(\d+)', text)
    assert match and '%Fatal' not in text and 'FATAL' not in text, 'Native music capture did not pass'
    assert 'PASS switched native runtime to model 3 frequency index=6' in text
    samples = list(map(int, samples_path.read_text().splitlines()))
    commands = [tuple(map(int, line.split())) for line in trace_path.read_text().splitlines()]
    assert len(samples) == 48000 and len(commands) == int(match[1]) >= 300
    assert (min(samples), max(samples)) == (int(match[3]), int(match[4]))
    assert len(set(samples)) > 8 and all(0 <= value <= 509 for value in samples)
    assert {0, 1, 2, 3, 4, 5, 7, 8, 9, 10}.issubset({row[1] for row in commands})
    # Capture digital PCM before sigma-delta conversion; remove DC and scale
    # for a convenient listening sample. The board's analogue filter is not modelled.
    mean = sum(samples)/len(samples)
    pcm = b''.join(struct.pack('<h', max(-32768, min(32767, round((value-mean)*64)))) for value in samples)
    wav_path = prefix.with_suffix('.wav')
    with wave.open(str(wav_path), 'wb') as file:
        file.setparams((1, 2, 48000, len(samples), 'NONE', 'not compressed'))
        file.writeframes(pcm)
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    report = {'passed': True, 'cpu_mhz': 24, 'ay_master_hz': 1773400, 'sample_rate': 48000,
              'samples': len(samples), 'register_writes': len(commands), 'pcm_min': min(samples),
              'pcm_max': max(samples), 'distinct_pcm_levels': len(set(samples)),
              'simulation_recording': True, 'physical_board_verified': False,
              'files': {path.name: sha(path) for path in (samples_path, trace_path, log_path, wav_path)}}
    prefix.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
