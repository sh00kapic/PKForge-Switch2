# PKForge for Switch 2

A native Switch 2 homebrew port of [PKForge](https://github.com/sofianeelhor/PKForge), built with devkitA64 and ouncebrew's libounce. It edits Pokémon save copies offline on the SD card. The controller interface adapts the Android app's cobalt game shelf, split storage and inspector views, and button hints.

## Install and test

1. Copy `PKForge.nro` to `/switch/PKForge/PKForge.nro` and launch it from the Switch 2 homebrew menu.
2. Place an exported save in `/switch/PKForge/saves/` or one folder below it. Press **X** on the Games shelf to refresh and **A** to open it.
3. Browse boxes with **L/R** and party with **X**. Press **A** for a Pokémon summary or **−** from Boxes for Trainer.
4. Press **+** to export an edited copy. Exports preserve the filename in a new folder under `/switch/PKForge/exports/`.

## Local media

The v0.7.4 release offers four editions. Each comes with pixel sprites and game covers. Portraits adds HOME artwork. AIO adds Wonder Cards and event legality tables. AIO Animated also includes Showdown animation loops. Save editing works offline with an empty SD media folder. Editions without the gift catalog display setup guidance until you supply cards. Event encounter checks depend on the local distribution tables and may flag legitimate event Pokémon when those tables are absent.

From the repository root, import a compatible 0.7.1-format NRO that you already possess and have permission to use. The installer copies only the portrait atlases, game icons and card catalogs into a new `/switch/PKForge/media/` folder. It uses local files exclusively. Supply `--mgdb` with a local PKHeX-format table directory if you have one you may use. This can also be installed separately.

```sh
python3 switch2/tools/import_media.py "/path/to/SD" --nro "/path/to/local-media.nro"
python3 switch2/tools/import_media.py "/path/to/SD" --mgdb "/path/to/local/mgdb"
```

To build a catalog from your own individual Wonder Cards, initialize the pinned submodules and use the .NET 10 SDK. Choose a new output directory, then copy the resulting files into `/switch/PKForge/media/events/`. The NRO reads each generation independently.

```sh
dotnet run --project switch2/tools/GiftIndex -- "/path/to/your/cards" "/new/output/events"
```

For individual portraits put a local WebP in `/switch/PKForge/portraits/`, named by species and form (for example `25-0.webp`). For a shiny look add `-s` before the extension. Alcremie uses species, form and sweet, for example `869-8-6-s.webp`. See [rigged models](#rigged-models) for animated meshes. Click the right stick to toggle motion. **ZL + ZR** filters long lists, and **X** also searches the Mystery Gift album.

## Editor and tools

- **Pokémon editor**. Inspect a party or boxed Pokémon and press **X**. On the **Shiny** row, press Left or Right to toggle it, then **A** to apply the draft. Gen I stores transfer shininess through DVs, and Gen II uses DVs in-game. In the editor, **X** edits the nickname, **ZR** edits the original trainer name, and **ZL** opens ribbons and marks. **B** cancels the draft. Party stat edits recalculate the stats and preserve damage and fainting. Press **Y** on the summary to copy the Pokémon to the Bank or **ZL** to copy it to Poképark.
- **Bank**. Press **Y** from Boxes, then **ZL + ZR** to search. Press **A** on an inspected entry to select it for paste. Select an empty box slot and press **ZR** to preview and confirm any supported conversion.
- **Trainer**. Use the D-pad to select and adjust money, raw trainer and secret IDs, gender, or play time. Press **X** to rename and **A** to apply. Press **Y** for the full card, named OT profiles, generation ownership, currencies, badges, dates, geography and game records exposed by that format.
- **Encounter and legality tools**. Choose **ZL → Advanced save tools** from Boxes, or press **−** on a party or box summary. Browse encounter cards by species name or ID and optional form, generate a Showdown set with AutoMod, or review a full party and box sweep. The selected Pokémon has a report, fixes, RNG controls, form data, memories, handler fields and technical records where supported. Format-specific fields include learnsets, relearn moves, PP, held items, contest stats, size and Legends: Arceus Move Shop records. Press **X** to accept a staged edit, **B** to discard it and **+** to export. Long lists support **ZL + ZR**.
- **Editing modes**. Standard uses the ordinary game choices. HaX expands the available choices where the format can store them. Hardcore allows management, including box moves, releases, snapshots and exports. A confirmed Hardcore Bank move exports the edited save immediately and consumes the source slot or Bank entry. Clipboard copies and Pokémon edits are blocked. Settings persist on the SD card.
- **Save tools**. Press **ZL** from Games or Boxes. In Bag, **Y** adds an item, **−** opens stack removal and refill actions, and **X** stores a stack in the Item Bank. Refills cover supported standard pouches and respect stack limits. Bag actions can also save named presets and restore their item quantities to saves from the same game.
- **Box tools**. Mark individual entries with **Y** or all occupied entries with **X**. Press **A** for actions. Box edits are staged until export. Releases require review.
- **Living Dex**. Press **A** to plan a same-save box, **L** to review a move plan from another save, or **R** to register owned species where supported. A cross-save export writes separate source and destination copies.
- **Snapshots**. Press **X** to name a snapshot of the open save. Press **A** to compare an existing snapshot or restore it to a separate output copy.
- **Showdown**. Press **A** or **Y** for the party or selected Pokémon QR. Press **X** to export the text. To import up to six sets, place them in `/switch/PKForge/imports/showdown.txt`, highlight an empty box slot, and press **ZL** in Showdown to review the destination slots. Supported alternate forms and Scarlet/Violet Tera types are accepted.
- **Mystery Gifts**. Queue eligible Gen IV–VII cards or stage supported Let's Go, Sword/Shield, Scarlet/Violet, and Legends: Z-A Pokémon and item gifts. Browse the offline Wonder Card album by title, Pokémon, OT, move or item. **Y** changes compatibility, gift type, shininess, session status, language, copies, sort, grouping and archive. **+** cycles sort in the album. **A** opens a card, **L/R** chooses the language copy, and **X** queues it. Return to Boxes and press **+** to export.
- **Nursery and Hall of Fame**. Day Care and breeding lists the facilities and parents stored by the open game. Sword and Shield have separate Route 5 and Bridge Field Nurseries. Select a Pokémon’s egg tools to deposit it, choose a compatible second parent, set egg moves or hatch it. The egg factory and generation-specific options appear for games that support them. Full GBA save copies can display their recorded Hall of Fame teams. GBA clock edits the initial and elapsed values used by Ruby, Sapphire, and Emerald.
- **Poképark journal**. Press **ZL** in Poképark to review your selected resident's saved feeds, play sessions, and walks.

## Save support

The parser covers the mainline Game Boy, Game Boy Advance, Nintendo DS, and Nintendo 3DS entries, Let's Go, Sword/Shield, Brilliant Diamond/Shining Pearl, Legends: Arceus, Scarlet/Violet, and supported Legends: Z-A revisions. It looks for files named `main`, or with a `.sav`, `.srm`, `.dsv`, `.bin`, `.dat`, or `.gci` extension.

Valid Game Boy, Let's Go, and supported Legends: Z-A files support editing and checksummed export. Brilliant Diamond and Shining Pearl 1.0–1.3 support checksummed editing and PB8 records. For GameCube, use an individual GCI export from Colosseum, XD, or Pokémon Box. Their party and storage are available for browsing. Pokémon Box supports Bank copies. The [feature checklist](FEATURE-CHECKLIST.md) tracks Android parity and hardware verification.

## Rigged models

The portrait panel can render rigged models from the SD card. Use Blender 4 to
convert a `.glb` or `.gltf` you have permission to use. The converter samples
the active animation and keeps the mesh, skin weights, and base-color textures.

```sh
blender -b --python-exit-code 1 --python tools/pack_model.py -- model.glb 25-0.pfm
```

Run the command from `switch2/`. Put the converted file in `/switch/PKForge/models/`
on the SD card. Name it by National Dex number and form. For example, `25-0.pfm`
is regular Pikachu, `25-0-s.pfm` is shiny, and `869-8-6.pfm` is Alcremie Rainbow
Swirl with a Ribbon Sweet. A female-specific model uses `-f` before `-s` if both
apply. A Gen VI Pikachu cosplay model uses `c` after the form number. Select the
matching Pokémon to display the model. The renderer uses number badges when no matching local artwork is installed.

Use `--action NAME` for a named clip on a single armature, or `--start FRAME`
and `--end FRAME` to select a loop. The converter accepts up to 16,384
vertices, 32,768 triangles, 256 skin joints, and 240 animation samples.

## Build

With devkitPro and [ounce-devkit](https://github.com/ouncebrew/ounce-devkit) installed, run `make -j` in this directory. The result is `PKForge.nro`. The source distribution includes the runtime C sources and managed assemblies for an offline NRO build.

For the pinned Linux container build, run `./build-container.sh -j8` with Docker, curl, and sha256sum. The script verifies the ounce-devkit release archive and builds as your user.

To rebuild the managed worker, initialize the two pinned submodules with `git submodule update --init`, install .NET SDK 10.0.404 and the `wasi-experimental` workload, then run `./tools/build_engine.sh`. The script builds PKForge.Engine, PKHeX.Core and AutoMod for the console. The runtime uses .NET Mono 10.0.15 translated with WABT 1.0.39. To regenerate the translation run `python3 tools/build_runtime.py /path/to/native/dotnet.wasm /path/to/wabt-1.0.39`. Runtime and cryptography licenses are under `romfs/engine/licenses`.

Run the host tests with `make -f tests/Makefile -j8` using GCC 14 or later. Run `python3 tests/verify_nro.py PKForge.nro` to check the binary layout and assets.

To run the advanced round trips from this directory, build `make -f tests/Makefile -j8 build/host-test/engine-runner` and run `python3 tests/advanced.py /path/to/compatibility-samples`. The test reads the Ruby, 64 KiB Ruby, X, Sword and Violet fixtures used in the save compatibility report and writes temporary copies.

Regenerate the base font atlases and PKForge launcher icon with `python3 switch2/tools/pack_ui.py`. Regenerate album glyphs with `python3 switch2/tools/pack_album_font.py` using Pillow, fontTools and Noto Sans CJK. Regenerate third-party notices with `python3 switch2/tools/pack_notices.py`.

## Source and credits

The Android project is by sofianeelhor and contributors. The launcher uses the PKForge logo by spritedmistery. The native engine is [FlagBrew's PKSM-Core](https://github.com/FlagBrew/PKSM-Core) at `d9bca6fcdf622760a0aa963565b102276e2ae2a3`. Its bundled dependencies are memecrypto at `bd3b16ee2c12bd9ee353cf3098cb998e7880638c` and pcg-cpp at `428802d1a5634f96bcd0705fab379ff0113bcf13`.

The BDSP personal table, GameCube save checksums and layouts, and GBA Hall of Fame and clock layouts follow [PKHeX](https://github.com/kwsch/PKHeX), distributed under GPLv3. QR codes use [Nayuki's QR Code generator](https://github.com/nayuki/QR-Code-generator) under the MIT license. The Switch 2 application is GPLv3 or later.

To reproduce the corresponding-source tarball from the tagged checkout, initialize the pinned submodules, then run `python3 switch2/tools/source_release.py v0.7.3 PKForge-source-v0.7.3.tar.gz` from the repository root. The tarball includes the shared libraries' build sources. For desktop-only upstream tests that examine the full upstream resource directories, use a checkout with submodules.
