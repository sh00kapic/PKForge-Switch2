"""Generate the bindings for unused WASI capabilities in the offline Mono host."""
import re
from pathlib import Path
root = Path(__file__).parent
header = (root / 'engine.h').read_text()
implemented = set(re.findall(r'WASI\((\w+)\)', (root / '../../source/engine_host.cpp').read_text()))
lines = ['/* Generated from the pinned runtime imports by make_imports.py. */']
for ret, name, params in re.findall(r'/\* import: [^\n]+\*/\n(\w+) (\w+)\(([^)]+)\);', header):
    if name.removeprefix('w2c_wasi__snapshot__preview1_') in implemented:
        continue
    if name.startswith('w2c_wasi__snapshot__preview1_') and ret == 'u32':
        body = 'return 52;'  # WASI ENOSYS
    else:
        body = 'throw std::runtime_error("Unsupported offline runtime capability.");'
    lines.append(f'{ret} {name}({params}) {{ {body} }}')
(root / 'unused_imports.inc').write_text('\n'.join(lines)+'\n')
