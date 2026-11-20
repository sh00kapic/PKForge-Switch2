#!/usr/bin/env python3
"""Pack the OFL typefaces and PKForge launcher icon for the Switch 2 framebuffer."""
from pathlib import Path

from PIL import Image, ImageFont

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


# Match the Android launcher background when converting the transparent logo to JPEG.
with Image.open(RES / 'AppIcon/pkforge.png') as source:
    logo = source.convert('RGBA')
    icon = Image.new('RGB', logo.size, '#14121D')
    icon.paste(logo, mask=logo.getchannel('A'))
    icon = icon.resize((256, 256), Image.Resampling.NEAREST)
    icon.save(SWITCH / 'icon.jpg', quality=95, subsampling=0)
