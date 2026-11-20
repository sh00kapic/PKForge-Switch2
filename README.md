# PKForge for Switch 2

An offline Pokémon save editor for Nintendo Switch 2, ported from [PKForge](https://github.com/sofianeelhor/PKForge) using libounce. Browse your party and boxes, edit trainer and bag data, receive Mystery Gifts, and check encounter legality directly on the console.

[Download the homebrew](https://github.com/sh00kapic/PKForge-Switch2-Homebrew/releases) · [Install and use](switch2/README.md) · [Feature checklist](switch2/FEATURE-CHECKLIST.md)

Copy `PKForge.nro` to `/switch/PKForge/PKForge.nro` on the SD card. Put save backups in `/switch/PKForge/saves/`. Press **+** from Boxes to export an edited copy.

## Build the NRO

With Docker running on Linux, run the following from a checkout.

```sh
sh switch2/build-container.sh -j2
python3 switch2/tests/verify_nro.py switch2/PKForge.nro
```

The build uses the pinned ouncebrew SDK and the runtime and resources checked into the port. The output is `switch2/PKForge.nro`. For native devkitA64 builds, host tests and optional portrait packs, see the [port guide](switch2/README.md).

The native application, tools and RomFS are in `switch2/`. The shared save and legality engine is in `src/`, its pinned dependencies in `external/`, and source fonts in `assets/`. Initialize the submodules to rebuild or test the managed engine.

The standard release displays Pokédex numbers. Portraits, game logos, and Wonder Cards use optional local files. See [local media setup](switch2/README.md#local-media).

PKForge is distributed under the [GPL](LICENSE). Bundled assets have their own [credits and licenses](switch2/romfs/ui/ATTRIBUTION.md). See [source provenance](PROVENANCE.md) and the [binary notices](switch2/romfs/THIRD-PARTY-NOTICES.txt).
