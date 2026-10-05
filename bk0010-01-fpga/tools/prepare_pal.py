#!/usr/bin/env python3
"""Tabulate the unchanged uJ11 encoder for BK's finite RGB332 palette."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def prepare():
    config = json.loads((ROOT/'config/video.json').read_text())
    colors = {0, 255}
    for palette in config['palettes_rgb24']:
        for value in palette:
            rgb = int(value, 16)
            colors.add(((rgb >> 16) & 224) | ((rgb >> 11) & 28) | ((rgb >> 6) & 3))
    colors = sorted(colors)
    assert len(colors) <= 16
    table = []
    for address in range(1024):
        index = address & 15
        rgb = colors[index] if index < len(colors) else 0
        red, green, blue = rgb >> 5, (rgb >> 2) & 7, rgb & 3
        active, burst, alternate, phase = bool(address & 512), bool(address & 256), bool(address & 128), (address >> 4) & 7
        y = 6*red + [0, 12, 23, 35, 47, 59, 70, 82][green] + [0, 5, 11, 16][blue] if active else 0
        u = -21 if burst else -3*red - [0, 6, 11, 17, 23, 29, 34, 40][green] + [0, 20, 41, 61][blue] if active else 0
        v = 21 if burst else [0, 12, 25, 37, 49, 61, 74, 86][red] - [0, 10, 21, 31, 41, 51, 62, 72][green] - [0, 5, 9, 14][blue] if active else 0
        if alternate:
            v = -v
        if phase in (0, 2, 4, 6):
            chroma = [v, u, -v, -u][phase//2]
        else:
            diagonal = {1: u+v, 3: u-v, 5: -u-v, 7: -u+v}[phase]
            chroma = (diagonal >> 1) + (diagonal >> 3) + (diagonal >> 4)
        mixed = 60 + y + chroma
        table.append(max(0, min(255, mixed)) >> 2)
    destination = ROOT/'build/pal_samples.hex'
    destination.write_text(''.join(f'{sample:02x}\n' for sample in table))
    lines = ['// Generated from config/video.json by tools/prepare_pal.py.',
             'function [3:0] bk_rgb_index;', '    input [7:0] rgb;', '    begin case(rgb)']
    lines += [f"        8'h{rgb:02x}: bk_rgb_index = 4'd{index};" for index, rgb in enumerate(colors)]
    lines += ['        default: bk_rgb_index = 0;', '    endcase end', 'endfunction', '']
    (ROOT/'rtl/bk_rgb_index.vh').write_text('\n'.join(lines))
    lines = ['// Generated from config/video.json by tools/prepare_pal.py.',
             f"localparam [3:0] BK_WHITE_INDEX = 4'd{colors.index(255)};",
             'function [3:0] bk_palette_index;', '    input [3:0] palette;',
             '    input [1:0] pixel;', '    begin case({palette,pixel})']
    for palette, values in enumerate(config['palettes_rgb24']):
        for pixel, value in enumerate(values):
            red, green, blue = (int(value[position:position+2], 16) for position in (0, 2, 4))
            rgb = round(red*7/255) << 5 | round(green*7/255) << 2 | round(blue*3/255)
            if rgb:
                lines.append(f"        6'h{palette*4+pixel:02x}: bk_palette_index = 4'd{colors.index(rgb)};")
    lines += ['        default: bk_palette_index = 0;', '    endcase end', 'endfunction', '']
    (ROOT/'rtl/bk_palette_index.vh').write_text('\n'.join(lines))


if __name__ == '__main__':
    prepare()
