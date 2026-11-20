// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "workspace.hpp"

#include <array>
#include <span>

namespace forge
{
inline constexpr auto Snapshots = "sdmc:/switch/PKForge/snapshots";
inline constexpr auto BagPresets = "sdmc:/switch/PKForge/bag-presets";
inline constexpr auto Imports = "sdmc:/switch/PKForge/imports";

std::unique_ptr<pksm::Sav> stageSave(pksm::Sav& active);
File snapshot(const Save& save, std::string_view label);
struct SaveDifference { int party; int boxes; int money; bool trainer; };
SaveDifference compareSaves(const Save& current, const Save& other);
std::string restoreSnapshot(Save& current, const File& selected);
std::string exportShowdown(std::string_view text);
void depositBatch(std::span<const pksm::PKX* const> entries);
std::string readShowdown();
std::string publishSavePair(const Save& current, const Save& donor);
struct CrossDexMove {
    int box, slot, targetSlot;
    unsigned species;
    std::vector<u8> original;
};
struct CrossDexPlan {
    File donor;
    int targetBox = -1;
    unsigned start = 0;
    std::vector<CrossDexMove> moves;
};
CrossDexPlan planCrossDex(Save& current, const File& donorFile, unsigned start, bool shiny);
std::string applyCrossDex(Save& current, const CrossDexPlan& plan);

bool boxSlotAssignedToTeam(const pksm::Sav& save, int box, int slot);

enum class BoxAction { Sort, Move, Duplicate, Release, CopyToBank };
unsigned organizeBox(Save& save, int box, std::span<const int> selected, BoxAction action);
struct BoxIssue { int slot; std::string message; };
std::vector<BoxIssue> auditBox(const Save& save, int box);
unsigned registerOwned(Save& save);
using ClockFields = std::array<unsigned, 8>;
ClockFields gbaClock(const Save& save);
void applyGbaClock(Save& save, const ClockFields& fields);

struct HallMember {
    pksm::Species species;
    std::string nickname;
    unsigned level;
    bool shiny;
};
struct HallTeam { int record; std::vector<HallMember> members; };
std::vector<HallTeam> hallOfFame(const Save& save);

struct Nursery {
    std::array<std::unique_ptr<pksm::PKX>, 2> parents;
    bool eggReady;
};
Nursery nursery(const Save& save);
void withdrawNursery(Save& save, int parent, int box, int slot);

void removeBagStack(Save& save, pksm::Sav::Pouch pouch, int slot);

struct BagPresetEntry { pksm::Sav::Pouch pouch; unsigned item, count; };
File storeBagPreset(const Save& save, std::string_view label);
std::vector<BagPresetEntry> readBagPreset(const File& file, const pksm::Sav& save);
void applyBagPreset(Save& save, std::span<const BagPresetEntry> entries);

struct ShowdownEntry
{
    int box, slot;
    std::unique_ptr<pksm::PKX> pokemon;
};
std::vector<ShowdownEntry> previewShowdownTeam(Save& save, std::string_view text, int box, int slot);
void applyShowdownTeam(Save& save, std::span<ShowdownEntry> team);

std::string showdownSet(const pksm::PKX& pokemon);
std::string showdownParty(const pksm::Sav& save);
std::unique_ptr<pksm::PKX> previewShowdown(Save& save, std::string_view text);
}
