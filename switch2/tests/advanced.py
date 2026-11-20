#!/usr/bin/env python3
"""Round-trip the advanced tools on copies of the public save compatibility fixtures."""
import argparse
import re
import subprocess
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("samples", type=Path, help="Corpus directory with ruby, ruby-64k, x, sword and violet subdirectories")
parser.add_argument("--runner", type=Path, default=Path("build/host-test/engine-runner"))
args = parser.parse_args()


def sample(game):
    return next(path for path in (args.samples / game).iterdir() if path.is_file())


def invoke(directory, source, route, value="", mode="0", refused=False):
    path = directory / f"result-{len(list(directory.iterdir()))}.sav"
    run = subprocess.run([args.runner, source, path, route, value, mode], capture_output=True, text=True, timeout=240)
    if refused:
        assert run.returncode and not path.exists(), route
    elif run.returncode:
        raise AssertionError(f"{route}: {run.stderr}")
    return run.stdout, path


def destination(message):
    box, slot = re.search(r"Box (\d+), slot (\d+)", message, re.IGNORECASE).groups()
    return f"{int(box)-1},{int(slot)-1}"


with tempfile.TemporaryDirectory() as temp:
    folder = Path(temp)
    def call(source, route, *values, **options):
        return invoke(folder, source, route, *values, **options)

    ruby = sample("ruby")
    sweep, _ = call(ruby, "0,0:sweep")
    assert "392 checked" in sweep and "2 with issues" in sweep and "Party 1" in sweep
    before, _ = call(ruby, "-1,0:rng")
    _, rerolled = call(ruby, "-1,0:reroll/7")
    after, _ = call(rerolled, "-1,0:rng")
    assert "Relaxed" in after
    assert next(x for x in before.splitlines() if "\tShiny\t" in x) in after
    generated, copy = call(ruby, "0,0:generate", "Pikachu")
    assert "Legal!" in call(copy, destination(generated) + ":report")[0]
    cards, _ = call(ruby, "0,0:encounters", "Pikachu")
    card = next(line.split("\t")[1] for line in cards.splitlines() if "Safari Zone" in line)
    placed, copy = call(ruby, card)
    assert "Legal!" in call(copy, destination(placed) + ":report")[0]

    made, copy = call(ruby, "0,0:factory", "Pichu")
    egg = destination(made)
    assert "Hatch this egg" in call(copy, egg + ":egg")[0]
    _, copy = call(copy, egg + ":hatch")
    assert "Legal!" in call(copy, egg + ":report")[0]
    compatible, _ = call(ruby, "-1,0:parents")
    pair = next(line.split("\t")[1] for line in compatible.splitlines() if line.startswith("w\t"))
    made, copy = call(ruby, pair)
    assert "Hatch this egg" in call(copy, destination(made) + ":egg")[0]

    sword = sample("sword")
    before, _ = call(sword, "0,0:nursery")
    assert "Route 5 Nursery" in before and "Bridge Field Nursery" in before
    withdraw = next(line.split("\t")[1] for line in before.splitlines() if ":withdraw/" in line)
    moved, copy = call(sword, withdraw, "", "2")
    assert withdraw not in call(copy, "0,0:nursery")[0]
    deposit = withdraw.split(":")[1].replace("withdraw", "depositparent")
    _, copy = call(copy, destination(moved) + ":" + deposit, "", "2")
    assert withdraw in call(copy, "0,0:nursery")[0]

    x = sample("x")
    _, copy = call(x, "0,0:record/0", "4321")
    assert "Steps Taken\t4321" in call(copy, "0,0:records")[0]
    call(x, "0,0:record/0", "111", "2", refused=True)
    _, copy = call(x, "-1,0:ability/1", "", "1")
    assert re.search(r":ability/1\t[^\t]+\tCurrent", call(copy, "-1,0:abilities", "", "1")[0])
    assert "Ability is not valid" in call(copy, "-1,0:report")[0]
    _, repaired = call(copy, "-1,0:legalize")
    assert "Legal!" in call(repaired, "-1,0:report")[0]
    profile = next(line.split("\t")[-1] for line in call(ruby, "0,0:trainer")[0].splitlines() if line.startswith("p\t"))
    _, copy = call(x, "0,0:profile", profile)
    assert "Trainer ID\t15933" in call(copy, "0,0:trainer")[0]

    for game in ["ruby-64k", "violet"]:
        original = sample(game)
        _, copy = call(original, "0,0:identity/money", "54321")
        assert copy.stat().st_size == original.stat().st_size
        assert "Money\t54321" in call(copy, "0,0:trainer")[0]
print("Trainer, modes, encounter, AutoMod, sweep, parent, hatch and Nursery round trips passed.")
