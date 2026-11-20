#!/usr/bin/env python3
"""Pack the OFL typefaces and original launcher icon for the Switch 2 framebuffer."""
from pathlib import Path
import struct

from PIL import Image, ImageFont, ImageDraw

SWITCH = Path(__file__).resolve().parents[1]
RES = SWITCH.parent / 'assets'
OUT = SWITCH / 'romfs/ui'
OUT.mkdir(parents=True, exist_ok=True)

# All faces share a fixed coverage plane. The first byte is the advance.
for name, file, size in [
    ('small', 'MPLUSRounded1c-Bold.ttf', 17),
    ('body', 'MPLUSRounded1c-Regular.ttf', 22),
    ('title', 'MPLUSRounded1c-ExtraBold.ttf', 34),
    ('hero', 'MPLUSRounded1c-ExtraBold.ttf', 48),
]:
    font = ImageFont.truetype(str(RES / 'Fonts' / file), size)
    with (OUT / f'{name}.font').open('wb') as out:
        for code in range(32, 127):
            char = chr(code)
            mask = font.getmask(char, mode='L')
            x, y, _, _ = font.getbbox(char)
            out.write(bytes((round(font.getlength(char)), x & 255, y & 255, *mask.size)))
            out.write(bytes(mask))


# Original launcher icon. The overlapping save slots mirror the box editor.
icon = Image.new('RGB', (256, 256), '#171b32')
draw = ImageDraw.Draw(icon)
draw.rounded_rectangle((35, 43, 190, 181), radius=22, fill='#26375c', outline='#758bb3', width=5)
draw.rounded_rectangle((60, 69, 219, 209), radius=20, fill='#233457', outline='#49c8e8', width=7)
for x in (84, 127, 170):
    for y in (95, 139):
        draw.rounded_rectangle((x, y, x+25, y+25), radius=5, fill='#e1d0ab' if (x,y)==(84,95) else '#6086ac')
icon.save(SWITCH/'icon.jpg', quality=95, subsampling=0)
