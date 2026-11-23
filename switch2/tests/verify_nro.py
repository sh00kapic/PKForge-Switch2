#!/usr/bin/env python3
"""Validate the homebrew header and all embedded assets."""
import struct
import sys
from pathlib import Path

nro = Path(sys.argv[1]).read_bytes()
assert nro[0x10:0x14] == b"NRO0", "Missing NRO0 header"
image_size = struct.unpack_from("<I", nro, 0x18)[0]
segments = [struct.unpack_from("<II", nro, off) for off in (0x20, 0x28, 0x30)]
end = 0
for start, size in segments:
    assert start >= end and size > 0
    end = start + size
assert end == image_size
assert nro[image_size:image_size + 4] == b"ASET", "Missing asset header"
assets = []
end = 56
for index in range(3):
    start, size = struct.unpack_from("<QQ", nro, image_size + 8 + 16 * index)
    assert start >= end and size > 0
    end = start + size
    assets.append(nro[image_size + start:image_size + end])
assert image_size + end == len(nro)

icon, nacp, romfs = assets
assert icon.startswith(b"\xff\xd8")
assert icon == (Path(__file__).resolve().parents[1] / "icon.jpg").read_bytes(), "Embedded launcher icon differs from icon.jpg"
assert len(nacp) == 0x4000 and nacp.startswith(b"PKForge\0")
assert b"0.7.3\0" in nacp
assert b"species.txt" in romfs and b"personal3" in romfs and b"personal8b" in romfs
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from nro_romfs import files
entries = files(sys.argv[1])
allowed_ui = {'ui/' + n + '.font' for n in ('small','body','title','hero','album','caption')}
allowed_ui.update('ui/'+n for n in ('ATTRIBUTION.md','DotGothic16-OFL.txt','FONT-LICENSE.txt','NOTO-LICENSE.txt','WEBP-LICENSE.txt'))
expected_events = set()
if "--aio" in sys.argv[2:]:
    allowed_ui.update("ui/" + name for name in (
        "species.art", "shiny.art", "chrome.art", "home.art", "home.idx",
        "games.art", "LOCAL-MEDIA-CREDITS.md",
    ))
    expected_events = {f"events/gen{gen}.{ext}" for gen in range(4, 10)
        for ext in ("bin", "extra", "tsv")}
    expected_events.update(f"events/mgdb/{name}.pkl" for name in
        "event1 event2 wc4 pgf wc6 wc6full wc7 wc7full wb7full wc8 wb8 wa8 wc9 wa9".split())
    expected_events.update({"events/ATTRIBUTION.md", "events/mgdb/PKHeX-LICENSE.txt"})
    romfs_dir = Path(__file__).resolve().parents[1] / "romfs"
    for name in allowed_ui | expected_events:
        assert bytes(entries[name]) == (romfs_dir / name).read_bytes(), name
assert {n for n in entries if n.startswith("ui/")} == allowed_ui
assert {n for n in entries if n.startswith("events/")} == expected_events
assert 'engine/managed/PKHeX.Core.dll' in entries and 'engine/managed/ForgeTools.dll' in entries
notice = Path(__file__).resolve().parents[1] / 'romfs/THIRD-PARTY-NOTICES.txt'
assert bytes(entries['THIRD-PARTY-NOTICES.txt']) == notice.read_bytes()
for name in ('libounce-ISC','libnx-ISC','mimalloc-MIT','AutoMod-MIT'):
    assert (notice.parents[1]/'licenses'/(name+'.txt')).read_bytes() in notice.read_bytes()
assert b"romfs:/personals/" in nro[:image_size]
print(f"Verified NRO header, executable segments, icon, NACP and RomFS ({len(nro):,} bytes).")
