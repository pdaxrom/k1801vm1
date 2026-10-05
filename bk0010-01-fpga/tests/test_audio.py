#!/usr/bin/env python3
"""Independent AY PCM vectors: closed-form envelopes and known LFSR stream."""
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LEVELS = [0, 1, 2, 3, 4, 6, 8, 12, 16, 23, 32, 45, 64, 80, 101, 127]


def envelope(shape, step):
    attack = bool(shape & 4)
    if step < 16:
        return step if attack else 15-step
    if not shape & 8:
        return 0
    if shape & 1:
        return 15 * (attack != bool(shape & 2))
    ramp = step % 16
    if shape & 2 and step // 16 % 2:
        attack = not attack
    return ramp if attack else 15-ramp


class AY:
    def reset(self):
        self.r = [0]*14
        self.tone = [0]*3
        self.elapsed = [0]*3
        self.noise = 1
        self.noise_elapsed = 0
        self.noise_half = 0
        self.env_elapsed = 0
        self.env_step = None

    def __init__(self):
        self.reset()

    def cycle(self, reset, tick, write, address, data):
        old = self.r[:]
        if reset:
            self.reset()
        else:
            if tick:
                for channel in range(3):
                    self.elapsed[channel] += 1
                    period = max(1, old[2*channel] + 256*old[2*channel+1])
                    if self.elapsed[channel] >= period:
                        self.elapsed[channel] = 0
                        self.tone[channel] ^= 1
                self.noise_elapsed += 1
                if self.noise_elapsed >= max(1, old[6]):
                    self.noise_elapsed = 0
                    self.noise_half ^= 1
                    if self.noise_half == 0:
                        feedback = ((self.noise >> 3) ^ self.noise) & 1
                        self.noise = (self.noise >> 1) | (feedback << 16)
                if self.env_step is not None:
                    self.env_elapsed += 1
                    if self.env_elapsed >= max(1, 2*(old[11]+256*old[12])):
                        self.env_elapsed = 0
                        self.env_step += 1
            if write:
                if address < 14:
                    mask = 15 if address in [1, 3, 5, 13] else 31 if address in [6, 8, 9, 10] else 255
                    self.r[address] = data & mask
                if address == 13:
                    self.env_step = 0
                    self.env_elapsed = 0
        value = 0
        for channel in range(3):
            gate = (self.tone[channel] or self.r[7] & (1 << channel)) and (self.noise & 1 or self.r[7] & (8 << channel))
            volume = self.r[8+channel]
            level = (0 if self.env_step is None else envelope(self.r[13], self.env_step)) if volume & 16 else volume & 15
            value += LEVELS[level] if gate else 0
        return value


def generate():
    model = AY()
    vectors = []

    def emit(reset=0, tick=0, write=0, address=0, data=0):
        row = (reset, tick, write, address, data)
        vectors.append(' '.join(map(str, (*row, model.cycle(*row)))))

    def reg(address, data):
        emit(write=1, address=address, data=data)

    # Every envelope shape for more than four full ramps, at period zero/one/three.
    for period in [0, 1, 3]:
        for shape in range(16):
            emit(reset=1)
            reg(7, 63)
            reg(8, 16)
            reg(11, period)
            reg(13, shape)
            for _ in range(80*max(1, period*2)):
                emit(tick=1)
            reg(13, shape)  # same shape restarts
            for _ in range(20*max(1, period*2)):
                emit(tick=1)
    # Noise and all tone widths, including zero/max, masks and gating combinations.
    for period in [0, 1, 2, 17, 4095]:
        emit(reset=1)
        for channel in range(3):
            reg(channel*2, period & 255)
            reg(channel*2+1, period >> 8)
            reg(channel+8, 15-channel)
        for mixer in [0, 7, 56, 63, 21, 42]:
            reg(7, mixer)
            reg(6, mixer & 31)
            for _ in range(8200 if period == 4095 else 100):
                emit(tick=1)
    # Coarse/fine boundary and maximum 16-bit envelope period must not wrap early.
    for period in [255, 256, 65535]:
        emit(reset=1)
        reg(7, 63)
        reg(8, 16)
        reg(11, period & 255)
        reg(12, period >> 8)
        reg(13, 12)
        for _ in range(2*period+4):
            emit(tick=1)
    # Arbitrary writes crossing generator ticks and asynchronous-looking gaps.
    rng = random.Random(8910)
    for _ in range(15000):
        emit(reset=int(rng.randrange(2000) == 0), tick=int(rng.randrange(4) != 0),
             write=int(rng.randrange(9) == 0), address=rng.randrange(16), data=rng.randrange(256))
    (ROOT/'build/ay-vectors.txt').write_text('\n'.join(vectors)+'\n')
    print(f'AY oracle: {len(vectors)} cycles, all 16 shapes, zero/max periods, masks, noise and random writes')


if __name__ == '__main__':
    generate()
