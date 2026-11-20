"""Generate the exact notice file distributed beside and inside the executable."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
parts = [
    ("PKForge, PKHeX, PKSM-Core and memecrypto — GPLv3 or later",
     root / "romfs/engine/licenses/GPL-3.0.txt"),
    ("PKHeX-Plugins / Auto Legality Mod", root / "licenses/AutoMod-MIT.txt"),
    ("libounce", root / "licenses/libounce-ISC.txt"),
    ("libnx", root / "licenses/libnx-ISC.txt"),
    ("mimalloc", root / "licenses/mimalloc-MIT.txt"),
    ("Mono / .NET", root / "romfs/engine/licenses/dotnet-MIT.txt"),
    (".NET third-party components", root / "romfs/engine/licenses/dotnet-third-party.txt"),
    ("WABT wasm2c", root / "romfs/engine/licenses/wabt.txt"),
    ("Bouncy Castle", root / "romfs/engine/licenses/bouncycastle.md"),
    ("PCG C++ — MIT option", root / "vendor/pcg-cpp/LICENSE-MIT.txt"),
    ("LZ4", root / "vendor/lz4/LICENSE"),
    ("libwebp", root / "vendor/libwebp/COPYING"),
    ("QR Code generator", root / "licenses/qrcodegen-MIT.txt"),
    ("PUSE data tables", root / "licenses/PUSE-MIT.txt"),
    ("PokéAPI data", root / "licenses/PokeAPI-BSD.txt"),
    ("M PLUS Rounded 1c", root / "romfs/ui/FONT-LICENSE.txt"),
    ("DotGothic16", root / "romfs/ui/DotGothic16-OFL.txt"),
    ("Noto Sans CJK", root / "romfs/ui/NOTO-LICENSE.txt"),
]
preface = """PKForge for Switch 2
Third-party notices

Switch 2 port of PKForge by sofianeelhor and contributors.
PKForge launcher logo by spritedmistery, resized and converted to JPEG for Switch 2.
Source, changes and build instructions: https://github.com/sh00kapic/PKForge-Switch2
Upstream: https://github.com/sofianeelhor/PKForge
PKHeX by Kaphotics and contributors. PKSM-Core by FlagBrew and contributors.
The port, shared engine and modified sources are distributed under GPLv3 or later.
The following components and fonts carry their own notices.
"""
data = preface.encode("utf-8")
for name, path in parts:
    data += ("\n\n" + name + "\n\n").encode() + path.read_bytes()
dest = root / "romfs/THIRD-PARTY-NOTICES.txt"
if "--check" in sys.argv:
    if dest.read_bytes() != data:
        sys.exit("Regenerate THIRD-PARTY-NOTICES.txt with tools/pack_notices.py")
else:
    dest.write_bytes(data)
