// SPDX-License-Identifier: GPL-3.0-or-later
#include "sav/SavBDSP.hpp"

#include "pkx/PB8.hpp"
#include "bdsp_tables.hpp"
#include "utils/endian.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>

namespace
{
// Offsets and revision values follow PKHeX SAV8BS (GPL-3.0).
constexpr size_t Bag = 0x563C;
constexpr size_t BagRecordSize = 16;
constexpr size_t Pokedex = 0x7A328;
constexpr size_t SpeciesCount = 493;
constexpr size_t PartyStart = 0x14098;
constexpr size_t Layout = 0x148AA;
constexpr size_t Storage = 0x14EF4;
constexpr size_t ConfigStart = 0x79B74;
constexpr size_t Trainer = 0x79BB4;
constexpr size_t Played = 0x79C04;
constexpr size_t HashOffset = 0xE9818;

std::array<u8, 16> saveHash(std::span<const u8> bytes)
{
    // MD5 as defined by RFC 1321. The on-disk digest field is zeroed for hashing.
    constexpr std::array<u32, 64> k{
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    constexpr std::array rotations{7,12,17,22, 5,9,14,20, 4,11,16,23, 6,10,15,21};
    std::array<u32, 4> hash{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    const size_t padded = (bytes.size() + 9 + 63) & ~size_t(63);
    const auto bitSize = static_cast<u64>(bytes.size()) * 8;
    for (size_t start = 0; start < padded; start += 64)
    {
        std::array<u32, 16> words{};
        for (size_t i = 0; i < 64; ++i)
        {
            const size_t offset = start + i;
            u8 byte = 0;
            if (offset < bytes.size() && (offset < HashOffset || offset >= HashOffset + 16))
                byte = bytes[offset];
            else if (offset == bytes.size())
                byte = 0x80;
            else if (offset >= padded - 8)
                byte = bitSize >> ((offset - (padded - 8)) * 8);
            words[i / 4] |= static_cast<u32>(byte) << (i % 4 * 8);
        }
        auto [a, b, c, d] = hash;
        for (int i = 0; i < 64; ++i)
        {
            u32 f;
            int index;
            if (i < 16) { f = (b & c) | (~b & d); index = i; }
            else if (i < 32) { f = (d & b) | (~d & c); index = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; index = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); index = (7 * i) % 16; }
            const auto sum = a + f + k[i] + words[index];
            a = d;
            d = c;
            c = b;
            b += std::rotl(sum, rotations[i / 16 * 4 + i % 4]);
        }
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
    }
    std::array<u8, 16> result{};
    for (size_t i = 0; i < hash.size(); ++i)
        LittleEndian::convertFrom<u32>(result.data() + i * 4, hash[i]);
    return result;
}

using Pouch = pksm::Sav::Pouch;
using PouchTable = std::pair<Pouch, std::span<const int>>;
constexpr std::array<PouchTable, 8> Tables{{
    {Pouch::NormalItem, bdsp_data::General}, {Pouch::KeyItem, bdsp_data::Key},
    {Pouch::TM, bdsp_data::Machine}, {Pouch::Medicine, bdsp_data::Medicine},
    {Pouch::Berry, bdsp_data::Berry}, {Pouch::Ball, bdsp_data::Balls},
    {Pouch::Battle, bdsp_data::Battle}, {Pouch::Treasure, bdsp_data::Treasure},
}};

std::span<const int> itemsFor(Pouch pouch)
{
    const auto table = std::ranges::find(Tables, pouch, &PouchTable::first);
    return table != Tables.end() ? table->second : std::span<const int>{};
}

class BDSPItem final : public pksm::Item
{
    u16 itemId;
    u16 quantity;
    u16 limit;

public:
    BDSPItem(u16 id, u16 count, u16 max) : itemId(id), quantity(count), limit(max) {}
    pksm::Generation generation() const override { return pksm::Generation::EIGHT; }
    u16 maxCount() const override { return limit; }
    u16 id() const override { return itemId; }
    u16 count() const override { return quantity; }
    void id(u16 value) override { itemId = value; }
    void count(u16 value) override { quantity = std::min(value, limit); }
    SmallVector<u8, 16> bytes() const override
    {
        std::array<u8, 16> record{};
        LittleEndian::convertFrom<u32>(record.data(), quantity);
        return {std::span(record)};
    }
};

void writeEntity(const pksm::PKX& entity, u8* destination)
{
    if (!dynamic_cast<const pksm::PB8*>(&entity))
        throw std::runtime_error("BDSP storage requires a PB8 entity.");
    auto copy = entity.clone();
    if (!copy->isParty())
    {
        auto expanded = pksm::PKX::getPKM<pksm::PB8>(nullptr, pksm::PB8::PARTY_LENGTH);
        std::ranges::copy(copy->rawData(), expanded->rawData().begin());
        if (expanded->species() != pksm::Species::None)
            expanded->updatePartyData();
        copy = std::move(expanded);
    }
    copy->refreshChecksum();
    copy->encrypt();
    std::ranges::copy(copy->rawData(), destination);
}
}

namespace pksm
{
bool SavBDSP::recognized(const std::shared_ptr<u8[]>& bytes, size_t length)
{
    constexpr std::array revisions{
        std::pair{0xE9828u, 0x25u}, std::pair{0xEDC20u, 0x2Cu},
        std::pair{0xEED8Cu, 0x32u}, std::pair{0xEF0A4u, 0x34u}};
    const auto revision = std::ranges::find(revisions, length, &std::pair<unsigned, unsigned>::first);
    return revision != revisions.end() &&
        LittleEndian::convertTo<u32>(bytes.get()) == revision->second && bytes[Trainer + 0x2B] <= 1;
}

bool SavBDSP::checksumsValid() const
{
    const auto hash = saveHash({data.get(), length});
    return std::equal(hash.begin(), hash.end(), data.get() + HashOffset);
}

GameVersion SavBDSP::version() const { return data[Trainer + 0x2B] == 0 ? GameVersion::BD : GameVersion::SP; }
u16 SavBDSP::TID() const { return LittleEndian::convertTo<u16>(data.get() + Trainer + 0x1C); }
u16 SavBDSP::SID() const { return LittleEndian::convertTo<u16>(data.get() + Trainer + 0x1E); }
Gender SavBDSP::gender() const { return data[Trainer + 0x24] == 1 ? Gender::Male : Gender::Female; }
Language SavBDSP::language() const
{
    return Language(LittleEndian::convertTo<u32>(data.get() + ConfigStart + 4));
}
std::string SavBDSP::otName() const { return StringUtils::getString(data.get(), Trainer, 13); }
u32 SavBDSP::money() const { return LittleEndian::convertTo<u32>(data.get() + Trainer + 0x20); }
u8 SavBDSP::badges() const { return data[Trainer + 0x29]; }
u16 SavBDSP::playedHours() const { return LittleEndian::convertTo<u16>(data.get() + Played); }
u8 SavBDSP::playedMinutes() const { return data[Played + 2]; }
u8 SavBDSP::playedSeconds() const { return data[Played + 3]; }
void SavBDSP::playedHours(u16 value) { LittleEndian::convertFrom<u16>(data.get() + Played, value); }
void SavBDSP::playedMinutes(u8 value) { data[Played + 2] = std::min<u8>(value, 59); }
void SavBDSP::playedSeconds(u8 value) { data[Played + 3] = std::min<u8>(value, 59); }
u8 SavBDSP::currentBox() const { return std::min<int>(data[Layout + 0x61E], maxBoxes() - 1); }
u8 SavBDSP::unlockedBoxes() const { return std::clamp<int>(data[Layout + 0x61D], 1, maxBoxes()); }
u32 SavBDSP::boxOffset(u8 box, u8 slot) const { return Storage + (box * 30 + slot) * PK8::PARTY_LENGTH; }
u32 SavBDSP::partyOffset(u8 slot) const { return PartyStart + slot * PK8::PARTY_LENGTH; }
u8 SavBDSP::partyCount() const { return std::min<int>(data[PartyStart + 6 * PK8::PARTY_LENGTH], 6); }
u8 SavBDSP::boxWallpaper(u8 box) const
{
    return std::max(0, data[Layout + 0x620 + box] - 1);
}

std::string SavBDSP::boxName(u8 box) const
{
    const auto name = StringUtils::getString(data.get(), Layout + box * 0x22, 17);
    return name.empty() ? "Box " + std::to_string(box + 1) : name;
}

std::unique_ptr<PKX> SavBDSP::pkm(u8 slot) const
{
    return slot < 6 ? PKX::getPKM<PB8>(data.get() + partyOffset(slot), PK8::PARTY_LENGTH) : emptyPkm();
}

std::unique_ptr<PKX> SavBDSP::pkm(u8 box, u8 slot) const
{
    return box < maxBoxes() && slot < 30
        ? PKX::getPKM<PB8>(data.get() + boxOffset(box, slot), PK8::PARTY_LENGTH) : emptyPkm();
}

std::unique_ptr<PKX> SavBDSP::emptyPkm() const { return PKX::getPKM<PB8>(nullptr, PK8::PARTY_LENGTH); }
void SavBDSP::finishEditing()
{
    const auto hash = saveHash({data.get(), length});
    std::ranges::copy(hash, data.get() + HashOffset);
}

void SavBDSP::TID(u16 value) { LittleEndian::convertFrom<u16>(data.get() + Trainer + 0x1C, value); }
void SavBDSP::SID(u16 value) { LittleEndian::convertFrom<u16>(data.get() + Trainer + 0x1E, value); }
void SavBDSP::gender(Gender value) { data[Trainer + 0x24] = value == Gender::Male; }
void SavBDSP::language(Language value)
{
    LittleEndian::convertFrom<u32>(data.get() + ConfigStart + 4, u8(value));
}
void SavBDSP::otName(const std::string_view& name) { StringUtils::setString(data.get(), name, Trainer, 13); }
void SavBDSP::money(u32 value)
{
    LittleEndian::convertFrom<u32>(data.get() + Trainer + 0x20, std::min(value, 999999u));
}
void SavBDSP::currentBox(u8 box) { data[Layout + 0x61E] = std::min<int>(box, maxBoxes() - 1); }
void SavBDSP::unlockedBoxes(u8 count) { data[Layout + 0x61D] = std::clamp<int>(count, 1, maxBoxes()); }
void SavBDSP::partyCount(u8 count) { data[PartyStart + 6 * PK8::PARTY_LENGTH] = std::min<u8>(count, 6); }
void SavBDSP::boxWallpaper(u8 box, u8 wallpaper)
{
    if (box < maxBoxes() && wallpaper < 32)
        data[Layout + 0x620 + box] = wallpaper + 1;
}
void SavBDSP::boxName(u8 box, const std::string_view& name)
{
    if (box < maxBoxes())
        StringUtils::setString(data.get(), name, Layout + box * 0x22, 17);
}
void SavBDSP::pkm(const PKX& value, u8 slot)
{
    if (slot < 6)
        writeEntity(value, data.get() + partyOffset(slot));
}
void SavBDSP::pkm(const PKX& value, u8 box, u8 slot, bool)
{
    if (box < maxBoxes() && slot < 30)
        writeEntity(value, data.get() + boxOffset(box, slot));
}

std::unique_ptr<Item> SavBDSP::item(Pouch pouch, u16 slot) const
{
    const auto ids = itemsFor(pouch);
    if (slot >= ids.size())
        return nullptr;
    const u16 id = ids[slot];
    const auto offset = Bag + id * BagRecordSize;
    const auto order = LittleEndian::convertTo<u16>(data.get() + offset + 12);
    const auto quantity = order ? LittleEndian::convertTo<u32>(data.get() + offset) : 0;
    return std::make_unique<BDSPItem>(id, std::min<u32>(quantity, maxCount(pouch)), maxCount(pouch));
}

void SavBDSP::item(const Item& record, Pouch pouch, u16 slot)
{
    const auto ids = itemsFor(pouch);
    if (slot >= ids.size() || record.id() != ids[slot])
        return;
    auto* dest = data.get() + Bag + record.id() * BagRecordSize;
    const u32 count = std::min(record.count(), maxCount(pouch));
    if (!count)
    {
        std::fill_n(dest, BagRecordSize, 0);
        return;
    }
    LittleEndian::convertFrom<u32>(dest, count);
    if (LittleEndian::convertTo<u16>(dest + 12) == 0)
    {
        u16 next = 0;
        for (const auto id : ids)
            next = std::max(next, LittleEndian::convertTo<u16>(data.get() + Bag + id * BagRecordSize + 12));
        LittleEndian::convertFrom<u16>(dest + 12, next == 0xFFFF ? 0xFFFF : next + 1);
    }
}

SmallVector<std::pair<Sav::Pouch, int>, 15> SavBDSP::pouches() const
{
    SmallVector<std::pair<Pouch, int>, 15> result;
    for (const auto& [pouch, ids] : Tables)
        result.push_back({pouch, static_cast<int>(ids.size())});
    return result;
}

SmallVector<std::pair<Sav::Pouch, std::span<const int>>, 15> SavBDSP::validItems() const
{
    static const auto permitted = []
    {
        std::array<std::vector<int>, Tables.size()> result;
        for (size_t i = 0; i < result.size(); ++i)
            for (const auto id : Tables[i].second)
                if (std::ranges::find(bdsp_data::Unreleased, id) == bdsp_data::Unreleased.end())
                    result[i].push_back(id);
        return result;
    }();
    SmallVector<PouchTable, 15> result;
    for (size_t i = 0; i < permitted.size(); ++i)
        result.push_back({Tables[i].first, permitted[i]});
    return result;
}

const std::set<int>& SavBDSP::availableItems() const
{
    static const auto items = []
    {
        std::set<int> result{0};
        for (const auto& [pouch, ids] : Tables)
            if (pouch != Pouch::KeyItem && pouch != Pouch::TM)
                for (const auto id : ids)
                    if (std::ranges::find(bdsp_data::Unreleased, id) == bdsp_data::Unreleased.end() &&
                        std::ranges::find(bdsp_data::DisallowHeldTreasure, id) == bdsp_data::DisallowHeldTreasure.end())
                        result.insert(id);
        return result;
    }();
    return items;
}

const std::set<Move>& SavBDSP::availableMoves() const
{
    static const auto moves = []
    {
        std::set<Move> result;
        for (unsigned id = 0; id <= 826; ++id)
            if (!(bdsp_data::UnusableMoves[id / 8] & (1 << (id % 8))))
                result.insert(Move(id));
        return result;
    }();
    return moves;
}

const std::set<Species>& SavBDSP::availableSpecies() const
{
    static const auto species = []
    {
        std::set<Species> result;
        for (unsigned id = 1; id <= SpeciesCount; ++id)
            result.insert(Species(id));
        return result;
    }();
    return species;
}

const std::set<Ability>& SavBDSP::availableAbilities() const
{
    static const auto abilities = []
    {
        auto entity = PKX::getPKM<PB8>(nullptr, PB8::BOX_LENGTH);
        std::set<Ability> result;
        for (unsigned id = 1; id <= SpeciesCount; ++id)
        {
            entity->species(Species(id));
            for (unsigned form = 0; form < PB8::formCount(entity->species()); ++form)
            {
                entity->alternativeForm(form);
                for (unsigned slot = 0; slot < 3; ++slot)
                    result.insert(entity->abilities(slot));
            }
        }
        return result;
    }();
    return abilities;
}

const std::set<Ball>& SavBDSP::availableBalls() const
{
    return VersionTables::availableBalls(GameVersion::SH);
}

u8 SavBDSP::formCount(Species species) const { return PB8::formCount(species); }

void SavBDSP::dex(const PKX& pokemon)
{
    const auto id = u16(pokemon.species());
    if (!id || id > SpeciesCount || pokemon.egg())
        return;
    const auto index = (id - 1) * 4;
    auto* dex = data.get() + Pokedex;
    const auto before = LittleEndian::convertTo<u32>(dex + index);
    LittleEndian::convertFrom<u32>(dex + index, 3);
    const bool shiny = pokemon.shiny();
    const auto gender = pokemon.gender();
    const unsigned category = shiny ? 0 : 2;
    if (gender != Gender::Female)
        LittleEndian::convertFrom<u32>(dex + index + (category + 1) * SpeciesCount * 4, 1);
    if (gender != Gender::Male)
        LittleEndian::convertFrom<u32>(dex + index + (category + 2) * SpeciesCount * 4, 1);

    // The form flags alternate normal and shiny arrays in this species order.
    struct FormEntry { Species species; int count; };
    constexpr std::array<FormEntry, 13> formEntries{{
        {Species::Unown, 28}, {Species::Castform, 4}, {Species::Deoxys, 4},
        {Species::Burmy, 3}, {Species::Wormadam, 3}, {Species::Mothim, 3},
        {Species::Cherrim, 2}, {Species::Shellos, 2}, {Species::Gastrodon, 2},
        {Species::Rotom, 6}, {Species::Giratina, 2}, {Species::Shaymin, 2},
        {Species::Arceus, 18}}};
    size_t offset = 5 * SpeciesCount * 4;
    for (const auto [species, count] : formEntries)
    {
        const auto form = pokemon.alternativeForm();
        if (species == pokemon.species() && form < count)
            LittleEndian::convertFrom<u32>(dex + offset + (form + (shiny ? count : 0)) * 4, 1);
        offset += count * 8;
    }
    const int lang = static_cast<int>(pokemon.language());
    if (lang >= 1 && lang <= 10 && lang != 6)
    {
        const auto flags = LittleEndian::convertTo<u32>(dex + offset + index);
        LittleEndian::convertFrom<u32>(dex + offset + index, flags | (1u << (lang - (lang > 6 ? 2 : 1))));
    }
    if (pokemon.species() == Species::Spinda)
    {
        auto* pattern = data.get() + 0xCEA34 + (4 * ((u8(gender) & 1) + (shiny ? 2 : 0)));
        if (before < 2)
            LittleEndian::convertFrom<u32>(pattern, pokemon.encryptionConstant());
        if (before < 3)
            LittleEndian::convertFrom<u32>(pattern + 0x10, pokemon.encryptionConstant());
    }
}

int SavBDSP::dexSeen() const
{
    int total = 0;
    for (size_t i = 0; i < SpeciesCount; ++i)
        total += LittleEndian::convertTo<u32>(data.get() + Pokedex + i * 4) >= 2;
    return total;
}

int SavBDSP::dexCaught() const
{
    int total = 0;
    for (size_t i = 0; i < SpeciesCount; ++i)
        total += LittleEndian::convertTo<u32>(data.get() + Pokedex + i * 4) == 3;
    return total;
}

}
