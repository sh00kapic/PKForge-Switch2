"""Install locally supplied media and optional event legality tables onto an SD card."""
import argparse
from pathlib import Path
from tempfile import TemporaryDirectory

from nro_romfs import files

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('sd',type=Path,help='Mounted SD card root')
parser.add_argument('--nro',type=Path,help='Local NRO containing compatible media you may use')
parser.add_argument('--mgdb',type=Path,help='Local PKHeX event table directory you may use')
args=parser.parse_args()
if not args.nro and not args.mgdb:
    parser.error('Choose --nro, --mgdb or both.')
if not args.sd.is_dir():
    parser.error('Choose an existing mounted SD card directory.')
app=args.sd/'switch/PKForge'
app.mkdir(parents=True,exist_ok=True)
atlases={f'ui/{n}.art' for n in ('species','shiny','chrome','home','games','idle')} | {'ui/home.idx','ui/idle.idx'}
tables={f'events/gen{g}.{e}' for g in range(4,10) for e in ('bin','tsv','extra')}
pkls='event1 event2 wc4 pgf wc6 wc6full wc7 wc7full wb7full wc8 wb8 wa8 wc9 wa9'.split()
with TemporaryDirectory(dir=app) as tmp:
    stage=Path(tmp)
    if args.nro:
        archive=files(args.nro)
        required=atlases|tables
        if not required.issubset(archive):
            parser.error('The supplied NRO does not contain a complete compatible atlas and card catalog.')
        for key in sorted(required):
            out=stage/key
            out.parent.mkdir(exist_ok=True)
            out.write_bytes(archive[key])
    if args.mgdb:
        out=stage/'events/mgdb'
        out.mkdir(parents=True,exist_ok=True)
        for stem in pkls:
            src=args.mgdb/(stem+'.pkl')
            if not src.is_file() or src.stat().st_size>16*1024*1024:
                parser.error(f'Missing or oversized table: {src.name}')
            (out/src.name).write_bytes(src.read_bytes())
    dest=app/'media'
    if not dest.exists():
        stage.rename(dest)
    elif args.mgdb and not args.nro and not (dest/'events/mgdb').exists():
        (dest/'events').mkdir(exist_ok=True)
        (stage/'events/mgdb').rename(dest/'events/mgdb')
    else:
        parser.error('A media installation already exists. Move it aside before replacing it.')
print('Installed local media in',dest)
