#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work="$PWD/switch2/build/managed-dist"
mkdir -p "$work"
sourcefile=external/PKHeX-Plugins/PKHeX.Core.AutoMod/AutoMod/APILegality.cs
python3 - "$sourcefile" "$work/APILegality.cs" <<'PY'
from pathlib import Path
import sys
original = Path(sys.argv[1]).read_text()
start = original.index("        var task = Task.Run(GetLegal);")
end = original.index("\n    }", start)
dest=Path(sys.argv[2])
updated=original[:start] + "        return GetLegal();" + original[end:]
if not dest.exists() or dest.read_text()!=updated:
    dest.write_text(updated)
PY
# Provide the engine's optional distribution tables from the user's SD media.
python3 - "$work/ResourceUtil.cs" <<'RESOURCE'
from pathlib import Path
import sys
root = Path("external/PKHeX/PKHeX.Core")
names = [stem + ".pkl" for stem in "event1 event2 wc4 pgf wc6 wc6full wc7 wc7full wb7full wc8 wb8 wa8 wc9 wa9".split()]
cases = " or ".join('"' + name + '"' for name in names)
source = (root/"Util/ResourceUtil.cs").read_text()
marker = "        if (!ResourceCache.TryGetBinaryResource(name, out var result))"
assert source.count(marker) == 1
source = source.replace(marker, f"""        if (name is {cases})
        {{
            var path = "/mgdb-" + name;
            return System.IO.File.Exists(path) ? System.IO.File.ReadAllBytes(path) : [];
        }}
{marker}""")
Path(sys.argv[1]).write_text(source)
RESOURCE
dotnet build -m:1 switch2/managed/worker/ForgeTools.csproj -c Debug -o "$work/out" \
    -p:CustomAfterMicrosoftCommonTargets="$PWD/switch2/managed/crypto/Portable.targets" -p:Optimize=true -p:DebugType=None -p:DebugSymbols=false -p:SwitchResourceSource="$work/ResourceUtil.cs" -p:SwitchLegalizerSource="$work/APILegality.cs"
dest=switch2/romfs/engine
mkdir -p "$dest/managed"
cp "$work/out/AppBundle"/{icudt.dat,runtimeconfig.bin} "$dest/"
cp "$work/out/AppBundle/managed/"*.dll "$dest/managed/"
