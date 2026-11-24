// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "workspace.hpp"

#include <array>
#include <ctime>
#include <span>

namespace forge
{
struct Field
{
    enum class Kind {
        Level, Friendship, Nature, Ability, HeldItem, Shiny, Virus, MetLevel, MetLocation,
        Tera, Move, IV, EV, HyperTraining, SecondaryStat, Form, Decoration, Ball, Origin, Language, Fateful, TrainerId, SecretId, MetDate, EggLocation
    };
    Kind kind;
    const char* label;
    int index = 0;
};

std::vector<Field> pokemonFields(const pksm::PKX& pokemon);
std::string fieldValue(const pksm::PKX& pokemon, Field field);
void adjustField(const pksm::Sav& save, pksm::PKX& pokemon, Field field, int step, bool hax = false);
std::vector<unsigned> fieldOptions(const pksm::Sav& save, const pksm::PKX& pokemon, Field field);
std::string fieldOptionName(const pksm::PKX& pokemon, Field field, unsigned value);
void setFieldOption(pksm::PKX& pokemon, Field field, unsigned value);
std::vector<std::string> compatibility(const pksm::Sav& save, const pksm::PKX& pokemon);
std::unique_ptr<pksm::PKX> previewTransfer(Save& save, int box, int slot, const pksm::PKX& pokemon);
bool sameFormat(const pksm::PKX& a, const pksm::PKX& b);
void editPartyMember(Save& save, int slot, pksm::PKX& draft, bool recalculateStats);

std::string_view pouchName(pksm::Sav::Pouch pouch);
struct BagSlot
{
    unsigned id;
    unsigned count;
    unsigned limit;
};
BagSlot bagSlot(const pksm::Sav& save, pksm::Sav::Pouch pouch, int slot);
void changeBagCount(Save& save, pksm::Sav::Pouch pouch, int slot, unsigned count);
std::vector<unsigned> bagCatalog(const pksm::Sav& save, pksm::Sav::Pouch pouch);
void addBagItem(Save& save, pksm::Sav::Pouch pouch, unsigned id, unsigned count);
struct BagFill { unsigned stacks; unsigned skipped; };
BagFill maxBag(Save& save, std::optional<pksm::Sav::Pouch> pouch, bool includeMissing);
std::array<unsigned, 1026> livingDex(const pksm::Sav* save, const std::vector<File>& bank,
    bool shiny, unsigned* skipped = nullptr);

struct Gift
{
    int generation;
    std::string name;
    std::string extension;
    std::vector<u8> data;
};
std::vector<Gift> readGifts(const std::string& path, int generation);
bool giftFitsSave(const Save& save, Gift& gift);
const char* giftQueueBlocker(const Save& save, Gift& gift);
bool canQueueGift(const Save& save, Gift& gift);
void injectGift(Save& save, Gift& gift);

struct DexMove { int fromBox; int fromSlot; int toSlot; unsigned species; };
struct DexPlan { int box = -1; unsigned start = 0; std::vector<DexMove> moves; };
DexPlan planDexBox(const Save& save, unsigned start, bool shiny);
void applyDexBox(Save& save, const DexPlan& plan);
}

namespace forge
{
inline constexpr auto ItemBank = "sdmc:/switch/PKForge/items";
inline constexpr auto Park = "sdmc:/switch/PKForge/park";
void prepareExtras();
struct StoredItem { unsigned id; unsigned count; };
void storeItem(StoredItem item);
std::vector<StoredItem> loadItems();
void copyItemToSave(Save& save, StoredItem entry);

struct Resident
{
    File pokemon;
    unsigned feeds = 0;
    unsigned plays = 0;
    unsigned steps = 0;
};
void moveCopyToPark(const pksm::PKX& pokemon);
std::vector<Resident> parkResidents();
struct ParkEvent
{
    std::time_t when;
    unsigned feeds, plays, steps;
};
std::vector<ParkEvent> parkJournal(const Resident& resident);
void logParkActivity(const Resident& resident, unsigned feeds, unsigned plays, unsigned steps);
}
