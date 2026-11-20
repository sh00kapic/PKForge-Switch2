"""Pack the album's pixel face and its multilingual fallback for the offline renderer."""
from pathlib import Path
import struct
import sys
from fontTools.ttLib import TTFont
from PIL import ImageFont

root = Path(__file__).resolve().parents[1]
fonts = root.parent / 'assets/Fonts'
pixel = fonts / 'DotGothic16-Regular.ttf'
fallback = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc')
coverage = TTFont(pixel).getBestCmap()
letters = set(range(32, 127)) | {ord('★'), ord('·')}
letters.update(int(code, 16) for code in (fonts / 'album-codepoints.txt').read_text().splitlines())
for label, size in [('album', 24), ('caption', 18)]:
    primary = ImageFont.truetype(str(pixel), size)
    secondary = ImageFont.truetype(str(fallback), size - 3)
    with (root / 'romfs/ui' / (label + '.font')).open('wb') as out:
        out.write(struct.pack('<I',len(letters)))
        for code in sorted(letters):
            char = chr(code)
            font = primary if code in coverage else secondary
            mask = font.getmask(char, mode='L')
            x,y,_,_ = font.getbbox(char)
            out.write(struct.pack('<IBbbBB',code,round(font.getlength(char)),x,y,*mask.size))
            out.write(bytes(mask))
