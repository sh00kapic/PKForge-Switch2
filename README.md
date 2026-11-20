# PKForge for Switch 2

An offline Pokémon save editor for Nintendo Switch 2, ported from [PKForge](https://github.com/sofianeelhor/PKForge) using libounce. Browse your party and boxes, edit trainer and bag data, receive Mystery Gifts, and check encounter legality directly on the console.

[Download the homebrew](https://github.com/sh00kapic/PKForge-Switch2/releases) · [Install and use](switch2/README.md) · [Feature checklist](switch2/FEATURE-CHECKLIST.md)

Copy `PKForge.nro` to `/switch/PKForge/PKForge.nro` on the SD card. Put save backups in `/switch/PKForge/saves/`. Press **+** from Boxes to export an edited copy.

## Features in v0.7.2

From Boxes, press **A** to inspect a Pokémon, **X** to open the party, **Y** for the Bank, **−** for Trainer, or **ZL** for Save tools. From a party or box summary, press **X** to edit or **−** for advanced tools. Use **L/R** in Boxes to switch boxes. The fields and actions available depend on the game and save format.

### Saves and snapshots

- **Save library.** Browse and refresh SD backups for the mainline Game Boy, Game Boy Color, Game Boy Advance, DS and 3DS games, plus Let’s Go, Sword/Shield, Brilliant Diamond/Shining Pearl (BDSP) 1.0–1.3, Legends: Arceus, Scarlet/Violet and supported Legends: Z-A revisions. The shelf recognizes `main`, `.sav`, `.srm`, `.dsv`, `.bin`, `.dat` and `.gci` files in the saves folder or one folder below it.
- **Export and restore.** Export a checksummed copy under `/switch/PKForge/exports/` with the original filename. A 64 KiB GBA save keeps its size. Name snapshots, compare party, box, trainer and money changes, and restore a snapshot to a separate file.
- **GameCube.** Browse checksum-verified, read-only GCI saves from Colosseum, XD and Pokémon Box. Pokémon Box records can also be copied to the Bank.

### Pokémon editor

- **Identity and origin.** Edit nickname, original trainer name and IDs, friendship, language, origin game, Poké Ball, met level, locations, met and egg dates, fateful encounter and egg origin. Change forms and supported form details, including Alcremie’s sweet. Set the owner or current handler and edit trainer memories, friendship and affection.
- **Stats and training.** Change level, nature, stat nature from mints, ability, held item, IVs, EVs, Hyper Training and Pokérus. Game Boy records expose DVs and Stat Exp, Let’s Go exposes Awakening Values, and Legends: Arceus exposes effort levels. Party stat recalculation preserves existing damage and fainting.
- **Moves.** Choose moves from the learnset, edit PP, PP Ups and relearn slots, and set or clear technical records. Legends: Arceus also exposes Move Shop purchases and mastery.
- **Shininess and appearance.** Toggle shininess across editable formats, including Game Boy DV-based shininess. Choose the shiny effect where supported. Edit markings, ribbons, marks, contest stats, size, favorite status, fullness, enjoyment and sociability. Game-specific fields include Scarlet/Violet Tera overrides, Dynamax level, Gigantamax factor and Alpha status.

### Trainer, inventory and editing modes

- **Trainer card and profiles.** Edit name, gender, trainer and secret IDs, money, play time and game records. Supported cards also expose rival name, badges, Fly destinations, region, adventure start, Hall of Fame and last-save dates. Currencies include Battle Points, Coins, Watts, Festival Coins, League Points and Blueberry Points. Save, replace, apply or delete named trainer profiles and choose whether generated Pokémon use the save’s trainer.
- **Bag and Item Bank.** Browse pouches, add compatible items, change quantities and remove stacks. Max out existing stacks in one pouch or all pouches, or fill supported pouches up to their slot and stack limits. Copy compatible stacks through the Item Bank and save named bag presets to restore quantities in the same game.
- **Editing modes.** Standard offers the usual game choices. HaX expands choices for abilities, forms, moves, held items, memories, records and generation. Hardcore restricts editing and copying while allowing moves, releases, snapshots and exports. A confirmed Hardcore Bank transfer moves the original and exports the edited save. Mode preferences persist on the SD card.

### Boxes, Bank and Living Dex

- **Box tools.** Select one or more Pokémon or mark all occupied slots. Copy them to the Bank, move them to the next unlocked box, duplicate them or review a release. Sort a box by species, rename it, change its wallpaper or audit it for compatibility. Bulk moves preserve battle-team assignments in BDSP, Sword/Shield and Scarlet/Violet.
- **Local Pokémon Bank.** Store individual Pokémon, including BDSP’s PB8 records. Search by species, nickname or filename. Choose an empty destination slot and review the supported format conversion before placing the Pokémon.
- **Living Dex and Pokédex.** Track National and shiny Living Dex coverage across the save and Bank, filter missing species and plan box moves. Review a 30-species plan from one donor save and export both edited saves together. Register species in the party and boxes as caught in supported in-game Pokédexes.
- **Pokémon Showdown.** Export the selected boxed Pokémon or the whole party as text or an on-screen QR code. Import up to six sets from `/switch/PKForge/imports/showdown.txt` and review their destination slots. The importer recognizes supported alternate forms and Scarlet/Violet Tera types.

### Encounters, Mystery Gifts and breeding

- **Legality and RNG.** Run an offline PKHeX encounter report for one Pokémon or sweep the party and every box. Preview suggested fixes, restore HP, status and PP, or run Auto Legality Mod repairs. Search encounter cards by species and form and place a matching encounter in an empty slot. Generate regular or shiny Pokémon by species, or generate from a Showdown set. Inspect and edit the personality ID (PID) and encryption constant, and reroll nature while keeping shiny status where supported. Event encounter matching uses distribution tables supplied on the SD card.
- **Mystery Gift album.** Browse locally installed Gen IV–IX Wonder Card catalogs. Search by title, Pokémon, original trainer, move or item. Switch generation archives, sort cards and group them by year, game, series or species. Filter by save compatibility, gift type, shininess, session status, language or available copies. Inspect the distribution date, trainer, moves, item and ball, then choose a language copy. Queue eligible Gen IV–VII cards or stage supported Pokémon and item gifts in Let’s Go, Sword/Shield, Scarlet/Violet and Legends: Z-A.
- **Eggs, Day Care and Nursery.** Choose compatible parents, create an egg by species or from a Pokémon’s evolutionary line, and generate shiny or perfect-IV eggs. Edit egg moves and hatch cycles, then hatch using the game’s rules. Inspect stored parents and egg readiness, deposit or withdraw parents, add Day Care experience and edit Nursery RNG seeds where supported. Both Sword/Shield Nurseries are accessible.

### Search, portraits and side activities

- **Controller search.** Press **ZL + ZR** for the keyboard in supported lists. Search all boxes, save files, either Bank, bag pouches, addable items across pouches, bag presets, Mystery Gifts, the Living Dex and editor choices such as moves, forms, balls and ribbons. Search also works in snapshots, donor saves, Hall of Fame teams, box audits, the park journal and advanced tool results.
- **Portraits and animation.** The standard release displays Pokédex numbers and game names. Supply local game logos, portrait atlases, animated WebP portraits or textured, rigged models with idle animations. Matching files can distinguish form, sex, shiny appearance and Alcremie decoration. Click the right stick to toggle motion. The media guide covers portrait names, Blender model conversion, importing individual Wonder Cards into a catalog and installing event tables. [Local media setup](switch2/README.md#local-media)
- **Poképark and Pokéwalker.** Keep up to 30 resident copies, switch between them, feed and play with them, and review their activity journals. Walk a resident along a controller-driven route and save its steps.
- **GBA Hall of Fame and clock.** Browse recorded Hall of Fame teams in full GBA saves. In Ruby, Sapphire and Emerald, adjust or reset the initial and elapsed clock values and apply the berry-glitch day adjustment.

## Build the NRO

With Docker running on Linux, run the following from a checkout.

```sh
sh switch2/build-container.sh -j2
python3 switch2/tests/verify_nro.py switch2/PKForge.nro
```

The build uses the pinned ouncebrew SDK and the runtime and resources checked into the port. The output is `switch2/PKForge.nro`. For native devkitA64 builds, host tests and optional portrait packs, see the [port guide](switch2/README.md).

The native application, tools and RomFS are in `switch2/`. The shared save and legality engine is in `src/`, its pinned dependencies in `external/`, and source fonts in `assets/`. Initialize the submodules to rebuild or test the managed engine.

PKForge is distributed under the [GPL](LICENSE). Bundled assets have their own [credits and licenses](switch2/romfs/ui/ATTRIBUTION.md). See [source provenance](PROVENANCE.md) and the [binary notices](switch2/romfs/THIRD-PARTY-NOTICES.txt).
