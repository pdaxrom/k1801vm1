#!/usr/bin/env python3
"""Independent F-format oracle: exact rational arithmetic, no host float.

DEC KE11-E/F OP-001 sections 3.2.2/3.2.3: exponent-zero is clean zero;
nearest rounding, ties increase magnitude; ADD/SUB underflow yields zero,
MUL/DIV underflow traps. This is the architectural arithmetic profile, not
the KE11-F's 24-place alignment / two-guard-bit microcycle implementation.
"""
from fractions import Fraction
import random


def decode(bits):
    e = (bits >> 23) & 255
    if not e:
        return Fraction(0)
    m = (bits & 0x7fffff) | 0x800000
    return (-1 if bits >> 31 else 1) * m * Fraction(2) ** (e - 152)


def encode(value, add_sub=False):
    if not value:
        return 0, 4, False
    sign = value < 0
    value = abs(value)
    exponent = value.numerator.bit_length() - value.denominator.bit_length() + 1
    if value < Fraction(2) ** (exponent - 1):
        exponent -= 1
    scaled = value * Fraction(2) ** (24 - exponent)
    mantissa, remainder = divmod(scaled.numerator, scaled.denominator)
    mantissa += 2 * remainder >= scaled.denominator
    if mantissa == 1 << 24:
        mantissa >>= 1
        exponent += 1
    exponent += 128
    if exponent <= 0:
        return (0, 4, False) if add_sub else (None, 10, True)
    if exponent > 255:
        return None, 2, True
    return ((int(sign) << 31) | (exponent << 23) | (mantissa & 0x7fffff)), 8 * sign, False


def reference(op, a, b):
    av, bv = decode(a), decode(b)
    if op == 3 and not bv:
        return a, 11, True
    result = (av + bv if op == 0 else av - bv if op == 1 else
              av * bv if op == 2 else av / bv)
    bits, flags, error = encode(result, op < 2)
    return a if error else bits, flags, error


def vectors(count=4096, seed=0x27f15):
    special = [0, 0x80000000, 0x007fffff, 0x807fffff, 0x00800000,
               0x00800001, 0x00ffffff, 0x01000000, 0x34000000, 0x34800000,
               0x35000000, 0x3fffffff, 0x40000000, 0x40800000, 0x40800001,
               0x40ffffff, 0x41000000, 0x7f000000, 0x7f7fffff, 0x7fffffff,
               0xbf800000, 0xc0800000, 0xc0800001, 0xffffffff]
    for op in range(4):
        for a in special:
            for b in special:
                yield op, a, b
    for exponent in (1, 2, 24, 25, 128, 129, 254, 255):
        for fraction in (0, 1, 0x7fffff):
            for gap in (1, 2, 23, 24, 25, 29, 30, 31, 32):
                if exponent <= gap:
                    continue
                for tail in (0, 1, 0x7fffff):
                    for signs in range(4):
                        a = ((signs >> 1) << 31) | (exponent << 23) | fraction
                        b = ((signs & 1) << 31) | ((exponent-gap) << 23) | tail
                        for op in (0, 1):
                            yield op, a, b
    rng = random.Random(seed)
    for i in range(count):
        a, b = rng.getrandbits(32), rng.getrandbits(32)
        if i & 1:
            e = max(1, min(255, ((a >> 23) & 255) + rng.randrange(-2, 3)))
            b = (b & 0x807fffff) | (e << 23)
        yield i % 4, a, b
