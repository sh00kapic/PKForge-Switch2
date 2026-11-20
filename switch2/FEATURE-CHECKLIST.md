# PKForge Switch 2 feature checklist

This checklist compares the libounce NRO with PKForge on Android. Each checked
line describes the scope available in the NRO.

## Saves and editing

- [x] Open supported SD-card saves, browse party and boxes, and export an independently checksummed copy.
- [x] Edit party and boxed Pokémon. Fields include nickname, original trainer name and IDs, level, friendship, nature, ability slot, held item, moves, IVs, EVs, shininess, Pokérus, form, ball, language, origin game, fateful encounter, and met details where the format supports them.
- [x] Edit Scarlet/Violet Tera overrides and supported Hyper Training, ribbons, and marks.
- [x] Toggle shininess across all editable record formats, including Legends: Arceus and the Game Boy DV formats.
- [x] Use ZL + ZR to filter long lists with the system keyboard, including bag additions across pouches, Mystery Gifts, boxed Pokémon, and editor value lists.
- [x] Display species numbers by default. Load portraits, animations, logos and form-specific art from local SD media.
- [x] Render textured and skinned models with animated joints when the matching model is installed on the SD card.
- [x] Edit play time, trainer name, raw trainer and secret IDs, gender, and money where the format stores them.
- [x] Edit valid original Game Boy and Game Boy Color saves with DVs and Stat Exp, Let's Go saves with Awakening Values, and supported Legends: Z-A saves.
- [x] Open and export 64 KiB Game Boy Advance saves at their original size. Use the filename to label older editions that share a save format.
- [x] Persist editing preferences. HaX expands ability, form, memory, record and generation choices. Hardcore permits moves, releases, backups and exports and blocks edits and copies.
- [ ] Broader coverage of articulated Pokémon meshes. Most moving portraits use rendered sprite frames.
- [x] Save, replace, delete and apply named trainer profiles. Set generation ownership, edit supported card fields, game records and currencies, and edit Pokémon handler, memory, forms, held items, learnsets, PP and relearn slots, technical records, markings, contest stats and supported size or training fields. Legends: Arceus exposes Move Shop purchase and mastery flags.
- [x] Analyze an encounter or sweep the party and all boxes with PKHeX. Preview legality presets, run AutoMod repairs or generation, place encounter cards, inspect PID and EC, and reroll nature while retaining shiny status.
- [x] Select compatible parents, generate eggs, edit egg moves and hatch cycles, and hatch using the applicable game rules. Inspect save-backed Day Cares, move parents and edit exposed Nursery RNG seeds, including both Sword/Shield facilities.

## Bank, transfers, and collection

- [x] Copy Pokémon to the local Bank, search by species, nickname, or filename, and paste compatible entries into an empty slot after a conversion preview.
- [x] Select multiple boxed Pokémon for batch Bank copy, movement to the next unlocked box, duplication, or release. Sort by species, rename boxes, change base wallpapers, and run a basic compatibility audit.
- [x] Preserve battle-team assignments in bulk moves for Brilliant Diamond/Shining Pearl, Sword/Shield, and Scarlet/Violet.
- [x] Browse bag pouches, add compatible items, adjust, refill or remove stacks, and copy compatible stacks through the Item Bank.
- [x] Track a National and shiny Living Dex across the open save and the Bank. Filter missing species and preview a same-save move plan.
- [x] Review a 30-species Living Dex move plan from one donor save and export both edited copies together.
- [x] Register boxed and party species as caught in supported native Pokédex formats.
- [x] Export single sets and teams as Showdown text or on-screen QR codes, and review and import up to six sets from the SD card, including supported alternate forms and Scarlet/Violet Tera overrides.
- [ ] Full PKHeX transfer coverage, PKForge Android Bank interoperability, themed boxes, manual box locks, and transfer legality.
- [x] Save named bag presets and restore their listed quantities to the same game.
- [ ] Broader item conversion, multi-donor Living Dex planning, and trade evolutions.
- [ ] QR scanning, full metagame syntax, and coverage of every alternate form.

## Events and game-specific tools

- [x] Browse Gen IV–IX Wonder Card catalogs supplied on the SD card.
- [x] Browse illustrated Wonder Cards grouped by year, game, series or species. Inspect distribution metadata and language copies, search titles, OTs or moves, and filter supported gifts before queuing.
- [x] Queue matching Gen IV–VII cards. Stage supported Let's Go, Sword/Shield, Scarlet/Violet, and Legends: Z-A Pokémon and item gifts.
- [x] Inspect Brilliant Diamond and Shining Pearl Nursery parents and egg readiness. Withdraw a parent to an empty box slot when no egg is waiting.
- [x] Browse recorded Game Boy Advance Hall of Fame teams.
- [ ] All other Wonder Card variants, gift types and regional restrictions.
- [ ] Nuzlocke reports, Grand Underground, Poké Beans, fashion, and other game-specific editors.
- [x] Adjust or reset Ruby, Sapphire, and Emerald save clock values, including the elapsed-day adjustment for the berry glitch.
- [ ] Roaming encounters, event flags, encounter re-arming, and RTC tools for other games.

## Park, walker, and platform

- [x] Copy up to 30 Pokémon to Poképark. Switch residents, feed and play with them, and browse their saved activity journal.
- [x] Walk a resident along a controller-driven Pokéwalker route and save steps.
- [x] Name save snapshots, compare changes to party, boxes, trainer and money, and restore a separate copy.
- [x] Display game logos and edit Brilliant Diamond/Shining Pearl 1.0–1.3 SD save copies, with PB8 Bank records and an independently verified save checksum.
- [ ] Poképark personalities, relationships, weather, dynamic events, photography, and albums.
- [ ] Pokéwalker Radar, Dowsing, catches, course unlocks, step-counter integration, and HeartGold/SoulSilver sync.
- [ ] Broader Hall of Fame support and emulator save conversions.
- [x] Browse checksummed GameCube GCI saves from Colosseum, XD, and Pokémon Box. Copy Pokémon Box records to the Bank.
- [ ] Editing and export of GameCube saves, conversion of Colosseum/XD Pokémon entities, and upstream ROM-hack-specific editing.
- [ ] Android home-screen widgets, emulator directory integration, and dual-screen companion views.

## Verification

- [x] Cross-build the Nintendo Switch 2 NRO with libounce and verify the native header, launcher data, and embedded resources.
- [x] Run host round-trip tests for BDSP, Violet, party changes, inventory, Wonder Cards, Nursery withdrawals, Bank copies, Living Dex transfers, snapshots, Showdown, and park persistence.
- [x] Exercise the native interface with controller inputs against synthetic saves.
- [x] Exercise the desktop save harness against 59 public samples and independently verify all 48 exported saves.
- [x] Round-trip Pokémon and item Wonder Cards on played Let's Go Eevee, Legends: Z-A, and Violet saves.
- [x] Apply shiny party edits through the controller interface on played saves across 13 formats and verify exported copies.
- [x] Verify portrait index coverage, play local loops, render an imported articulated test model, and export a changed Alcremie sweet to a Violet save copy.
- [x] Exercise the offline engine in the native host, reopen exported saves, and drive the trainer, sweep, encounter, RNG and Nursery screens with controller input.
- [ ] Test on physical Switch 2 hardware and on user saves.
