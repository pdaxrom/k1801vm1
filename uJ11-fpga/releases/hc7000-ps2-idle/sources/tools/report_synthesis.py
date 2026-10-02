#!/usr/bin/env python3
"""Extract actual MAP/PAR/TRACE evidence. Missing results are errors, not zeros."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def extract(map_text, timing_text, par_text, *, clock=None):
    result = {}
    for name, pattern in [('lut4','LUT4s'),('ff','registers'),('ebr','block RAMs'),('slices','SLICEs')]:
        m = re.search(r'Number of '+pattern+r':\s+(\d+)\s+out of\s+(\d+)',map_text)
        if not m:
            raise ValueError(f'MAP missing {name}')
        result[name] = int(m[1])
        result[name+'_capacity'] = int(m[2])
    frequency_text = timing_text
    if clock is not None:
        sections = re.split(r'(?=^Preference:)', timing_text, flags=re.M)
        frequency_text = '\n'.join(s for s in sections if re.match(
            r'Preference:\s+FREQUENCY NET "'+re.escape(clock)+r'"\s', s))
    frequencies = re.findall(r'(?:Report|Warning):\s+([0-9.]+)MHz is the maximum frequency',frequency_text)
    slacks = re.findall(r'Cumulative negative slack:\s*(-?[0-9.]+)',timing_text)
    if len(frequencies) != 1 or not slacks:
        raise ValueError('Expected one clock preference and completed TRACE slack summary')
    result['fmax_mhz'] = float(frequencies[0])
    result['cumulative_negative_slack_reported'] = [float(s) for s in slacks]
    result['timing_pass'] = all(float(s) == 0 for s in slacks)
    result['fully_routed'] = 'All signals are completely routed' in par_text
    if not result['fully_routed']:
        raise ValueError('PAR has no complete-routing confirmation')
    return result


def check_clock(result, timing_text, requested_mhz):
    """A passing nominal OSCH TRACE must not satisfy a faster requested gate."""
    clocks=sorted({float(x) for x in re.findall(r'Preference:\s+FREQUENCY NET "clk"\s+([\d.]+) MHz',timing_text)})
    matched=len(clocks)==1 and abs(clocks[0]-requested_mhz)<0.001
    result.update(applied_clock_mhz=clocks[0] if len(clocks)==1 else None,
                  requested_clock_mhz=requested_mhz,constraint_matched=matched,
                  trace_timing_pass=result['timing_pass'])
    result['timing_pass']=matched and result['timing_pass'] and result['fmax_mhz']+0.001>=requested_mhz
    if not matched:result['timing_failure']='TRACE clock preference differs from the requested gate'
    elif not result['timing_pass']:result['timing_failure']='Requested clock timing failed'
    return result
