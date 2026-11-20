#!/usr/bin/env python3
"""Recreate the vendored Mono wasm2c source archive with WABT 1.0.39."""
import argparse
import io
import lzma
import subprocess
import tarfile
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("component", type=Path, help=".NET 10.0.15 wasi-wasm native/dotnet.wasm")
parser.add_argument("wabt", type=Path, help="Unpacked WABT 1.0.39 binary distribution")
args = parser.parse_args()
destination = Path(__file__).resolve().parents[1] / "managed/runtime"
component = args.component.read_bytes()


def leb(offset):
    value = shift = 0
    while offset < len(component):
        byte = component[offset]
        offset += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, offset
        shift += 7
    raise ValueError("Incomplete component section")


modules = []
pos = 8
while pos < len(component):
    section = component[pos]
    length, start = leb(pos + 1)
    pos = start + length
    if section == 1:
        modules.append(component[start:pos])
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    core = root / "mono.wasm"
    core.write_bytes(max(modules, key=len))
    compiler = args.wabt.resolve() / "bin/wasm2c"
    if subprocess.check_output([compiler, "--version"], text=True).strip() != "1.0.39":
        raise ValueError("Use WABT 1.0.39")
    subprocess.run([compiler, "--no-debug-names", "--enable-exceptions", "--num-outputs=48",
                    "-n", "engine", core, "-o", root / "engine.c"], check=True)
    (destination / "engine.h").write_bytes((root / "engine.h").read_bytes())
    for pattern in ("wasm-rt*.c", "wasm-rt*.inc", "wasm-rt*.h"):
        for source in args.wabt.rglob(pattern):
            data = source.read_text()
            data = data.replace("#else\n#include <sys/mman.h>", "#elif WASM_RT_USE_MMAP\n#include <sys/mman.h>")
            data = data.replace("#ifndef _WIN32\n#define WASM_RT_SETJMP_SETBUF", "#if !defined(_WIN32) && !defined(__NEWLIB__)\n#define WASM_RT_SETJMP_SETBUF")
            data = data.replace("#ifndef _WIN32\n#define WASM_RT_LONGJMP_UNCHECKED", "#if !defined(_WIN32) && !defined(__NEWLIB__)\n#define WASM_RT_LONGJMP_UNCHECKED")
            target = destination if source.suffix == ".h" else root
            (target / source.name).write_text(data)
    sources = sorted([*root.glob("*.c"), *root.glob("*.inc"), root / "engine-impl.h"])
    with lzma.open(destination / "runtime-source.tar.xz", "wb", preset=2) as packed:
        with tarfile.open(fileobj=packed, mode="w|", format=tarfile.USTAR_FORMAT) as archive:
            for source in sources:
                data = source.read_bytes()
                entry = tarfile.TarInfo(source.name)
                entry.size = len(data)
                archive.addfile(entry, io.BytesIO(data))
subprocess.run(["python3", destination / "make_imports.py"], check=True)
