# Switch 2 interface

The landscape interface adapts PKForge Android's default blue theme, game shelf,
storage wells, sprite inspector, and shared hint bar to the Switch 2's single display.
The controller and handheld touchscreen share the same actions. The current selection has a cyan outline with
corner brackets and a lighter fill. The file shelf also raises the focused card.

The RGB565 palette lives in `include/ui.hpp`. The canonical colors derive from
the upstream PKForge Chrome theme: Deep `#171B32`, Deck `#1B2447`, Well `#041F46`,
Cyan `#42BFE8`, Ink `#F4F8FF`, and Soft `#A8BADC`. The backdrop's muted cobalt grid
keeps the screen in the same visual family as the Android menus. M PLUS Rounded 1c
is used for headings, controls, and data labels. Interface asset provenance is documented
in `romfs/ui/ATTRIBUTION.md`.

Screens share a title strip, save context, status line, and controller hint bar.
Boxes display six columns beside a live inspector. The Bank shows 12 entries per page
with the selected record in the same inspector. The game shelf shows four saves per
page. Party and Pokémon details share the storage palette and focus treatment.

Screen transitions slide and fade over 180 ms. Focus changes and edits blend over
110 ms. Clicking the right stick toggles transitions and portrait playback for the
session. The renderer composes new scenes on input and updates the portrait region
while a clip is active. Screens without an active clip stay static.

English is the interface language. The general-purpose type atlas displays ASCII. Unsupported
characters are replaced for display without changing the stored name.

Save tools use the same split layout as Boxes. The left pane holds a bounded
list and the right pane holds details, quantities, compatibility or destination
information. Living Dex plans and format changes are reviewed on a dedicated
screen. Park routes use the same chrome with a green map and resident readout.

The Pokémon summary, editor, and Bank inspector share the same portrait renderer.
The portrait frame contains an installed 3D model, a matching SD WebP, a local species-specific idle
loop, or a still matching the saved look. Alcremie's decoration uses the saved
sweet. With no installed art, thumbnails and portraits show species numbers. The atlas reads one compressed
loop at a time and keeps its decoded frames while that look is in focus.

The game shelf uses the recognized save's game logo with the generic cartridge
as the fallback. The Trainer and Bag workflows use the same draft and review
pattern as the Pokémon editor.

Searchable views show a controller badge in the status band. The same band
shows the active filter. Search uses the system keyboard and the existing list
layout, preserving a consistent position for focus, details, and empty states.

Mystery Gift follows the upstream plum Wonder Card album. The six-row list shares the screen with the live card, with search and filter chips above and controller hints below. The album uses the DotGothic16 face with Noto Sans CJK fallback for catalog characters. The selected row carries a blue highlight and a border. Language tags and staged indicators remain text labels.


Touch targets follow the drawn cards, rows and action labels. A tap selects a save or
Pokémon. A second tap on that selection opens it. Vertical swipes move through lists,
and horizontal swipes change pages on the game shelf, in Boxes, and in the Bank.
The Controls panel exposes the controller actions on every interactive screen. It uses
the existing blue panels and sits above the current content. Confirmation screens
only accept their own actions. Leaving an older target, using multiple fingers, or
interleaving controller input cancels an in-progress tap.
