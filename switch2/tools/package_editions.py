#!/usr/bin/env python3
"""Package and verify four NRO editions from one compiled ELF and NACP."""
import argparse
from hashlib import sha256
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

SWITCH = Path(__file__).resolve().parents[1]
IMAGE = "devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282"
EDITION_NAMES = {
    "2d": "PKForge-2D.nro",
    "portraits": "PKForge-Portraits.nro",
    "aio": "PKForge-AIO.nro",
    "animated": "PKForge-AIO-Animated.nro",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("atlas", type=Path, help="Directory with idle.art and idle.idx")
    parser.add_argument("output", type=Path, help="New directory for the release")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir()
    checksums = []
    for edition, name in EDITION_NAMES.items():
        with TemporaryDirectory(prefix="pkforge-edition-") as temporary:
            stage = Path(temporary)
            ignored = ["models"]
            if edition in ("2d", "portraits"):
                ignored.append("events")
            if edition == "2d":
                ignored += ["home.art", "home.idx"]
            romfs = stage / "romfs"
            shutil.copytree(SWITCH / "romfs", romfs, ignore=shutil.ignore_patterns(*ignored))
            if edition == "animated":
                for atlas in ("idle.art", "idle.idx"):
                    shutil.copyfile(args.atlas / atlas, romfs / "ui" / atlas)
                with (romfs / "ui/LOCAL-MEDIA-CREDITS.md").open("a") as out:
                    out.write("\n## Animated Pokémon portraits\n\n"
                        "Showdown sprite artists and Smogon, via https://github.com/PokeAPI/sprites. "
                        "Converted to a local animation atlas. Artist credits: https://pokemonshowdown.com/credits. "
                        "Pokémon characters and artwork belong to their respective rights holders, including "
                        "Nintendo, Creatures Inc., and GAME FREAK Inc.\n")
            subprocess.run([
                "docker", "run", "--rm", "--platform", "linux/amd64", "--user", f"{os.getuid()}:{os.getgid()}",
                "--entrypoint", "/opt/devkitpro/tools/bin/elf2nro",
                "-v", f"{SWITCH}:/build:ro", "-v", f"{stage}:/stage", IMAGE,
                "/build/PKForge.elf", "/stage/PKForge.nro", "--icon=/build/icon.jpg",
                "--nacp=/build/PKForge.nacp", "--romfsdir=/stage/romfs",
            ], check=True, stdout=subprocess.DEVNULL)
            binary = stage / "PKForge.nro"
            subprocess.run([sys.executable, str(SWITCH / "tests/verify_nro.py"), str(binary),
                            "--edition", edition, "--romfs", str(romfs)], check=True)
            data = binary.read_bytes()
            (output / name).write_bytes(data)
            checksums.append(f"{sha256(data).hexdigest()}  {name}")
            print(name, len(data), flush=True)
    (output / "SHA256SUMS").write_text("\n".join(checksums) + "\n")
    shutil.copyfile(SWITCH / "romfs/THIRD-PARTY-NOTICES.txt", output / "THIRD-PARTY-NOTICES.txt")


if __name__ == "__main__":
    main()
