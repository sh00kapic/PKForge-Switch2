"""Read regular files from an NRO's embedded RomFS without extracting paths."""
import struct
from pathlib import Path


def files(nro):
    data = memoryview(Path(nro).read_bytes())
    if data[16:20] != b'NRO0':
        raise ValueError('Expected an NRO executable')
    aset, = struct.unpack_from('<I', data, 24)
    if data[aset:aset+4] != b'ASET':
        raise ValueError('Missing NRO asset section')
    offset, size = struct.unpack_from('<QQ', data, aset+40)
    rom = data[aset+offset:aset+offset+size]
    if len(rom) != size:
        raise ValueError('Truncated RomFS')
    header, _, _, dirs, dir_size, _, _, entries, entry_size, content = struct.unpack_from('<10Q', rom)
    if header != 80 or dirs+dir_size > size or entries+entry_size > size or content > size:
        raise ValueError('Invalid RomFS directory')
    stop = 0xffffffff
    seen_dirs, seen_files = set(), set()
    result = {}
    def name(table, limit, pos, length):
        if pos+length > limit:
            raise ValueError('Invalid RomFS name length')
        value = bytes(rom[table+pos:table+pos+length]).decode('utf-8')
        if value in ('.','..') or '/' in value or '\\' in value or '\x00' in value:
            raise ValueError('Invalid RomFS name')
        return value
    pending = [(0,'')]
    while pending:
        pos, prefix = pending.pop()
        if pos in seen_dirs or pos+24 > dir_size:
            raise ValueError('Invalid RomFS directory link')
        seen_dirs.add(pos)
        _, sibling, child, file, _, length = struct.unpack_from('<6I',rom,dirs+pos)
        dn = name(dirs,dir_size,pos+24,length)
        if sibling != stop:
            pending.append((sibling,prefix))
        path = prefix+dn+'/' if dn else prefix
        if child != stop:
            pending.append((child,path))
        while file != stop:
            if file in seen_files or file+32 > entry_size:
                raise ValueError('Invalid RomFS file link')
            seen_files.add(file)
            _, link, start, length, _, n = struct.unpack_from('<IIQQII',rom,entries+file)
            key = path+name(entries,entry_size,file+32,n)
            if content+start+length > size or key in result:
                raise ValueError('Invalid RomFS file')
            result[key] = rom[content+start:content+start+length]
            file = link
    return result
