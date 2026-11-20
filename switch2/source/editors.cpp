// SPDX-License-Identifier: GPL-3.0-or-later
#include "editors.hpp"
#include "library.hpp"

#include "pkx/PA8.hpp"
#include "pkx/PA9.hpp"
#include "pkx/PK2.hpp"
#include "pkx/PK9.hpp"
#include "pkx/PK3.hpp"
#include "utils/i18n.hpp"
#include "wcx/PCD.hpp"
#include "wcx/PGF.hpp"
#include "wcx/PGT.hpp"
#include "wcx/WC6.hpp"
#include "wcx/WC7.hpp"
#include "wcx/WC8.hpp"
#include "wcx/WC9.hpp"
#include "wcx/WB7.hpp"
#include "wcx/WA9.hpp"
#include "sav/Sav4.hpp"
#include "sav/Sav5.hpp"
#include "sav/SavSWSH.hpp"
#include "sav/SavSV.hpp"
#include "sav/SavLGPE.hpp"
#include "sav/SavZA.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <set>
#include <typeinfo>

namespace forge
{
namespace
{
using pksm::Generation;
using pksm::Language;
using pksm::PKX;
using pksm::Sav;
using Kind = Field::Kind;
constexpr auto ENG = Language::ENG;
constexpr std::array sweets{"Strawberry Sweet", "Berry Sweet", "Love Sweet", "Star Sweet",
    "Clover Sweet", "Flower Sweet", "Ribbon Sweet"};
constexpr std::array stats{pksm::Stat::HP, pksm::Stat::ATK, pksm::Stat::DEF,
    pksm::Stat::SPATK, pksm::Stat::SPDEF, pksm::Stat::SPD};

template<typename T>
int increment(T value, int delta, int min, int max)
{
    return std::clamp(static_cast<int>(value) + delta, min, max);
}

template<class Set, class Value>
auto stepIn(const Set& values, Value current, int delta)
{
    auto iter = values.lower_bound(current);
    if (iter == values.end())
        iter = std::prev(iter);
    while (delta > 0 && std::next(iter) != values.end())
    {
        ++iter;
        --delta;
    }
    while (delta < 0 && iter != values.begin())
    {
        --iter;
        ++delta;
    }
    return *iter;
}

void requireWritable(const Save& save)
{
    if (!save.data || !save.writable)
        throw std::runtime_error("Open an editable save first.");
}

constexpr std::array languages{Language::JPN, Language::ENG, Language::FRE, Language::ITA,
    Language::GER, Language::SPA, Language::KOR, Language::CHS, Language::CHT};
constexpr std::array languageNames{"Japanese", "English", "French", "Italian", "German",
    "Spanish", "Korean", "Simplified Chinese", "Traditional Chinese"};

std::vector<pksm::GameVersion> originGames(Generation gen)
{
    using V = pksm::GameVersion;
    std::vector<pksm::GameVersion> versions;
    const auto add = [&](std::initializer_list<pksm::GameVersion> games)
    { versions.insert(versions.end(), games); };
    add({V::S, V::R, V::E, V::FR, V::LG, V::CXD});
    if (gen >= Generation::FOUR) add({V::D, V::P, V::Pt, V::HG, V::SS});
    if (gen >= Generation::FIVE) add({V::B, V::W, V::B2, V::W2});
    if (gen >= Generation::SIX) add({V::X, V::Y, V::OR, V::AS});
    if (gen >= Generation::SEVEN)
        add({V::SN, V::MN, V::US, V::UM, V::RD, V::GN, V::BU, V::YW, V::GD, V::SV, V::C});
    if (gen >= Generation::EIGHT) add({V::GO, V::GP, V::GE, V::SW, V::SH, V::PLA, V::BD, V::SP});
    if (gen >= Generation::NINE) add({V::SL, V::VL});
    return versions;
}

template<class Values, class Value>
auto stepList(const Values& values, Value current, int delta)
{
    const auto it = std::ranges::find(values, current);
    if (it == values.end())
        return values.front();
    return values[std::clamp(static_cast<int>(it - values.begin()) + delta, 0,
        static_cast<int>(values.size()) - 1)];
}

std::string itemName(unsigned id)
{
    if (!id)
        return "None";
    const auto& name = i18n::item(ENG, id);
    return name.empty() ? "#" + std::to_string(id) : name;
}
}

std::vector<Field> pokemonFields(const PKX& pk)
{
    const auto gen = pk.generation();
    const bool gameBoy = gen <= Generation::TWO;
    const bool letsGo = gen == Generation::LGPE;
    const bool arceus = dynamic_cast<const pksm::PA8*>(&pk);
    const bool za = dynamic_cast<const pksm::PA9*>(&pk);
    std::vector<Field> fields{{Kind::Level, "Level"}};
    if (gen != Generation::ONE)
        fields.push_back({Kind::Friendship, "Friendship"});
    if (!gameBoy)
    {
        fields.push_back({Kind::Nature, "Nature"});
        if (!letsGo && !arceus && !za)
            fields.push_back({Kind::Ability, "Ability slot"});
    }
    fields.push_back({Kind::Shiny, gameBoy && gen == Generation::ONE ? "Transfer shiny" : "Shiny"});
    if (!gameBoy)
    {
        fields.push_back({Kind::MetLevel, "Met level"});
        fields.push_back({Kind::MetLocation, "Met location ID"});
    }
    if (gen != Generation::ONE && !letsGo && !arceus && !za)
        fields.push_back({Kind::HeldItem, "Held item"});
    if (gen != Generation::ONE && !letsGo && !arceus && !za)
        fields.push_back({Kind::Virus, "Pokerus"});
    if (dynamic_cast<const pksm::PK9*>(&pk))
        fields.push_back({Kind::Tera, "Tera type override"});
    for (int i = 0; i < 4; ++i)
        fields.push_back({Kind::Move, std::array{"Move 1", "Move 2", "Move 3", "Move 4"}[i], i});
    constexpr std::array ivNames{"HP IV", "Attack IV", "Defense IV", "Sp. Attack IV",
        "Sp. Defense IV", "Speed IV"};
    constexpr std::array evNames{"HP EV", "Attack EV", "Defense EV", "Sp. Attack EV",
        "Sp. Defense EV", "Speed EV"};
    constexpr std::array htNames{"Hyper train HP", "Hyper train Attack", "Hyper train Defense",
        "Hyper train Sp. Attack", "Hyper train Sp. Defense", "Hyper train Speed"};
    constexpr std::array dvs{"", "Attack DV", "Defense DV", "Special DV", "", "Speed DV"};
    constexpr std::array statExp{"HP Stat Exp", "Attack Stat Exp", "Defense Stat Exp", "Special Stat Exp", "", "Speed Stat Exp"};
    constexpr std::array awakened{"HP Awakening", "Attack Awakening", "Defense Awakening", "Sp. Attack Awakening",
        "Sp. Defense Awakening", "Speed Awakening"};
    constexpr std::array grit{"HP Effort Level", "Attack Effort Level", "Defense Effort Level",
        "Sp. Attack Effort Level", "Sp. Defense Effort Level", "Speed Effort Level"};
    for (int i = 0; i < 6; ++i)
        if (!gameBoy || (i != 0 && i != 4))
            fields.push_back({Kind::IV, gameBoy ? dvs[i] : ivNames[i], i});
    for (int i = 0; i < 6; ++i)
        if (!gameBoy || i != 4)
        {
            if (letsGo || arceus)
                fields.push_back({Kind::SecondaryStat, letsGo ? awakened[i] : grit[i], i});
            else
                fields.push_back({Kind::EV, gameBoy ? statExp[i] : evNames[i], i});
        }
    if (pk.generation() >= Generation::SEVEN && !dynamic_cast<const pksm::PA8*>(&pk))
        for (int i = 0; i < 6; ++i)
            fields.push_back({Kind::HyperTraining, htNames[i], i});
    if (pk.generation() >= Generation::FOUR)
    {
        fields.push_back({Kind::Form, "Form"});
        if (pk.species() == pksm::Species::Alcremie && gen >= Generation::EIGHT && !arceus)
            fields.push_back({Kind::Decoration, "Sweet decoration"});
        fields.push_back({Kind::MetDate, "Met year", 0});
        fields.push_back({Kind::MetDate, "Met month", 1});
        fields.push_back({Kind::MetDate, "Met day", 2});
        fields.push_back({Kind::EggLocation, "Egg location ID"});
    }
    if (pk.generation() >= Generation::THREE)
    {
        fields.push_back({Kind::Ball, "Poke Ball"});
        fields.push_back({Kind::Origin, "Origin game"});
        fields.push_back({Kind::Language, "Language"});
        fields.push_back({Kind::Fateful, "Fateful encounter"});
        fields.push_back({Kind::TrainerId, "OT trainer ID"});
        fields.push_back({Kind::SecretId, "OT secret ID"});
    }
    else
        fields.push_back({Kind::TrainerId, "OT trainer ID"});
    return fields;
}

std::string fieldValue(const PKX& pk, Field field)
{
    switch (field.kind)
    {
        case Kind::Level: return std::to_string(pk.level());
        case Kind::Friendship: return std::to_string(pk.currentFriendship());
        case Kind::Nature: return pk.nature().localize(ENG);
        case Kind::Ability: return pk.ability().localize(ENG);
        case Kind::HeldItem: return itemName(pk.heldItem());
        case Kind::Shiny: return pk.shiny() ? "Yes" : "No";
        case Kind::Virus: return pk.pkrs() ? "Strain " + std::to_string(pk.pkrsStrain()) +
            " / " + std::to_string(pk.pkrsDays()) + " days" : "None";
        case Kind::MetLevel: return std::to_string(pk.metLevel());
        case Kind::MetLocation: return std::to_string(pk.metLocation());
        case Kind::Tera:
        {
            const auto type = static_cast<const pksm::PK9&>(pk).teraTypeOverride();
            return type < 19 ? i18n::type(ENG, pksm::Type(type)) : "Original type";
        }
        case Kind::Move:
            return pk.move(field.index) == pksm::Move::None ? "None" : pk.move(field.index).localize(ENG);
        case Kind::IV: return std::to_string(pk.iv(stats[field.index]));
        case Kind::EV: return std::to_string(pk.ev(stats[field.index]));
        case Kind::SecondaryStat: return std::to_string(pk.secondaryStatCalc(stats[field.index]));
        case Kind::HyperTraining: return pk.hyperTrain(stats[field.index]) ? "Yes" : "No";
        case Kind::Form:
        {
            const auto& name = i18n::form(ENG, pk.version(), pk.species(), pk.alternativeForm());
            return name.empty() ? "Form " + std::to_string(pk.alternativeForm()) : name;
        }
        case Kind::Decoration: return sweets[std::min<size_t>(sweets.size() - 1, pk.formArgument())];
        case Kind::Ball: return i18n::ball(ENG, pk.ball());
        case Kind::Origin: return i18n::game(ENG, pk.version());
        case Kind::Language:
        {
            const auto it = std::ranges::find(languages, pk.language());
            return it == languages.end() ? "Unknown" : languageNames[it - languages.begin()];
        }
        case Kind::Fateful: return pk.fatefulEncounter() ? "Yes" : "No";
        case Kind::TrainerId: return std::to_string(pk.TID());
        case Kind::SecretId: return std::to_string(pk.SID());
        case Kind::MetDate:
        {
            const auto d = pk.metDate();
            return std::to_string(field.index == 0 ? d.year() : field.index == 1 ? d.month() : d.day());
        }
        case Kind::EggLocation: return std::to_string(pk.eggLocation());
    }
    return {};
}

std::vector<unsigned> fieldOptions(const Sav& save, const PKX& pk, Field field)
{
    std::vector<unsigned> options;
    const auto add = [&](const auto& list)
    {
        for (const auto& value : list)
            options.push_back(static_cast<unsigned>(value));
    };
    switch (field.kind)
    {
        case Kind::Move: add(save.availableMoves()); break;
        case Kind::HeldItem: add(save.availableItems()); break;
        case Kind::Ball: add(save.availableBalls()); break;
        case Kind::Form:
            for (unsigned id = 0; id < save.formCount(pk.species()); ++id)
                options.push_back(id);
            break;
        case Kind::Origin:
            add(originGames(pk.generation()));
            if (dynamic_cast<const pksm::PA9*>(&pk))
                options.push_back(static_cast<unsigned>(pksm::GameVersion::ZA));
            if (pk.generation() == Generation::LGPE)
                add(std::array<pksm::GameVersion, 3>{pksm::GameVersion::GO,
                    pksm::GameVersion::GP, pksm::GameVersion::GE});
            break;
        default: break;
    }
    return options;
}

std::string fieldOptionName(const PKX& pk, Field field, unsigned value)
{
    switch (field.kind)
    {
        case Kind::Move: return value ? pksm::Move(value).localize(ENG) : "None";
        case Kind::HeldItem:
            if (!value) return "None";
            if (pk.generation() == Generation::TWO) return i18n::item2(ENG, value);
            if (pk.generation() == Generation::THREE) return i18n::item3(ENG, value);
            return itemName(value);
        case Kind::Ball: return i18n::ball(ENG, pksm::Ball(value));
        case Kind::Origin: return i18n::game(ENG, pksm::GameVersion(value));
        case Kind::Form:
        {
            const auto& name = i18n::form(ENG, pk.version(), pk.species(), value);
            return name.empty() ? "Form " + std::to_string(value) : name;
        }
        default: return {};
    }
}

void setFieldOption(PKX& pk, Field field, unsigned value)
{
    switch (field.kind)
    {
        case Kind::Move:
            pk.move(field.index, pksm::Move(value));
            pk.PPUp(field.index, 0);
            pk.PP(field.index, pk.maxPP(field.index));
            break;
        case Kind::HeldItem:
            if (auto* gbc = dynamic_cast<pksm::PK2*>(&pk))
                gbc->heldItem2(value);
            else if (auto* gba = dynamic_cast<pksm::PK3*>(&pk))
                gba->heldItem3(value);
            else
                pk.heldItem(value);
            break;
        case Kind::Ball: pk.ball(pksm::Ball(value)); break;
        case Kind::Form:
        {
            if (value != pk.alternativeForm())
            {
                pk.alternativeForm(value);
                const int ability = pk.abilityNumber() == 4 ? 2 : pk.abilityNumber() == 2 ? 1 : 0;
                pk.setAbility(ability);
            }
            break;
        }
        case Kind::Origin: pk.version(pksm::GameVersion(value)); break;
        default: break;
    }
}

void adjustField(const Sav& save, PKX& pk, Field field, int delta, bool hax)
{
    const int direction = delta < 0 ? -1 : 1;
    switch (field.kind)
    {
        case Kind::Level: pk.level(increment(pk.level(), delta, 1, 100)); break;
        case Kind::Friendship: pk.currentFriendship(increment(pk.currentFriendship(), delta, 0, 255)); break;
        case Kind::Nature:
            if (pk.version() == pksm::GameVersion::INVALID || pk.version() == pksm::GameVersion::None)
                throw std::runtime_error("A valid origin game is required to change nature.");
            pk.nature(pksm::Nature((static_cast<int>(pk.nature()) + direction + 25) % 25));
            break;
        case Kind::Ability:
        {
            constexpr std::array<u8, 3> slots{1, 2, 4};
            const int count = pk.generation() >= Generation::FIVE ? 3 : 2;
            const auto current = std::find(slots.begin(), slots.end(), pk.abilityNumber());
            const int offset = current == slots.end() ? 0 : current - slots.begin();
            pk.setAbility((offset + direction + count) % count);
            break;
        }
        case Kind::HeldItem:
        {
            const auto& items = save.availableItems();
            if (items.empty())
                throw std::runtime_error("This save has no held item table.");
            if (pk.generation() == Generation::TWO)
            {
                auto& gbc = static_cast<pksm::PK2&>(pk);
                setFieldOption(pk, field, stepIn(items, static_cast<int>(gbc.heldItem2()), delta));
            }
            else if (pk.generation() == Generation::THREE)
            {
                auto& gba = dynamic_cast<pksm::PK3&>(pk);
                setFieldOption(pk, field, stepIn(items, static_cast<int>(gba.heldItem3()), delta));
            }
            else
                setFieldOption(pk, field, stepIn(items, static_cast<int>(pk.heldItem()), delta));
            break;
        }
        case Kind::Shiny:
        {
            const bool desired = !pk.shiny();
            pk.shiny(desired);
            if (pk.shiny() != desired)
                throw std::runtime_error("This combination of form and trainer IDs cannot change shininess.");
            break;
        }
        case Kind::Virus:
            if (pk.pkrs())
                pk.pkrs(0);
            else
            {
                pk.pkrsStrain(1);
                pk.pkrsDays(2);
            }
            break;
        case Kind::MetLevel: pk.metLevel(increment(pk.metLevel(), delta, 0, pk.level())); break;
        case Kind::MetLocation:
        {
            const int cap = pk.generation() == Generation::THREE ? 255 : 65535;
            pk.metLocation(increment(pk.metLocation(), delta, 0, cap));
            break;
        }
        case Kind::Tera:
        {
            auto& tera = static_cast<pksm::PK9&>(pk);
            tera.teraTypeOverride((std::min<int>(tera.teraTypeOverride(), 19) + direction + 20) % 20);
            break;
        }
        case Kind::Move:
        {
            const auto& moves = save.availableMoves();
            if (moves.empty())
                throw std::runtime_error("This save has no move table.");
            setFieldOption(pk, field, static_cast<unsigned>(stepIn(moves, pk.move(field.index), delta)));
            break;
        }
        case Kind::IV:
            pk.iv(stats[field.index], increment(pk.iv(stats[field.index]), delta, 0,
                pk.generation() <= Generation::TWO ? 15 : 31));
            break;
        case Kind::SecondaryStat:
            pk.secondaryStatCalc(stats[field.index], increment(pk.secondaryStatCalc(stats[field.index]),
                delta, 0, pk.generation() == Generation::LGPE ? 200 : 10));
            break;
        case Kind::EV:
        {
            const auto stat = stats[field.index];
            if (pk.generation() <= Generation::TWO)
            {
                pk.ev(stat, increment(pk.ev(stat), delta, 0, 65535));
                break;
            }
            int others = 0;
            for (const auto stat : stats)
                if (stat != stats[field.index])
                    others += pk.ev(stat);
            const auto cap = hax ? 255 : std::min(pk.generation() >= Generation::SIX ? 252 : 255,
                std::max(0, 510 - others));
            pk.ev(stat, increment(pk.ev(stat), delta, 0, std::max<int>(pk.ev(stat), cap)));
            break;
        }
        case Kind::HyperTraining:
            if (!hax && pk.level() < (pk.generation() >= Generation::NINE ? 50 : 100))
                throw std::runtime_error("Raise the level before applying Hyper Training.");
            pk.hyperTrain(stats[field.index], !pk.hyperTrain(stats[field.index]));
            break;
        case Kind::Form:
        {
            const auto count = save.formCount(pk.species());
            if (count > 1)
            {
                setFieldOption(pk, field, increment(pk.alternativeForm(), delta, 0, count - 1));
            }
            break;
        }
        case Kind::Decoration:
            pk.formArgument(increment(std::min<unsigned>(6, pk.formArgument()), delta, 0, 6));
            break;
        case Kind::Ball:
            if (!save.availableBalls().empty())
                setFieldOption(pk, field, static_cast<unsigned>(stepIn(save.availableBalls(), pk.ball(), delta)));
            break;
        case Kind::Origin:
        {
            const auto games = fieldOptions(save, pk, field);
            setFieldOption(pk, field, stepList(games, static_cast<unsigned>(pk.version()), delta));
            break;
        }
        case Kind::Language:
        {
            std::vector<Language> available;
            for (const auto language : languages)
                if (pksm::getSafeLanguage(pk.generation(), language) == language &&
                    (pk.generation() != Generation::THREE || (language == Language::JPN) == (pk.language() == Language::JPN)))
                    available.push_back(language);
            if (!available.empty())
                pk.language(stepList(available, pk.language(), delta));
            break;
        }
        case Kind::Fateful: pk.fatefulEncounter(!pk.fatefulEncounter()); break;
        case Kind::TrainerId: pk.TID(increment(pk.TID(), delta, 0, 65535)); break;
        case Kind::SecretId: pk.SID(increment(pk.SID(), delta, 0, 65535)); break;
        case Kind::MetDate:
        {
            using namespace std::chrono;
            const auto old = pk.metDate();
            const auto y = year(increment(old.year(), field.index == 0 ? delta : 0, 2000, 2255));
            const auto m = month(increment(old.month(), field.index == 1 ? delta : 0, 1, 12));
            const auto days = unsigned((y / m / last).day());
            pk.metDate(Date(increment(old.day(), field.index == 2 ? delta : 0, 1, days),
                unsigned(m), int(y)));
            break;
        }
        case Kind::EggLocation: pk.eggLocation(increment(pk.eggLocation(), delta, 0, 65535)); break;
    }
    pk.refreshChecksum();
}

std::vector<std::string> compatibility(const Sav& save, const PKX& pk)
{
    using Reason = Sav::BadTransferReason;
    std::vector<std::string> notes;
    switch (save.invalidTransferReason(pk))
    {
        case Reason::OKAY: break;
        case Reason::MOVE: notes.push_back("A move is unavailable in this game."); break;
        case Reason::SPECIES: notes.push_back("Species is unavailable in this game."); break;
        case Reason::FORM: notes.push_back("Form is unavailable in this game."); break;
        case Reason::ABILITY: notes.push_back("Ability is unavailable in this game."); break;
        case Reason::ITEM: notes.push_back("Held item is unavailable in this game."); break;
        case Reason::BALL: notes.push_back("Ball is unavailable in this game."); break;
    }
    if (pk.metLevel() > pk.level())
        notes.push_back("Met level is above the current level.");
    if (pk.generation() >= Generation::THREE && !dynamic_cast<const pksm::PA8*>(&pk))
    {
        unsigned total = 0;
        for (const auto stat : stats)
            total += pk.ev(stat);
        if (total > 510)
            notes.push_back("EV total exceeds 510.");
    }
    return notes;
}

bool sameFormat(const PKX& a, const PKX& b)
{
    return typeid(a) == typeid(b);
}

std::unique_ptr<PKX> previewTransfer(Save& save, int box, int slot, const PKX& pokemon)
{
    requireWritable(save);
    if (slot < 0 || slot >= boxSlotCount(*save.data, box))
        throw std::runtime_error("That box slot is unavailable.");
    const auto target = save.data->pkm(box, slot);
    if (target->species() != pksm::Species::None)
        throw std::runtime_error("Choose an empty box slot.");
    if (sameFormat(*target, pokemon))
        return pokemon.clone();
    if (pokemon.egg())
        throw std::runtime_error("Hatch eggs before converting between formats.");
    if (target->extension() == ".pb8" || pokemon.extension() == ".pb8")
        throw std::runtime_error("BDSP can transfer PB8 records between BDSP saves.");
    auto converted = save.data->transfer(pokemon);
    if (!converted || !sameFormat(*target, *converted) || converted->species() != pokemon.species() ||
        save.data->invalidTransferReason(*converted) != Sav::BadTransferReason::OKAY)
        throw std::runtime_error("No compatible conversion for this Pokemon and game.");
    return converted;
}

void editPartyMember(Save& save, int slot, PKX& draft, bool recalculateStats)
{
    requireWritable(save);
    if (slot < 0 || slot >= std::min(6, static_cast<int>(save.data->partyCount())))
        throw std::runtime_error("Choose an occupied party slot.");
    const auto original = save.data->pkm(slot);
    if (original->species() == pksm::Species::None || !sameFormat(*original, draft) ||
        original->species() != draft.species())
        throw std::runtime_error("Party contents changed. Reopen the Pokemon.");
    if (recalculateStats)
    {
        const int previousHP = original->partyCurrHP();
        const int damage = std::max(0, original->partyStat(pksm::Stat::HP) - previousHP);
        draft.updatePartyData();
        const int maximum = draft.partyStat(pksm::Stat::HP);
        if (previousHP >= 0 && maximum > 0)
            draft.partyCurrHP(previousHP == 0 ? 0 : std::max(1, maximum - damage));
    }
    draft.refreshChecksum();
    save.data->pkm(draft, slot);
    save.dirty = true;
}

std::string_view pouchName(Sav::Pouch pouch)
{
    using enum Sav::Pouch;
    switch (pouch)
    {
        case NormalItem: return "Items";
        case KeyItem: return "Key items";
        case TM: return "TMs & HMs";
        case Mail: return "Mail";
        case Medicine: return "Medicine";
        case Berry: return "Berries";
        case Ball: return "Balls";
        case Battle: return "Battle";
        case Candy: return "Candy";
        case ZCrystals: return "Z-Crystals";
        case Treasure: return "Treasure";
        case Ingredient: return "Ingredients";
        case PCItem: return "PC items";
        case RotomPower: return "Rotom powers";
        case CatchingItem: return "Catching items";
        case MegaStones: return "Mega Stones";
        case Recipe: return "Recipes";
        case Donut: return "Donuts";
    }
    return "Items";
}

BagSlot bagSlot(const Sav& save, Sav::Pouch pouch, int slot)
{
    const auto item = save.item(pouch, slot);
    if (!item)
        return {};
    return {item->id(), item->count(), std::min(item->maxCount(), save.maxCount(pouch))};
}

void changeBagCount(Save& save, Sav::Pouch pouch, int slot, unsigned count)
{
    requireWritable(save);
    const auto pouches = save.data->pouches();
    const auto range = std::ranges::find(pouches, pouch, &std::pair<Sav::Pouch, int>::first);
    if (range == pouches.end() || slot < 0 || slot >= range->second)
        throw std::runtime_error("Choose an item slot.");
    auto item = save.data->item(pouch, slot);
    if (!item || !item->id() || !item->count())
        throw std::runtime_error("Choose an existing item.");
    if (!count || count > std::min(item->maxCount(), save.data->maxCount(pouch)))
        throw std::runtime_error("Quantity is outside this pouch's limits.");
    item->count(count);
    save.data->item(*item, pouch, slot);
    save.dirty = true;
}

namespace
{
bool refillable(Sav::Pouch pouch)
{
    using enum Sav::Pouch;
    return pouch != KeyItem && pouch != PCItem && pouch != Mail && pouch != Recipe && pouch != Donut &&
        pouch != ZCrystals && pouch != MegaStones;
}

unsigned maxStack(const Sav& save, Sav::Pouch pouch, const pksm::Item& item)
{
    return std::min(item.maxCount(), save.maxCount(pouch));
}
}

std::vector<unsigned> bagCatalog(const Sav& save, Sav::Pouch pouch)
{
    if (!refillable(pouch))
        return {};
    const auto pouches = save.validItems();
    const auto match = std::ranges::find_if(pouches, [=](const auto& entry)
        { return entry.first == pouch; });
    std::vector<unsigned> catalog;
    if (match == pouches.end())
        return catalog;
    for (const int id : match->second)
        if (id > 0 && id <= 65535)
            catalog.push_back(id);
    std::ranges::sort(catalog);
    catalog.erase(std::unique(catalog.begin(), catalog.end()), catalog.end());
    return catalog;
}

void addBagItem(Save& save, Sav::Pouch pouch, unsigned id, unsigned count)
{
    requireWritable(save);
    if (!count || !std::ranges::binary_search(bagCatalog(*save.data, pouch), id))
        throw std::runtime_error("This item is unavailable in the selected pouch.");
    const auto pouches = save.data->pouches();
    const auto match = std::ranges::find_if(pouches, [=](const auto& entry)
        { return entry.first == pouch; });
    if (match == pouches.end())
        throw std::runtime_error("This pouch is unavailable.");
    const bool fixed = save.data->pouchIndexedByItem(pouch);
    int chosen = -1;
    for (int i = 0; i < match->second; ++i)
    {
        auto item = save.data->item(pouch, i);
        if (!item)
            continue;
        if (item->id() == id)
        {
            chosen = i;
            break;
        }
        if (!fixed && chosen == -1 && !item->id())
            chosen = i;
    }
    if (chosen < 0)
        throw std::runtime_error("The selected pouch has no room for this item.");
    auto item = save.data->item(pouch, chosen);
    const auto current = item->id() == id ? item->count() : 0u;
    if (count > maxStack(*save.data, pouch, *item) ||
        current > maxStack(*save.data, pouch, *item) - count)
        throw std::runtime_error("This quantity exceeds the stack limit.");
    item->id(id);
    if (item->id() != id)
        throw std::runtime_error("This item has no stored ID in this save format.");
    item->count(current + count);
    save.data->item(*item, pouch, chosen);
    save.dirty = true;
}

BagFill maxBag(Save& save, std::optional<Sav::Pouch> only, bool includeMissing)
{
    requireWritable(save);
    auto staged = stageSave(*save.data);
    BagFill result{};
    for (const auto [pouch, capacity] : staged->pouches())
    {
        if ((only && *only != pouch) || !refillable(pouch))
            continue;
        const auto allowed = bagCatalog(*staged, pouch);
        const bool fixed = staged->pouchIndexedByItem(pouch);
        std::vector<int> openSlots;
        std::set<unsigned> present;
        for (int pos = 0; pos < capacity; ++pos)
        {
            auto item = staged->item(pouch, pos);
            if (!item)
                continue;
            const unsigned id = item->id();
            if (!id && !fixed)
            {
                openSlots.push_back(pos);
                continue;
            }
            if (!std::ranges::binary_search(allowed, id))
                continue;
            if (item->count() != 0 || (includeMissing && fixed))
            {
                present.insert(id);
                const auto limit = maxStack(*staged, pouch, *item);
                if (item->count() < limit)
                {
                    item->count(limit);
                    staged->item(*item, pouch, pos);
                    ++result.stacks;
                }
            }
        }
        if (!includeMissing)
            continue;
        for (const auto id : allowed)
        {
            if (present.contains(id))
                continue;
            if (fixed || openSlots.empty())
            {
                ++result.skipped;
                continue;
            }
            auto item = staged->item(pouch, openSlots.front());
            item->id(id);
            if (item->id() != id)
            {
                ++result.skipped;
                continue;
            }
            item->count(maxStack(*staged, pouch, *item));
            staged->item(*item, pouch, openSlots.front());
            openSlots.erase(openSlots.begin());
            ++result.stacks;
        }
    }
    if (result.stacks)
    {
        save.data = std::move(staged);
        save.dirty = true;
    }
    return result;
}

void copyItemToSave(Save& save, StoredItem entry)
{
    requireWritable(save);
    const auto pouches = save.data->pouches();
    const auto allowed = save.data->validItems();
    if (!entry.id || entry.id > 65535 || !entry.count || entry.count > 9999)
        throw std::runtime_error("This item has no match in the destination game.");
    for (const auto [pouch, capacity] : pouches)
    {
        if (pouch == Sav::Pouch::KeyItem || pouch == Sav::Pouch::PCItem)
            continue;
        const auto available = std::ranges::find_if(allowed, [=](const auto& entry)
            { return entry.first == pouch; });
        if (available == allowed.end() || std::ranges::find(available->second, static_cast<int>(entry.id)) == available->second.end())
            continue;
        const bool fixed = save.data->pouchIndexedByItem(pouch);
        int candidate = -1;
        for (int i = 0; i < capacity; ++i)
        {
            const auto slot = bagSlot(*save.data, pouch, i);
            if (slot.id == entry.id)
            {
                candidate = slot.limit >= slot.count + entry.count ? i : -1;
                break;
            }
            if (!fixed && candidate < 0 && !slot.id && slot.limit >= entry.count)
                candidate = i;
        }
        if (candidate < 0)
            continue;
        auto item = save.data->item(pouch, candidate);
        const unsigned existing = item->id() == entry.id ? item->count() : 0;
        if (!fixed)
            item->id(entry.id);
        if (item->id() != entry.id)
            continue;
        item->count(existing + entry.count);
        save.data->item(*item, pouch, candidate);
        save.dirty = true;
        return;
    }
    throw std::runtime_error("No compatible pouch with space for this quantity.");
}

std::array<unsigned, 1026> livingDex(const Sav* save, const std::vector<File>& bank,
    bool shiny, unsigned* skipped)
{
    std::array<unsigned, 1026> counts{};
    const auto track = [&](const PKX& pk)
    {
        const auto id = static_cast<unsigned>(pk.species());
        if (id > 0 && id < counts.size() && !pk.egg() && (!shiny || pk.shiny()))
            ++counts[id];
    };
    if (save)
    {
        const int boxes = save->maxBoxes();
        const int perBox = save->generation() <= Generation::TWO ? save->maxSlot() / boxes : 30;
        for (int box = 0; box < boxes; ++box)
        {
            int slots = std::min(perBox, save->maxSlot() - box * perBox);
            for (int slot = 0; slot < slots; ++slot)
                track(*save->pkm(box, slot));
        }
        if (save->generation() != Generation::LGPE)
            for (int i = 0; i < std::min(6, static_cast<int>(save->partyCount())); ++i)
                track(*save->pkm(i));
    }
    for (const auto& file : bank)
    {
        try { track(*bankPokemon(file)); }
        catch (const std::exception&) { if (skipped) ++*skipped; }
    }
    return counts;
}

std::vector<Gift> readGifts(const std::string& path, int generation)
{
    struct Close { void operator()(FILE* f) const { std::fclose(f); } };
    const std::unique_ptr<FILE, Close> stream(std::fopen(path.c_str(), "rb"));
    if (!stream)
        throw std::runtime_error("Cannot open the event catalog.");
    const auto read = [&](void* data, size_t length)
    {
        if (std::fread(data, 1, length, stream.get()) != length)
            throw std::runtime_error("The event catalog is incomplete.");
    };
    u32 count = 0;
    read(&count, sizeof(count));
    if (count > 10000)
        throw std::runtime_error("Invalid event catalog size.");
    std::vector<Gift> gifts;
    for (u32 i = 0; i < count; ++i)
    {
        u16 size = 0;
        read(&size, sizeof(size));
        if (!size || size > 2048)
            throw std::runtime_error("Invalid event title.");
        std::string path(size, '\0');
        read(path.data(), size);
        u32 length = 0;
        read(&length, sizeof(length));
        if (!length || length > 4096)
            throw std::runtime_error("Invalid event card length.");
        const auto dot = path.find_last_of('.');
        if (dot == path.npos)
            throw std::runtime_error("Event card has no format.");
        const auto start = path.find_last_of("/\\");
        Gift gift{generation, path.substr(start == path.npos ? 0 : start + 1,
            dot - (start == path.npos ? 0 : start + 1)), path.substr(dot), std::vector<u8>(length)};
        read(gift.data.data(), length);
        gifts.push_back(std::move(gift));
    }
    return gifts;
}

static std::unique_ptr<pksm::WCX> cardFor(Gift& gift)
{
    std::unique_ptr<pksm::WCX> card;
    auto* data = gift.data.data();
    const auto size = gift.data.size();
    const auto& type = gift.extension;
    if ((type == ".pcd" || type == ".wc4") && size == pksm::PCD::length)
        card = std::make_unique<pksm::PCD>(data);
    else if (type == ".pgt" && size == pksm::PGT::length)
        card = std::make_unique<pksm::PGT>(data);
    else if (type == ".pgf" && size == pksm::PGF::length)
        card = std::make_unique<pksm::PGF>(data);
    else if ((type == ".wc6" && size == pksm::WC6::length) ||
        (type == ".wc6full" && size == pksm::WC6::lengthFull))
        card = std::make_unique<pksm::WC6>(data, type == ".wc6full");
    else if ((type == ".wc7" && size == pksm::WC7::length) ||
        (type == ".wc7full" && size == pksm::WC7::lengthFull))
        card = std::make_unique<pksm::WC7>(data, type == ".wc7full");
    else if (type == ".wc8" && size == pksm::WC8::length)
        card = std::make_unique<pksm::WC8>(data);
    else if (type == ".wc9" && size == pksm::WC9::length)
        card = std::make_unique<pksm::WC9>(data);
    else if ((type == ".wb7" && size == pksm::WB7::length) ||
             (type == ".wb7full" && size == pksm::WB7::lengthFull))
        card = std::make_unique<pksm::WB7>(data, type == ".wb7full");
    else if (type == ".wa9" && size == pksm::WA9::length)
        card = std::make_unique<pksm::WA9>(data);
    return card;
}

static bool directGift(const pksm::WCX& card)
{
    return card.generation() >= Generation::LGPE;
}

static bool canQueueCard(const Save& save, const pksm::WCX& card, const Gift& gift)
{
    if (!save.data || card.generation() != save.data->generation())
        return false;
    const auto version = save.data->version();
    const auto supportedGift = [&]
    {
        return card.item() || (card.pokemon() && save.data->availableSpecies().contains(card.species()) &&
            card.alternativeForm() < save.data->formCount(card.species()));
    };
    if (card.extension() == ".wb7")
    {
        const auto allowed = gift.extension == ".wb7full" ? gift.data[0] : 0;
        const int mask = version == pksm::GameVersion::GP ? 1 : 2;
        return (version == pksm::GameVersion::GP || version == pksm::GameVersion::GE) &&
            (!allowed || (allowed & mask)) && supportedGift();
    }
    if (card.extension() == ".wa9")
        return version == pksm::GameVersion::ZA && card.rawData()[0x0E] <= 1 && supportedGift();
    if (card.generation() == Generation::EIGHT || card.generation() == Generation::NINE)
    {
        const bool scarletViolet = card.generation() == Generation::NINE;
        if ((scarletViolet && !dynamic_cast<pksm::SavSV*>(save.data.get())) ||
            (!scarletViolet && !dynamic_cast<pksm::SavSWSH*>(save.data.get())))
            return false;
        const pksm::GameVersion first = scarletViolet ? pksm::GameVersion::SL : pksm::GameVersion::SW;
        const pksm::GameVersion second = scarletViolet ? pksm::GameVersion::VL : pksm::GameVersion::SH;
        const auto allowed = card.rawData()[0x0E];
        if (allowed > 3 || (allowed == 1 && version != first) || (allowed == 2 && version != second) ||
            (!scarletViolet && allowed == 0 && card.pokemon()))
            return false;
        return supportedGift();
    }
    return !directGift(card) && save.data->maxWondercards() != 0;
}

bool giftFitsSave(const Save& save, Gift& gift)
{
    const auto card = cardFor(gift);
    return card && canQueueCard(save, *card, gift);
}

// Gen V keeps the gift table encrypted even while ordinary save editing is active.
class OpenGiftTable
{
    pksm::Sav5* encrypted;
public:
    explicit OpenGiftTable(pksm::Sav& save) : encrypted(dynamic_cast<pksm::Sav5*>(&save))
    {
        if (encrypted) encrypted->cryptMysteryGiftData();
    }
    ~OpenGiftTable() { if (encrypted) encrypted->cryptMysteryGiftData(); }
};

static bool albumHasRoom(pksm::Sav& save, const pksm::WCX& card)
{
    if (auto* ds=dynamic_cast<pksm::Sav4*>(&save); ds && card.size()>=pksm::PCD::length && !ds->hasOpenCardAlbumSlot())
        return false;
    auto index=save.currentGiftAmount();
    return index>=0 && static_cast<size_t>(index)<save.maxWondercards();
}

bool canQueueGift(const Save& save, Gift& gift)
{
    if (!save.writable) return false;
    auto card=cardFor(gift);
    if (!card || !canQueueCard(save,*card,gift)) return false;
    if (directGift(*card)) return true;
    OpenGiftTable open(*save.data);
    return albumHasRoom(*save.data,*card);
}

void injectGift(Save& save, Gift& gift)
{
    requireWritable(save);
    auto card = cardFor(gift);
    if (!card || !canQueueCard(save, *card, gift))
        throw std::runtime_error("This Wonder Card format does not match the open save.");
    if (directGift(*card) && card->item())
    {
        Save staged{save.file, stageSave(*save.data), true, false};
        const auto apply = [&](const auto& gift)
        {
            if (gift.items() <= 0 || gift.items() > 8)
                throw std::runtime_error("The card does not contain a supported item bundle.");
            for (int i = 0; i < gift.items(); ++i)
                copyItemToSave(staged, {gift.object(i), gift.objectQuantity(i)});
        };
        if (const auto* wc9 = dynamic_cast<const pksm::WC9*>(card.get()))
            apply(*wc9);
        else if (const auto* wa9 = dynamic_cast<const pksm::WA9*>(card.get()))
            apply(*wa9);
        else if (const auto* wb7 = dynamic_cast<const pksm::WB7*>(card.get()))
            apply(*wb7);
        else
            apply(static_cast<const pksm::WC8&>(*card));
        save.data = std::move(staged.data);
        save.dirty = true;
        return;
    }
    int index = 0;
    if (directGift(*card))
    {
        auto staged = stageSave(*save.data);
        if (auto* portable = dynamic_cast<pksm::SavLGPE*>(staged.get()))
            portable->compressBox();
        for (int box = 0; box < staged->maxBoxes(); ++box)
            for (int slot = 0; slot < boxSlotCount(*staged, box); ++slot)
                if (staged->pkm(box, slot)->species() == pksm::Species::None)
                {
                    staged->mysteryGift(*card, index);
                    const auto written = staged->pkm(box, slot);
                    if (written->species() != card->species() || written->level() == 0)
                        throw std::runtime_error("The save did not accept this Pokemon gift.");
                    save.data = std::move(staged);
                    save.dirty = true;
                    return;
                }
        throw std::runtime_error("No empty slot for the event Pokemon.");
    }
    OpenGiftTable open(*save.data);
    if (!albumHasRoom(*save.data,*card))
        throw std::runtime_error("The Wonder Card album is full.");
    index = save.data->currentGiftAmount();
    save.data->mysteryGift(*card,index);
    save.dirty = true;
}

DexPlan planDexBox(const Save& save, unsigned start, bool shiny)
{
    requireWritable(save);
    if (!start || start > 1025)
        throw std::runtime_error("Choose a valid species number.");
    if (save.data->generation() <= Generation::TWO || save.data->generation() == Generation::LGPE)
        throw std::runtime_error("This game stores boxes in a compact list. Use Box tools to move entries.");
    DexPlan plan;
    plan.start = start - (start - 1) % 30;
    std::array<bool, 30> assigned{};
    for (int box = 0; box < save.data->maxBoxes(); ++box)
    {
        bool empty = true;
        for (int slot = 0; slot < 30; ++slot)
        {
            const auto pk = save.data->pkm(box, slot);
            const auto id = static_cast<unsigned>(pk->species());
            const bool assignedToTeam = boxSlotAssignedToTeam(*save.data, box, slot);
            if (id || assignedToTeam)
                empty = false;
            if (!id || assignedToTeam || pk->egg() || (shiny && !pk->shiny()) || id < plan.start || id >= plan.start + 30 ||
                assigned[id - plan.start])
                continue;
            assigned[id - plan.start] = true;
            plan.moves.push_back({box, slot, static_cast<int>(id - plan.start), id});
        }
        if (empty && plan.box < 0 && box < save.data->unlockedBoxes())
            plan.box = box;
    }
    if (plan.box < 0)
        throw std::runtime_error("Leave a completely empty box for the Living Dex plan.");
    std::ranges::sort(plan.moves, {}, &DexMove::toSlot);
    return plan;
}

void applyDexBox(Save& save, const DexPlan& plan)
{
    requireWritable(save);
    if (plan.moves.empty() || plan.box < 0 || plan.box >= save.data->unlockedBoxes())
        throw std::runtime_error("There are no moves to apply.");
    auto staged = stageSave(*save.data);
    std::vector<std::unique_ptr<PKX>> originals;
    std::array<bool, 30> destinations{};
    for (const auto& move : plan.moves)
    {
        if (move.fromBox == plan.box || move.fromBox < 0 || move.fromBox >= save.data->maxBoxes() ||
            move.fromSlot < 0 || move.fromSlot >= 30 || move.toSlot < 0 || move.toSlot >= 30 ||
            destinations[move.toSlot] || boxSlotAssignedToTeam(*staged, move.fromBox, move.fromSlot) ||
            boxSlotAssignedToTeam(*staged, plan.box, move.toSlot))
            throw std::runtime_error("The Living Dex plan contains an invalid slot.");
        destinations[move.toSlot] = true;
        auto pokemon = staged->pkm(move.fromBox, move.fromSlot);
        if (static_cast<unsigned>(pokemon->species()) != move.species ||
            staged->pkm(plan.box, move.toSlot)->species() != pksm::Species::None)
            throw std::runtime_error("Box contents changed. Refresh the plan.");
        originals.push_back(std::move(pokemon));
    }
    auto empty = staged->emptyPkm();
    for (size_t i = 0; i < plan.moves.size(); ++i)
    {
        const auto& move = plan.moves[i];
        staged->pkm(*originals[i], plan.box, move.toSlot, false);
        staged->pkm(*empty, move.fromBox, move.fromSlot, false);
    }
    save.data = std::move(staged);
    save.dirty = true;
}
}
