"""Package tagged source and pinned build dependencies. Optional atlas files are release inputs."""
import argparse
import gzip
import subprocess
import tarfile
from io import BytesIO
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('ref')
parser.add_argument('output',type=Path)
parser.add_argument('--atlas',type=Path,help='Directory containing the generated idle.art and idle.idx')
args=parser.parse_args()
def git(repo,*argv):
    return subprocess.check_output(['git','-C',str(repo),*argv])
def members(repo,ref,paths=()):
    for entry in git(repo,'ls-tree','-r','-z',ref,'--',*paths).split(b'\0'):
        if entry:
            info,path=entry.split(b'\t',1)
            mode,kind,oid=info.decode().split()
            yield path.decode(),mode,kind,oid

# Use one persistent reader per repository to handle thousands of source files.
def add_tree(archive,repo,ref,prefix,paths=()):
    with subprocess.Popen(['git','-C',str(repo),'cat-file','--batch'],stdin=subprocess.PIPE,stdout=subprocess.PIPE) as blobs:
        for name,mode,kind,oid in members(repo,ref,paths):
            if kind!='blob' or name.startswith('PKHeX.Core/Resources/legality/mgdb/'):
                continue
            blobs.stdin.write((oid+'\n').encode()); blobs.stdin.flush()
            blob,typ,count=blobs.stdout.readline().split()
            data=blobs.stdout.read(int(count))
            if blob.decode()!=oid or typ!=b'blob' or blobs.stdout.read(1)!=b'\n':
                raise ValueError('Invalid source object: '+name)
            info=tarfile.TarInfo('PKForge-Switch2/'+prefix+name)
            if mode=='120000':
                info.type=tarfile.SYMTYPE; info.linkname=data.decode()
                archive.addfile(info)
            else:
                info.mode=int(mode,8)&0o777
                info.size=len(data); archive.addfile(info,BytesIO(data))
        blobs.stdin.close()
submodules={n:o for n,m,k,o in members(root,args.ref) if k=='commit'}
with args.output.open('wb') as f, gzip.GzipFile(fileobj=f,mode='wb',filename='',mtime=0) as gz, tarfile.open(fileobj=gz,mode='w|') as out:
    add_tree(out,root,args.ref,'')
    for name,project in [('external/PKHeX','PKHeX.Core'),('external/PKHeX-Plugins','PKHeX.Core.AutoMod')]:
        add_tree(out,root/name,submodules[name],name+'/',(project,'Directory.Build.props','LICENSE','README.md'))
    if args.atlas:
        for name in ('idle.art','idle.idx'):
            path=args.atlas/name
            info=tarfile.TarInfo('PKForge-Switch2/switch2/release-inputs/'+name)
            info.size=path.stat().st_size
            with path.open('rb') as source:
                out.addfile(info,source)
