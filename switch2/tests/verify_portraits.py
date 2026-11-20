#!/usr/bin/env python3
"""Check the bundled portrait index, including every Alcremie look."""

from pathlib import Path
import struct
import sys

root = Path(__file__).resolve().parents[1]
ui = Path(sys.argv[1])
species = (root / "romfs/strings/eng/species.txt").read_text(encoding="utf-8-sig").splitlines()
known = {}
for name in ("home", "idle"):
    index = (ui / (name + ".idx")).read_bytes()
    entries = list(struct.iter_unpack("<II", index))
    assert entries and all(a[0] < b[0] for a, b in zip(entries, entries[1:]))
    with (ui / (name + ".art")).open("rb") as art:
        (count,) = struct.unpack("<I", art.read(4))
        offsets = struct.unpack(f"<{count + 1}I", art.read(4 * (count + 1)))
        assert offsets[0] == 0 and all(a < b for a, b in zip(offsets, offsets[1:]))
        assert art.tell() + offsets[-1] == (ui / (name + ".art")).stat().st_size
        assert all(row < count for _, row in entries)
    for key, row in entries:
        known[key] = (name, row)

assert 0 in known  # Egg.
for species_id in range(1, len(species)):
    assert species_id << 15 in known, f"Missing normal portrait for species {species_id}"
    assert species_id << 15 | 2 in known, f"Missing shiny portrait for species {species_id}"

for form in range(9):
    for palette in (0, 2):
        looks = {known[869 << 15 | form << 7 | sweet << 4 | palette] for sweet in range(7)}
        assert len(looks) == 7, f"Duplicate sweet artwork for Alcremie form {form}, palette {palette}"

print(f"Portrait index covers {len(species) - 1} species in both palettes and 126 Alcremie looks.")
