// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.hpp"
#include "editors.hpp"

#include "pkx/PK9.hpp"
#include "utils/i18n.hpp"
#include "pkx/PA8.hpp"
#include "pkx/PB8.hpp"
#include "sav/Sav8.hpp"
#include "sav/Sav3.hpp"
#include "sav/SavLGPE.hpp"
#include "utils/endian.hpp"
#include "utils/ValueConverter.hpp"
#include "utils/utils.hpp"
#include "utils/random.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <set>
#include <ranges>
#include <stdexcept>

namespace forge
{
namespace
{
using pksm::PKX;
using pksm::Sav;
using pksm::Species;
using pksm::Stat;
constexpr auto ENG = pksm::Language::ENG;
constexpr std::array stats{Stat::HP, Stat::ATK, Stat::DEF, Stat::SPATK, Stat::SPDEF, Stat::SPD};
constexpr std::array shortStats{"HP", "Atk", "Def", "SpA", "SpD", "Spe"};

void needSave(const Save& save)
{
    if (!save.data || !save.writable)
        throw std::runtime_error("Open an editable save first.");
}

bool occupied(const PKX& pk) { return pk.species() != Species::None; }

bool changed(const PKX& a, const PKX& b)
{
    if (!occupied(a) && !occupied(b))
        return false;
    return !std::ranges::equal(a.rawData(), b.rawData());
}

std::string_view trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    return first == value.npos ? std::string_view{} :
        value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

std::string key(std::string_view value)
{
    std::string normalized;
    for (const unsigned char ch : value)
        if (std::isalnum(ch))
            normalized.push_back(static_cast<char>(std::tolower(ch)));
    return normalized;
}

int number(std::string_view value, int min, int max)
{
    auto input = trim(value);
    int result = 0;
    if (input.empty())
        throw std::runtime_error("A Showdown quantity or stat is missing.");
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() ||
        result < min || result > max)
        throw std::runtime_error("A Showdown quantity or stat is out of range.");
    return result;
}

void statLine(std::string_view line, std::array<int, 6>& values, int maximum)
{
    while (!line.empty())
    {
        const auto split = line.find('/');
        const auto value = trim(line.substr(0, split));
        const auto gap = value.find(' ');
        if (gap == value.npos)
            throw std::runtime_error("A Showdown stat is missing its name.");
        const auto stat = trim(value.substr(gap));
        const auto it = std::ranges::find(shortStats, stat);
        if (it == shortStats.end())
            throw std::runtime_error("A Showdown stat name is unknown.");
        values[it - shortStats.begin()] = number(value.substr(0, gap), 0, maximum);
        if (split == line.npos)
            break;
        line.remove_prefix(split + 1);
    }
}

template <class Values, class Name>
std::optional<typename Values::value_type> findByName(
    const Values& values, std::string_view name, Name nameFor)
{
    const auto wanted = key(name);
    if (wanted.empty())
        return std::nullopt;
    const auto match = std::ranges::find_if(values, [&](auto value) { return key(nameFor(value)) == wanted; });
    return match == values.end() ? std::nullopt : std::optional(*match);
}
}

SaveDifference compareSaves(const Save& current, const Save& other)
{
    if (!current.data || !other.data || current.data->version() != other.data->version())
        throw std::runtime_error("Choose a snapshot of the same game.");
    const auto& a = *current.data;
    const auto& b = *other.data;
    SaveDifference diff{};
    const auto partyA = std::min<int>(a.partyCount(), 6);
    const auto partyB = std::min<int>(b.partyCount(), 6);
    diff.party = std::abs(partyA - partyB);
    for (int i = 0; i < std::min(partyA, partyB); ++i)
        diff.party += changed(*a.pkm(i), *b.pkm(i));
    for (int box = 0; box < std::min(a.maxBoxes(), b.maxBoxes()); ++box)
        for (int slot = 0; slot < std::min(boxSlotCount(a, box), boxSlotCount(b, box)); ++slot)
            diff.boxes += changed(*a.pkm(box, slot), *b.pkm(box, slot));
    diff.money = static_cast<int>(a.money()) - static_cast<int>(b.money());
    diff.trainer = a.TID() != b.TID() || a.SID() != b.SID() ||
        a.otName() != b.otName() || a.gender() != b.gender();
    return diff;
}

bool boxSlotAssignedToTeam(const Sav& save, int box, int slot)
{
    if (slot < 0 || slot >= boxSlotCount(save, box))
        return false;
    if (const auto* letsGo = dynamic_cast<const pksm::SavLGPE*>(&save))
    {
        const int index = box * pksm::SavLGPE::SlotsPerBox + slot;
        for (int i = 0; i < 6; ++i)
            if (letsGo->partyBoxSlot(i) == index)
                return true;
        return letsGo->followPkm() == index;
    }
    constexpr size_t IndexCount = 36;
    constexpr size_t BDSPTeamOffset = 0x148AA + 0x5D4;
    std::span<const u8> indexes;
    const auto version = save.version();
    if (version == pksm::GameVersion::BD || version == pksm::GameVersion::SP)
        indexes = {save.rawData().get() + BDSPTeamOffset, IndexCount * 2};
    else if (version == pksm::GameVersion::SW || version == pksm::GameVersion::SH ||
        version == pksm::GameVersion::SL || version == pksm::GameVersion::VL ||
        version == pksm::GameVersion::ZA)
    {
        const auto block = static_cast<const pksm::Sav8&>(save).getBlock(0x33F39467);
        if (block && block->length() >= IndexCount * 2)
            indexes = {block->decryptedData(), IndexCount * 2};
    }
    const u16 position = (box << 8) | slot;
    for (size_t i = 0; i < indexes.size(); i += 2)
        if (LittleEndian::convertTo<u16>(indexes.data() + i) == position)
            return true;
    return false;
}

unsigned organizeBox(Save& save, int box, std::span<const int> selected, BoxAction action)
{
    if (action != BoxAction::CopyToBank)
        needSave(save);
    if (!save.data || box < 0 || box >= save.data->maxBoxes())
        throw std::runtime_error("Open a box first.");
    const int count = boxSlotCount(*save.data, box);
    std::set<int> distinct;
    std::vector<std::unique_ptr<PKX>> entries;
    const auto collect = [&](int slot)
    {
        if (slot < 0 || slot >= count || !distinct.insert(slot).second)
            throw std::runtime_error("The selection contains an invalid slot.");
        auto pk = save.data->pkm(box, slot);
        if (occupied(*pk))
            entries.push_back(std::move(pk));
        else
            distinct.erase(slot);
    };
    if (action == BoxAction::Sort)
        for (int slot = 0; slot < count; ++slot)
            collect(slot);
    else
        for (const int slot : selected)
            collect(slot);
    if (entries.empty())
        throw std::runtime_error("Mark at least one occupied slot.");
    if (action == BoxAction::CopyToBank)
    {
        if (!canDeposit(*save.data))
            throw std::runtime_error("GameCube story records can be inspected in the save.");
        std::vector<const PKX*> borrowed;
        for (const auto& entry : entries)
            borrowed.push_back(entry.get());
        depositBatch(borrowed);
        return entries.size();
    }

    const auto protectedSlot = [&](int slot) { return boxSlotAssignedToTeam(*save.data, box, slot); };
    if ((action == BoxAction::Sort && std::ranges::any_of(std::views::iota(0, count), protectedSlot)) ||
        ((action == BoxAction::Move || action == BoxAction::Release) &&
            std::ranges::any_of(distinct, protectedSlot)))
        throw std::runtime_error("Remove affected entries from battle teams first.");

    auto staged = stageSave(*save.data);
    auto empty = staged->emptyPkm();
    if (action == BoxAction::Sort)
    {
        std::ranges::stable_sort(entries, {}, [](const auto& pk)
            { return std::pair{static_cast<unsigned>(pk->species()), pk->alternativeForm()}; });
        for (int slot = 0; slot < count; ++slot)
            staged->pkm(slot < static_cast<int>(entries.size()) ? *entries[slot] : *empty, box, slot, false);
    }
    else if (action == BoxAction::Move || action == BoxAction::Duplicate)
    {
        const int unlocked = std::clamp<int>(staged->unlockedBoxes(), 1, staged->maxBoxes());
        if (action == BoxAction::Move && unlocked < 2)
            throw std::runtime_error("Unlock at least two boxes before moving entries.");
        const int target = action == BoxAction::Move ? (box + 1) % unlocked : box;
        std::vector<int> available;
        for (int slot = 0; slot < boxSlotCount(*staged, target); ++slot)
            if (!occupied(*staged->pkm(target, slot)) && !boxSlotAssignedToTeam(*staged, target, slot))
                available.push_back(slot);
        if (available.size() < entries.size())
            throw std::runtime_error("The destination box does not have enough empty slots.");
        for (size_t i = 0; i < entries.size(); ++i)
            staged->pkm(*entries[i], target, available[i], false);
    }
    if (action == BoxAction::Move || action == BoxAction::Release)
        for (const int slot : distinct)
            staged->pkm(*empty, box, slot, false);

    save.data = std::move(staged);
    save.dirty = true;
    return entries.size();
}

std::vector<BoxIssue> auditBox(const Save& save, int box)
{
    if (!save.data || box < 0 || box >= save.data->maxBoxes())
        throw std::runtime_error("Open a box first.");
    std::vector<BoxIssue> issues;
    for (int slot = 0; slot < boxSlotCount(*save.data, box); ++slot)
    {
        const auto pk = save.data->pkm(box, slot);
        if (!occupied(*pk))
            continue;
        for (const auto& message : compatibility(*save.data, *pk))
            issues.push_back({slot + 1, message});
    }
    return issues;
}

unsigned registerOwned(Save& save)
{
    needSave(save);
    auto staged = stageSave(*save.data);
    const auto initial = staged->dexCaught();
    for (int box = 0; box < staged->maxBoxes(); ++box)
        for (int slot = 0; slot < boxSlotCount(*staged, box); ++slot)
        {
            const auto pokemon = staged->pkm(box, slot);
            if (occupied(*pokemon) && !pokemon->egg() && staged->availableSpecies().contains(pokemon->species()))
                staged->dex(*pokemon);
        }
    for (int i = 0; i < std::min<int>(6, staged->partyCount()); ++i)
    {
        const auto pokemon = staged->pkm(i);
        if (occupied(*pokemon) && !pokemon->egg() && staged->availableSpecies().contains(pokemon->species()))
            staged->dex(*pokemon);
    }
    const int registered = staged->dexCaught() - initial;
    if (registered > 0)
    {
        save.data = std::move(staged);
        save.dirty = true;
    }
    return std::max(0, registered);
}

CrossDexPlan planCrossDex(Save& current, const File& donorFile, unsigned start, bool shiny)
{
    needSave(current);
    if (current.file.path == donorFile.path || start == 0 || start > 1025)
        throw std::runtime_error("Choose a different source save and a valid species range.");
    if (current.data->generation() <= pksm::Generation::TWO || current.data->generation() == pksm::Generation::LGPE)
        throw std::runtime_error("This game stores boxes in a compact list. Use Box tools to move entries.");
    auto donor = openSave(donorFile);
    needSave(donor);
    CrossDexPlan plan{donorFile, -1, start - (start - 1) % 30, {}};
    for (int box = 0; box < std::min<int>(current.data->maxBoxes(), current.data->unlockedBoxes()); ++box)
    {
        bool empty = true;
        for (int slot = 0; slot < boxSlotCount(*current.data, box); ++slot)
            if (occupied(*current.data->pkm(box, slot)) || boxSlotAssignedToTeam(*current.data, box, slot))
            {
                empty = false;
                break;
            }
        if (empty)
        {
            plan.targetBox = box;
            break;
        }
    }
    if (plan.targetBox < 0)
        throw std::runtime_error("An unlocked, empty destination box is required.");
    std::array<bool, 30> assigned{};
    for (int box = 0; box < donor.data->maxBoxes(); ++box)
        for (int slot = 0; slot < boxSlotCount(*donor.data, box); ++slot)
        {
            const auto mon = donor.data->pkm(box, slot);
            const auto species = static_cast<unsigned>(mon->species());
            if (mon->egg() || (shiny && !mon->shiny()) || species < plan.start || species >= plan.start + 30 ||
                boxSlotAssignedToTeam(*donor.data, box, slot))
                continue;
            const int to = species - plan.start;
            if (assigned[to])
                continue;
            try
            {
                const auto converted = previewTransfer(current, plan.targetBox, to, *mon);
                if (current.data->invalidTransferReason(*converted) != Sav::BadTransferReason::OKAY)
                    continue;
                plan.moves.push_back({box, slot, to, species, {mon->rawData().begin(), mon->rawData().end()}});
                assigned[to] = true;
            }
            catch (const std::runtime_error&) {}
        }
    std::ranges::sort(plan.moves, {}, &CrossDexMove::targetSlot);
    return plan;
}

std::string applyCrossDex(Save& current, const CrossDexPlan& plan)
{
    needSave(current);
    if (plan.moves.empty() || current.file.path == plan.donor.path)
        throw std::runtime_error("There are no cross-save moves to apply.");
    auto donor = openSave(plan.donor);
    needSave(donor);
    auto destination = Save{current.file, stageSave(*current.data), true, true};
    auto empty = donor.data->emptyPkm();
    for (const auto& move : plan.moves)
    {
        auto original = donor.data->pkm(move.box, move.slot);
        if (!std::ranges::equal(original->rawData(), move.original) ||
            boxSlotAssignedToTeam(*donor.data, move.box, move.slot) ||
            boxSlotAssignedToTeam(*destination.data, plan.targetBox, move.targetSlot))
            throw std::runtime_error("The source changed. Refresh your transfer plan.");
        auto transferred = previewTransfer(destination, plan.targetBox, move.targetSlot, *original);
        putPokemon(destination, plan.targetBox, move.targetSlot, *transferred);
        donor.data->pkm(*empty, move.box, move.slot, false);
    }
    const auto path = publishSavePair(destination, donor);
    destination.file.path = path + "/destination/" +
        current.file.path.substr(current.file.path.find_last_of('/') + 1);
    destination.dirty = false;
    current = std::move(destination);
    return path;
}

namespace
{
pksm::Sav3& hoennClock(const Save& save)
{
    auto* gba = dynamic_cast<pksm::Sav3*>(save.data.get());
    if (!gba || (gba->version() != pksm::GameVersion::S && gba->version() != pksm::GameVersion::E))
        throw std::runtime_error("Open Ruby, Sapphire, or Emerald for the clock editor.");
    return *gba;
}
}

ClockFields gbaClock(const Save& save)
{
    const auto& gba = hoennClock(save);
    return {gba.rtcInitialDay(), gba.rtcInitialHour(), gba.rtcInitialMinute(), gba.rtcInitialSecond(),
        gba.rtcElapsedDay(), gba.rtcElapsedHour(), gba.rtcElapsedMinute(), gba.rtcElapsedSecond()};
}

void applyGbaClock(Save& save, const ClockFields& fields)
{
    needSave(save);
    constexpr std::array<unsigned, 4> limits{65535, 23, 59, 59};
    for (size_t i = 0; i < fields.size(); ++i)
        if (fields[i] > limits[i % 4])
            throw std::runtime_error("A clock value is outside the game's range.");
    if (gbaClock(save) == fields)
        return;
    auto& gba = hoennClock(save);
    gba.rtcInitialDay(fields[0]);
    gba.rtcInitialHour(fields[1]);
    gba.rtcInitialMinute(fields[2]);
    gba.rtcInitialSecond(fields[3]);
    gba.rtcElapsedDay(fields[4]);
    gba.rtcElapsedHour(fields[5]);
    gba.rtcElapsedMinute(fields[6]);
    gba.rtcElapsedSecond(fields[7]);
    save.dirty = true;
}

std::vector<HallTeam> hallOfFame(const Save& save)
{
    if (!save.data || save.data->generation() != pksm::Generation::THREE ||
        save.data->getEntireLengthIncludingFooter() < 0x1E000)
        throw std::runtime_error("Open a full Game Boy Advance save to view the Hall of Fame.");
    constexpr size_t SectorData = 0xF80;
    constexpr size_t MemberSize = 0x14;
    const auto* bytes = save.data->rawData().get();
    std::array<u8, 2 * SectorData> records{};
    std::copy_n(bytes + 0x1C000, SectorData, records.begin());
    std::copy_n(bytes + 0x1D000, SectorData, records.begin() + SectorData);
    std::vector<HallTeam> teams;
    for (int i = 0; i < 50; ++i)
    {
        HallTeam team{i + 1, {}};
        for (int slot = 0; slot < 6; ++slot)
        {
            const auto offset = (i * 6 + slot) * MemberSize;
            const auto values = LittleEndian::convertTo<u16>(records.data() + offset + 8);
            const unsigned level = values >> 9;
            const auto species = pksm::SpeciesConverter::g3ToNational(values & 0x1ff);
            if (!level || level > 100 || species == Species::None || species > Species::Deoxys)
                continue;
            const auto ids = LittleEndian::convertTo<u32>(records.data() + offset);
            const auto pid = LittleEndian::convertTo<u32>(records.data() + offset + 4);
            const auto shiny = u16(ids ^ pid ^ (ids >> 16) ^ (pid >> 16)) < 8;
            const bool japanese = save.data->language() == pksm::Language::JPN;
            team.members.push_back({species, StringUtils::getString3(records.data(), offset + 10, 10, japanese),
                level, shiny});
        }
        if (!team.members.empty())
            teams.push_back(std::move(team));
    }
    return teams;
}

namespace
{
constexpr size_t BDSPNursery = 0x96080;
constexpr size_t BDSPNurseryEgg = BDSPNursery + 2 * pksm::PB8::PARTY_LENGTH;
}

Nursery nursery(const Save& save)
{
    if (!save.data || (save.data->version() != pksm::GameVersion::BD &&
        save.data->version() != pksm::GameVersion::SP))
        throw std::runtime_error("Open Brilliant Diamond or Shining Pearl to inspect the Nursery.");
    auto* bytes = save.data->rawData().get();
    Nursery result{};
    for (int i = 0; i < 2; ++i)
        result.parents[i] = PKX::getPKM<pksm::PB8>(bytes + BDSPNursery + i * pksm::PB8::PARTY_LENGTH,
            pksm::PB8::PARTY_LENGTH);
    result.eggReady = LittleEndian::convertTo<u32>(bytes + BDSPNurseryEgg) != 0;
    return result;
}

void withdrawNursery(Save& save, int parent, int box, int slot)
{
    needSave(save);
    const auto info = nursery(save);
    if (parent < 0 || parent >= static_cast<int>(info.parents.size()) || !occupied(*info.parents[parent]))
        throw std::runtime_error("Choose an occupied Nursery slot.");
    if (info.eggReady)
        throw std::runtime_error("Collect the waiting egg in-game before withdrawing.");
    if (box < 0 || box >= save.data->unlockedBoxes() || slot < 0 || slot >= boxSlotCount(*save.data, box) ||
        boxSlotAssignedToTeam(*save.data, box, slot))
        throw std::runtime_error("Select an empty, unlocked box slot.");
    Save draft{save.file, stageSave(*save.data), true, false};
    putPokemon(draft, box, slot, *info.parents[parent]);
    auto* bytes = draft.data->rawData().get();
    std::fill_n(bytes + BDSPNursery + parent * pksm::PB8::PARTY_LENGTH, pksm::PB8::PARTY_LENGTH, 0);
    save.data = std::move(draft.data);
    save.dirty = true;
}

void removeBagStack(Save& save, Sav::Pouch pouch, int slot)
{
    needSave(save);
    const auto pouches = save.data->pouches();
    const auto range = std::ranges::find_if(pouches, [&](auto entry) { return entry.first == pouch; });
    if (range == pouches.end() || slot < 0 || slot >= range->second)
        throw std::runtime_error("Choose an item stack.");
    const auto item = bagSlot(*save.data, pouch, slot);
    if (!item.count || !std::ranges::binary_search(bagCatalog(*save.data, pouch), item.id))
        throw std::runtime_error("That stack cannot be removed.");
    auto staged = stageSave(*save.data);
    if (!staged->pouchIndexedByItem(pouch))
        while (slot + 1 < range->second)
        {
            auto next = staged->item(pouch, slot + 1);
            if (!next || !next->id() || !next->count())
                break;
            staged->item(*next, pouch, slot++);
        }
    auto empty = staged->item(pouch, slot);
    empty->count(0);
    if (!staged->pouchIndexedByItem(pouch))
        empty->id(0);
    staged->item(*empty, pouch, slot);
    save.data = std::move(staged);
    save.dirty = true;
}

void applyBagPreset(Save& save, std::span<const BagPresetEntry> entries)
{
    needSave(save);
    Save staged{save.file, stageSave(*save.data), true, false};
    const auto pouches = staged.data->pouches();
    std::set<std::pair<Sav::Pouch, unsigned>> seen;
    for (const auto& entry : entries)
    {
        const auto pouch = std::ranges::find_if(pouches,
            [&](const auto& value) { return value.first == entry.pouch; });
        if (pouch == pouches.end() || !entry.count || !seen.emplace(entry.pouch, entry.item).second ||
            !std::ranges::binary_search(bagCatalog(*staged.data, entry.pouch), entry.item))
            throw std::runtime_error("The bag preset contains a stack this save cannot restore.");
        int slot = 0;
        for (; slot < pouch->second; ++slot)
        {
            const auto stack = bagSlot(*staged.data, entry.pouch, slot);
            if (stack.id == entry.item)
            {
                if (entry.count > stack.limit)
                    throw std::runtime_error("The preset quantity exceeds this pouch's stack limit.");
                if (stack.count)
                    changeBagCount(staged, entry.pouch, slot, entry.count);
                else
                    addBagItem(staged, entry.pouch, entry.item, entry.count);
                break;
            }
        }
        if (slot == pouch->second)
            addBagItem(staged, entry.pouch, entry.item, entry.count);
    }
    if (staged.dirty)
    {
        save.data = std::move(staged.data);
        save.dirty = true;
    }
}

namespace
{
std::string showdownSpecies(Species species, unsigned form, pksm::GameVersion game)
{
    const auto& name = species.localize(ENG);
    if (!form)
        return name;
    auto suffix = i18n::form(ENG, game, species, form);
    if (suffix == "Alolan") suffix = "Alola";
    if (suffix == "Galarian") suffix = "Galar";
    if (suffix == "Hisuian") suffix = "Hisui";
    if (suffix == "Paldean") suffix = "Paldea";
    for (const std::string_view ending : {" Forme", " Form", " Breed", " Style"})
        if (suffix.ends_with(ending))
            suffix.resize(suffix.size() - ending.size());
    std::ranges::replace(suffix, ' ', '-');
    return suffix.empty() ? "" : name + "-" + suffix;
}

std::pair<Species, unsigned> showdownForm(const Sav& save, std::string_view heading)
{
    const auto base = findByName(save.availableSpecies(), heading,
        [](Species id) { return id.localize(ENG); });
    if (base)
        return {*base, 0};
    const auto wanted = key(heading);
    for (const auto species : save.availableSpecies())
    {
        const auto name = key(species.localize(ENG));
        if (!wanted.starts_with(name))
            continue;
        for (unsigned form = 1; form < save.formCount(species); ++form)
        {
            const auto aliases = showdownSpecies(species, form, save.version());
            if ((!aliases.empty() && key(aliases) == wanted) ||
                name + key(i18n::form(ENG, save.version(), species, form)) == wanted)
                return {species, form};
        }
    }
    throw std::runtime_error("The Showdown species or form is unavailable in this game.");
}
}

std::string showdownSet(const PKX& pokemon)
{
    if (!occupied(pokemon) || pokemon.egg())
        throw std::runtime_error("Choose a hatched Pokemon.");
    std::string result = showdownSpecies(pokemon.species(), pokemon.alternativeForm(), pokemon.version());
    if (result.empty())
        throw std::runtime_error("This form has no Showdown name in the game tables.");
    if (pokemon.nicknamed() && pokemon.nickname() != pokemon.species().localize(ENG) && !pokemon.nickname().empty())
        result = pokemon.nickname() + " (" + result + ")";
    if (pokemon.gender() < pksm::Gender::Genderless)
        result += pokemon.gender() == pksm::Gender::Female ? " (F)" : " (M)";
    if (pokemon.heldItem())
        result += " @ " + i18n::item(ENG, pokemon.heldItem());
    result += "\nAbility: " + pokemon.ability().localize(ENG);
    result += "\nLevel: " + std::to_string(pokemon.level());
    if (pokemon.shiny())
        result += "\nShiny: Yes";
    if (const auto* paldea = dynamic_cast<const pksm::PK9*>(&pokemon); paldea && paldea->teraTypeOverride() < 19)
        result += "\nTera Type: " + i18n::type(ENG, pksm::Type(paldea->teraTypeOverride()));
    const auto writeStats = [&](std::string_view label, auto read, int omitted)
    {
        bool first = true;
        for (size_t i = 0; i < stats.size(); ++i)
        {
            const int value = read(stats[i]);
            if (value == omitted)
                continue;
            if (first)
            {
                result += "\n";
                result += label;
            }
            else
                result += " / ";
            result += std::to_string(value) + " " + shortStats[i];
            first = false;
        }
    };
    writeStats("EVs: ", [&](Stat stat) { return pokemon.ev(stat); }, 0);
    result += "\n" + pokemon.nature().localize(ENG) + " Nature";
    writeStats("IVs: ", [&](Stat stat) { return pokemon.iv(stat); }, 31);
    for (int i = 0; i < 4; ++i)
        if (const auto move = pokemon.move(i); move != pksm::Move::None)
            result += "\n- " + move.localize(ENG);
    return result + "\n";
}

std::string showdownParty(const Sav& save)
{
    std::string result;
    for (int i = 0; i < std::min<int>(6, save.partyCount()); ++i)
    {
        const auto pokemon = save.pkm(i);
        if (!occupied(*pokemon) || pokemon->egg())
            continue;
        if (!result.empty())
            result += "\n";
        result += showdownSet(*pokemon);
    }
    return result;
}

std::unique_ptr<PKX> previewShowdown(Save& save, std::string_view text)
{
    needSave(save);
    auto pokemon = save.data->emptyPkm();
    if (save.data->generation() < pksm::Generation::SIX || dynamic_cast<pksm::PA8*>(pokemon.get()))
        throw std::runtime_error("Showdown import requires a save with conventional IVs and EVs.");
    if (text.size() > 8192)
        throw std::runtime_error("Showdown input exceeds 8 KB.");
    auto nextLine = [&]()
    {
        const auto end = text.find('\n');
        const auto line = trim(text.substr(0, end));
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        return line;
    };
    auto heading = nextLine();
    const auto at = heading.find(" @ ");
    const auto held = at == heading.npos ? std::string_view{} : heading.substr(at + 3);
    heading = trim(heading.substr(0, at));
    auto gender = -1;
    if (heading.ends_with(" (M)") || heading.ends_with(" (F)"))
    {
        gender = heading[heading.size() - 2] == 'F';
        heading.remove_suffix(4);
    }
    std::string_view nickname;
    if (const auto open = heading.rfind(" ("); open != heading.npos && heading.ends_with(")"))
    {
        nickname = trim(heading.substr(0, open));
        heading = heading.substr(open + 2, heading.size() - open - 3);
    }
    const auto [species, form] = showdownForm(*save.data, heading);
    pokemon->species(species);
    pokemon->alternativeForm(form);
    pokemon->version(save.data->version());
    pokemon->language(save.data->language());
    pokemon->otName(save.data->otName());
    pokemon->TID(save.data->TID());
    pokemon->SID(save.data->SID());
    pokemon->otGender(save.data->gender());
    pokemon->PID(pksm::randomNumber(1, 0xffffffff));
    pokemon->encryptionConstant(pksm::randomNumber(1, 0xffffffff));
    pokemon->ball(pksm::Ball::Poke);
    pokemon->otFriendship(pokemon->baseFriendship());
    pokemon->nicknamed(!nickname.empty());
    pokemon->nickname(nickname.empty() ? species.localize(ENG) : nickname);
    pokemon->setAbility(0);
    if (gender >= 0)
        pokemon->gender(pksm::Gender(gender));
    else
        pokemon->gender(PKX::genderFromRatio(pokemon->PID(), pokemon->genderType()));
    if (!held.empty())
    {
        const auto item = findByName(save.data->availableItems(), held,
            [](int id) { return i18n::item(ENG, id); });
        if (!item)
            throw std::runtime_error("Unknown or unavailable Showdown held item.");
        pokemon->heldItem(*item);
    }
    std::array<int, 6> iv{31, 31, 31, 31, 31, 31}, ev{};
    int move = 0, level = 100;
    bool shiny = false;
    if (auto* paldea = dynamic_cast<pksm::PK9*>(pokemon.get()))
        paldea->teraTypeOverride(19);
    pksm::Nature nature = pksm::Nature::Hardy;
    while (!text.empty())
    {
        const auto line = nextLine();
        if (line.empty())
        {
            if (!trim(text).empty())
                throw std::runtime_error("Import one Showdown set at a time.");
            break;
        }
        if (line.starts_with("Level:"))
            level = number(line.substr(6), 1, 100);
        else if (line.starts_with("Shiny:"))
            shiny = key(line.substr(6)) == "yes";
        else if (line.starts_with("Happiness:"))
            pokemon->otFriendship(number(line.substr(10), 0, 255));
        else if (line.starts_with("Tera Type:"))
        {
            auto* paldea = dynamic_cast<pksm::PK9*>(pokemon.get());
            if (!paldea)
                throw std::runtime_error("Tera Type requires a Scarlet or Violet save.");
            const auto wanted = key(line.substr(10));
            int type = 0;
            while (type < 19 && key(i18n::type(ENG, pksm::Type(type))) != wanted)
                ++type;
            if (type == 19)
                throw std::runtime_error("Unknown Showdown Tera Type.");
            paldea->teraTypeOverride(type);
        }
        else if (line.starts_with("EVs:"))
            statLine(line.substr(4), ev, 252);
        else if (line.starts_with("IVs:"))
            statLine(line.substr(4), iv, 31);
        else if (line.starts_with("Ability:"))
        {
            const auto wanted = key(line.substr(8));
            int slot = 0;
            while (slot < 3 && key(pokemon->abilities(slot).localize(ENG)) != wanted)
                ++slot;
            if (slot == 3)
                throw std::runtime_error("The Showdown ability does not match this species.");
            pokemon->setAbility(slot);
        }
        else if (line.ends_with(" Nature"))
        {
            int id = 0;
            while (id < 25 && key(pksm::Nature(id).localize(ENG)) != key(line.substr(0, line.size() - 7)))
                ++id;
            if (id == 25)
                throw std::runtime_error("Unknown Showdown nature.");
            nature = pksm::Nature(id);
        }
        else if (line.starts_with("- "))
        {
            const auto id = findByName(save.data->availableMoves(), line.substr(2),
                [](pksm::Move value) { return value.localize(ENG); });
            if (!id || move >= 4)
                throw std::runtime_error("A Showdown move is unavailable or there are more than four moves.");
            pokemon->move(move, *id);
            pokemon->PP(move, pokemon->maxPP(move));
            ++move;
        }
        else
            throw std::runtime_error("A Showdown option is unsupported.");
    }
    int totalEV = 0;
    for (size_t i = 0; i < stats.size(); ++i)
    {
        pokemon->iv(stats[i], iv[i]);
        pokemon->ev(stats[i], ev[i]);
        totalEV += ev[i];
    }
    if (totalEV > 510)
        throw std::runtime_error("Showdown EV total exceeds 510.");
    pokemon->level(level);
    pokemon->metLevel(level);
    pokemon->nature(nature);
    pokemon->shiny(shiny);
    pokemon->refreshChecksum();
    return pokemon;
}
std::vector<ShowdownEntry> previewShowdownTeam(Save& save, std::string_view text, int box, int slot)
{
    needSave(save);
    if (text.size() > 8192 || trim(text).empty())
        throw std::runtime_error("Provide one to six Showdown sets in a text file under 8 KB.");
    std::vector<ShowdownEntry> entries;
    const int boxes = std::min<int>(save.data->maxBoxes(), save.data->unlockedBoxes());
    if (box < 0 || box >= boxes || slot < 0 || slot >= boxSlotCount(*save.data, box) ||
        occupied(*save.data->pkm(box, slot)) || boxSlotAssignedToTeam(*save.data, box, slot))
        throw std::runtime_error("Choose an empty slot in an unlocked box first.");

    const auto addSet = [&](std::string_view set)
    {
        if (entries.size() == 6)
            throw std::runtime_error("Import up to six Pokemon at a time.");
        while (box < boxes)
        {
            if (slot >= boxSlotCount(*save.data, box))
            {
                ++box;
                slot = 0;
                continue;
            }
            if (!occupied(*save.data->pkm(box, slot)) && !boxSlotAssignedToTeam(*save.data, box, slot))
                break;
            ++slot;
        }
        if (box >= boxes)
            throw std::runtime_error("The team needs more empty slots after the selected slot.");
        entries.push_back({box, slot++, previewShowdown(save, set)});
    };
    size_t start = text.npos;
    for (size_t line = 0; line < text.size();)
    {
        const size_t end = std::min(text.find('\n', line), text.size());
        if (!trim(text.substr(line, end - line)).empty())
        {
            if (start == text.npos)
                start = line;
        }
        else if (start != text.npos)
        {
            addSet(text.substr(start, line - start));
            start = text.npos;
        }
        line = end == text.size() ? end : end + 1;
    }
    if (start != text.npos)
        addSet(text.substr(start));
    return entries;
}

void applyShowdownTeam(Save& save, std::span<ShowdownEntry> team)
{
    needSave(save);
    if (team.empty() || team.size() > 6)
        throw std::runtime_error("Review a team before importing.");
    Save staged{save.file, stageSave(*save.data), true, false};
    for (const auto& entry : team)
    {
        if (boxSlotAssignedToTeam(*staged.data, entry.box, entry.slot))
            throw std::runtime_error("A battle team uses a selected slot.");
        putPokemon(staged, entry.box, entry.slot, *entry.pokemon);
    }
    save.data = std::move(staged.data);
    save.dirty = true;
}

}
