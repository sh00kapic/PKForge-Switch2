#!/usr/bin/env python3
"""Convert mapped Showdown GIFs to PKForge's LZ4 idle atlas."""
import argparse
from bisect import bisect_right
from concurrent.futures import ThreadPoolExecutor
from io import BytesIO
from pathlib import Path
import re
import struct
import time
from urllib.request import urlopen

from PIL import Image, ImageSequence
import lz4.block
import numpy as np

REVISION = "c0542f7c51215d9672439012838c99f520596cbd"
SOURCE = f"https://raw.githubusercontent.com/PokeAPI/sprites/{REVISION}/sprites/pokemon/other/showdown/"
VARIANTS = ((1, "", 0), (2, "shiny/", 2), (4, "female/", 1), (8, "shiny/female/", 3))
SIDE = 96

def entries(table):
    for line in table.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        name, _, _, stem, mask = line.split("\t")
        match = re.fullmatch(r"(\d+)-(\d+)([cg]?)(?:-(\d+))?", name)
        if not match or not re.fullmatch(r"\d+", stem):
            continue
        species, form, trait, sweet = match.groups()
        key = (int(species) << 15 | int(form) << 7 | int(sweet or 0) << 4
               | (8 if trait == "g" else 4 if trait == "c" else 0))
        for flag, folder, variant in VARIANTS:
            if int(mask) & flag:
                yield key | variant, f"{folder}{stem}.gif"

def fetch(path, cache):
    target = cache / path
    if target.is_file():
        return target.read_bytes()
    for attempt in range(3):
        try:
            with urlopen(SOURCE + path, timeout=12) as response:
                data = response.read(4 * 1024 * 1024 + 1)
            if len(data) > 4 * 1024 * 1024 or not data.startswith((b"GIF87a", b"GIF89a")):
                raise ValueError("Invalid GIF data")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            return data
        except OSError:
            if attempt == 2:
                raise
            time.sleep(attempt + 1)

def clip(data):
    with Image.open(BytesIO(data)) as gif:
        frames, ends = [], []
        duration = 0
        for image in ImageSequence.Iterator(gif):
            frames.append(image.convert("RGBA"))
            duration += max(20, image.info.get("duration", 80) or 80)
            ends.append(duration)
    if len(frames) < 2 or not 40 <= duration <= 65535:
        raise ValueError("No supported loop")
    bounds = [frame.getchannel("A").getbbox() for frame in frames]
    visible = [b for b in bounds if b]
    if not visible:
        raise ValueError("Empty animation")
    left, top = min(b[0] for b in visible), min(b[1] for b in visible)
    right, bottom = max(b[2] for b in visible), max(b[3] for b in visible)
    scale = min((SIDE - 8) / max(right - left, bottom - top), 2)
    w, h = max(1, round((right-left)*scale)), max(1, round((bottom-top)*scale))
    count = min(30, max(2, len(frames), round(duration / 70)))
    strip = Image.new("RGBA", (SIDE, SIDE * count))
    for index in range(count):
        frame = frames[bisect_right(ends, index * duration / count)]
        frame = frame.crop((left, top, right, bottom)).resize((w, h), Image.Resampling.NEAREST)
        strip.paste(frame, ((SIDE-w)//2, index*SIDE+(SIDE-h)//2))
    rgb = Image.new("RGB", strip.size)
    rgb.paste(strip, mask=strip.getchannel("A"))
    indexed = rgb.quantize(colors=255, dither=Image.Dither.NONE)
    palette = indexed.getpalette()
    colors = [0] + [max(1, (r>>3)<<11 | (g>>2)<<5 | b>>3)
                    for r,g,b in zip(palette[0::3],palette[1::3],palette[2::3])][:255]
    colors.extend([0] * (256-len(colors)))
    pixels = np.asarray(indexed, dtype=np.uint8) + 1
    pixels[np.asarray(strip.getchannel("A")) < 96] = 0
    pixels = pixels.reshape(count, SIDE*SIDE)
    if not np.any(pixels[1:] != pixels[:-1]):
        raise ValueError("Frames do not retain visible motion")
    encoded = pixels.copy()
    encoded[1:] ^= pixels[:-1]
    return (struct.pack("<HBB256H", duration,count,SIDE,*colors)
            + lz4.block.compress(encoded.tobytes(), store_size=False))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("table",type=Path)
    parser.add_argument("cache",type=Path)
    parser.add_argument("output",type=Path)
    args=parser.parse_args()
    paths=dict(entries(args.table))
    unique=sorted(set(paths.values()))
    args.output.mkdir(parents=True, exist_ok=True)

    def convert(path):
        try:
            return path,clip(fetch(path,args.cache)),None
        except (OSError,ValueError) as error:
            return path,None,str(error)

    records, failures = {},[]
    with ThreadPoolExecutor(max_workers=20) as pool:
        for i,(path,data,error) in enumerate(pool.map(convert,unique),1):
            if data is None:
                failures.append(f"{path}\t{error}")
            else:
                records[path]=data
            if i%200==0 or i==len(unique):
                print(f"{i}/{len(unique)} checked, {len(records)} loops",flush=True)
    if not records or "155.gif" not in records:
        raise RuntimeError("Cyndaquil animation missing; atlas was not written")
    offsets, ids = [0],{}
    for path,data in records.items():
        ids[path]=len(ids)
        offsets.append(offsets[-1]+len(data))
    with (args.output/"idle.art").open("wb") as art:
        art.write(struct.pack(f"<{len(offsets)+1}I",len(ids),*offsets))
        for data in records.values():
            art.write(data)
    (args.output/"idle.idx").write_bytes(b"".join(
        struct.pack("<II",key,ids[path]) for key,path in sorted(paths.items()) if path in ids))
    (args.output/"showdown-skipped.tsv").write_text("\n".join(failures)+"\n")

if __name__=="__main__":
    main()
