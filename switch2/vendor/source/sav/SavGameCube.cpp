// SPDX-License-Identifier: GPL-3.0-or-later
// GameCube save layouts and integrity checks follow PKHeX's GPL-3.0 implementation.
#include "sav/SavGameCube.hpp"
#include "pkx/PK3.hpp"
#include "utils/ValueConverter.hpp"
#include "utils/crypto.hpp"
#include "utils/endian.hpp"
#include "utils/i18n.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stdexcept>

namespace pksm
{
namespace
{
constexpr size_t Header = 0x40;
constexpr size_t SlotStart = 0x6000;
constexpr size_t Sector = 0x2000;
constexpr size_t Group = 23;
constexpr size_t SectorPayload = 0x1FF0;
constexpr unsigned Empty = 0xFFFFFFFF;

template<typename T>
T read(std::span<const u8> data, size_t offset)
{
    return BigEndian::convertTo<T>(data.data() + offset);
}

std::string gcName(std::span<const u8> data, size_t offset, size_t characters)
{
    std::u16string name;
    for (size_t i = 0; i < characters; ++i)
    {
        const auto ch = read<u16>(data, offset + i * 2);
        if (ch == 0 || ch == 0xFFFF)
            break;
        name.push_back(ch);
    }
    return StringUtils::convert<std::string>(name);
}

bool decryptColosseum(std::span<u8> slot)
{
    auto stored = slot.last<20>();
    std::array<u8, 20> digest{};
    std::ranges::transform(stored, digest.begin(), [](u8 value) -> u8 { return ~value; });
    for (size_t i = 0x18; i + 20 <= slot.size() - 20; i += 20)
    {
        auto block = slot.subspan(i, 20);
        const auto next = crypto::sha1(block);
        for (size_t j = 0; j < 20; ++j)
            block[j] ^= digest[j];
        digest = next;
    }

    const auto checksum = read<u32>(slot, 0x0C);
    BigEndian::convertFrom<u32>(slot.data() + 0x0C, 0);
    const auto hash = crypto::sha1(slot.first(slot.size() - 40));
    u32 computed = 0;
    for (size_t i = 0; i < 0x18; i += 4)
        computed -= read<u32>(slot, i);
    computed -= read<u32>(slot, 0x18) ^ ~read<u32>(hash, 0);
    computed -= read<u32>(slot, 0x1C) ^ ~read<u32>(hash, 4);
    return checksum == computed && std::ranges::equal(hash, stored);
}

size_t xdOffset(std::span<const u8> slot, size_t index)
{
    return 0xA8 + (read<u16>(slot, 0x40 + 4 * index) |
        (size_t(read<u16>(slot, 0x42 + 4 * index)) << 16));
}

bool decryptXD(std::span<u8> slot)
{
    std::array<u16, 4> keys{};
    for (size_t i = 0; i < 4; ++i)
        keys[i] = read<u16>(slot, 8 + i * 2);
    for (size_t offset = 16; offset < 0x27FD8; offset += 8)
    {
        for (size_t j = 0; j < 4; ++j)
            BigEndian::convertFrom<u16>(slot.data() + offset + j * 2,
                read<u16>(slot, offset + j * 2) - keys[j]);
        const std::array ticks{0x43, 0x29, 0x17, 0x13};
        const auto prior = keys;
        for (size_t word = 0; word < 4; ++word)
        {
            keys[word] = 0;
            for (size_t nibble = 0; nibble < 4; ++nibble)
                keys[word] |= ((prior[nibble] + ticks[nibble]) >> (word * 4) & 0xF) << (nibble * 4);
        }
    }

    const auto headerChecksum = xdOffset(slot, 0) + 0x38;
    if (headerChecksum + 4 > slot.size() || read<u32>(slot, headerChecksum) !=
        std::accumulate(slot.begin(), slot.begin() + 8, 0u))
        return false;
    std::array<u16, 8> stored{};
    for (size_t i = 0; i < stored.size(); ++i)
        stored[i] = read<u16>(slot, 0x10 + i * 2);
    std::fill_n(slot.data() + 0x10, 16, 0);
    for (size_t i = 0; i < 4; ++i)
    {
        u32 sum = 0;
        for (size_t offset = 8 + i * 0x9FF4; offset < 8 + (i + 1) * 0x9FF4; offset += 2)
            sum += read<u16>(slot, offset);
        if (stored[7 - 2 * i] != (sum >> 16) || stored[6 - 2 * i] != u16(sum))
            return false;
    }
    return true;
}

std::vector<u8> boxGroup(std::span<const u8> body, size_t group)
{
    std::vector<u8> output(Group * SectorPayload);
    std::array<bool, Group> used{};
    for (size_t i = 0; i < Group; ++i)
    {
        const auto block = body.subspan((1 + group * Group + i) * Sector, Sector);
        const auto index = read<u32>(block, 4);
        if (index >= Group || used[index])
            return {};
        u16 sum = 0;
        for (size_t offset = 4; offset < 0x1FFC; offset += 2)
            sum += read<u16>(block, offset);
        const u32 expected = (u32(sum) << 16) | u16(0xF004 - sum);
        if (read<u32>(block, 0) != expected)
            return {};
        used[index] = true;
        std::ranges::copy(block.subspan(12, SectorPayload), output.begin() + index * SectorPayload);
    }
    return output;
}
}

std::unique_ptr<Sav> SavGameCube::open(const std::shared_ptr<u8[]>& data, size_t size)
{
    if (size < Header || std::memcmp(data.get() + 4, "01", 2) != 0)
        return {};
    const std::string_view family(reinterpret_cast<const char*>(data.get()), 3);
    if (family == "GC6" && size == Header + 0x60000)
        return std::make_unique<SavGameCube>(data, size, Format::Colosseum);
    if (family == "GXX" && size == Header + 0x56000)
        return std::make_unique<SavGameCube>(data, size, Format::XD);
    if (family == "GPX" && size == Header + 0x76000)
        return std::make_unique<SavGameCube>(data, size, Format::Box);
    return {};
}

SavGameCube::SavGameCube(const std::shared_ptr<u8[]>& bytes, size_t size, Format kind)
    : Sav(bytes, size), format(kind)
{
    const std::span<const u8> body(bytes.get() + Header, size - Header);
    if (format == Format::Box)
    {
        const auto counter = [&](size_t group)
        {
            u32 maximum = 0;
            for (size_t i = 0; i < Group; ++i)
                maximum = std::max(maximum, read<u32>(body, (1 + group * Group + i) * Sector + 8));
            return maximum;
        };
        const size_t latest = counter(1) > counter(0);
        storage = boxGroup(body, latest);
        if (storage.empty())
            storage = boxGroup(body, 1 - latest);
        if (storage.empty())
            throw std::runtime_error("Pokemon Box storage checksum is invalid.");
        recordSize = 84;
        return;
    }

    const bool xd = format == Format::XD;
    const size_t slotSize = xd ? 0x28000 : 0x1E000;
    std::array<int, 3> order{0, 1, 2};
    auto slots = std::span(order).first(xd ? 2 : 3);
    std::ranges::sort(slots, std::greater{}, [&](int index)
        { return read<u32>(body, SlotStart + index * slotSize + 4); });
    for (const int index : slots)
    {
        const auto encrypted = body.subspan(SlotStart + index * slotSize, slotSize);
        storage.assign(encrypted.begin(), encrypted.end());
        if (xd ? decryptXD(storage) : decryptColosseum(storage))
            break;
        storage.clear();
    }
    if (storage.empty())
        throw std::runtime_error("No valid GameCube save slot was found.");

    recordSize = xd ? 196 : 312;
    trainer = xd ? xdOffset(storage, 1) : 0x78;
    partyStart = xd ? trainer + 0x30 : 0xA8;
    boxStart = xd ? xdOffset(storage, 2) : 0xB90;
    if (trainer + (xd ? 0x8E8 : 0xA88) > storage.size() ||
        partyStart + 6 * recordSize > storage.size() ||
        boxStart + maxBoxes() * (20 + 30 * recordSize) > storage.size())
        throw std::runtime_error("GameCube save offsets are outside the save slot.");
    const size_t config = xd ? xdOffset(storage, 0) : 0x08;
    const bool jp = bytes[3] == 'J';
    const auto timeOffset = config + (xd && !jp ? 0x30 : 0x20);
    if (timeOffset + 8 <= storage.size())
    {
        const double seconds = xd && !jp ? read<double>(storage, timeOffset) : read<float>(storage, timeOffset);
        if (std::isfinite(seconds) && seconds > 0 && seconds < 65536.0 * 3600)
            playSeconds = seconds;
    }
    for (unsigned i = 0; i < 6; ++i)
        if (read<u16>(storage, partyStart + i * recordSize))
            partySlots.push_back(i);
}

std::string_view SavGameCube::title() const
{
    switch (format)
    {
        case Format::XD: return "XD: Gale of Darkness";
        case Format::Box: return "Pokemon Box: Ruby & Sapphire";
        case Format::Colosseum: return "Pokemon Colosseum";
    }
    return {};
}

Language SavGameCube::language() const { return data[3] == 'J' ? Language::JPN : Language::ENG; }

std::string SavGameCube::otName() const
{
    return format == Format::Box ? std::string{} : gcName(storage, trainer, 10);
}

u16 SavGameCube::TID() const
{
    return format == Format::Box ? 0 : read<u16>(storage, trainer + 0x2E);
}

u16 SavGameCube::SID() const
{
    return format == Format::Box ? 0 : read<u16>(storage, trainer + 0x2C);
}

Gender SavGameCube::gender() const
{
    if (format == Format::Box)
        return Gender::Genderless;
    return Gender(storage[format == Format::XD ? trainer + 0x8E0 : 0xAF8] & 1);
}

u32 SavGameCube::money() const
{
    return format == Format::Box ? 0 : read<u32>(storage, format == Format::XD ? trainer + 0x8E4 : 0xAFC);
}

int SavGameCube::maxBoxes() const
{
    return format == Format::Box ? 50 : format == Format::XD ? 8 : 3;
}

u32 SavGameCube::boxOffset(u8 box, u8 slot) const
{
    if (box >= maxBoxes() || slot >= 30)
        return Empty;
    if (format != Format::Box)
        return boxStart + (20 + 30 * recordSize) * box + 20 + recordSize * slot;
    const int swizzled = (slot / 6) * 12 + (box % 2) * 6 + slot % 6;
    return 8 + recordSize * ((box & ~1) * 30 + swizzled);
}

u32 SavGameCube::partyOffset(u8 slot) const
{
    return slot < partySlots.size() ? partyStart + recordSize * partySlots[slot] : Empty;
}

std::unique_ptr<PKX> SavGameCube::pkm(u8 slot) const { return pokemonAt(partyOffset(slot)); }
std::unique_ptr<PKX> SavGameCube::pkm(u8 box, u8 slot) const { return pokemonAt(boxOffset(box, slot)); }

std::unique_ptr<PKX> SavGameCube::emptyPkm() const
{
    return PKX::getPKM<PK3>(nullptr, PK3::BOX_LENGTH);
}

std::string SavGameCube::boxName(u8 box) const
{
    if (format != Format::Box && box < maxBoxes())
    {
        const auto name = gcName(storage, boxStart + (20 + 30 * recordSize) * box, 8);
        if (!name.empty())
            return name;
    }
    return "Box " + std::to_string(box + 1);
}

std::unique_ptr<PKX> SavGameCube::pokemonAt(size_t offset) const
{
    if (offset >= storage.size() || storage.size() - offset < recordSize)
        return emptyPkm();
    if (format == Format::Box)
    {
        auto raw = const_cast<u8*>(storage.data() + offset);
        auto pk = PKX::getPKM<PK3>(raw, PK3::BOX_LENGTH);
        const auto checksum = pk->checksum();
        pk->refreshChecksum();
        return pk->species() <= Species::Deoxys && checksum == pk->checksum() ? std::move(pk) : emptyPkm();
    }

    const bool xd = format == Format::XD;
    const auto record = std::span(storage).subspan(offset, recordSize);
    const auto species = SpeciesConverter::g3ToNational(read<u16>(record, 0));
    if (species == Species::None || species > Species::Deoxys)
        return emptyPkm();
    auto pk = PKX::getPKM<PK3>(nullptr, PK3::PARTY_LENGTH);
    pk->species(species);
    pk->version(GameVersion::CXD);
    pk->language(language());
    pk->PID(read<u32>(record, xd ? 0x28 : 4));
    pk->SID(read<u16>(record, xd ? 0x24 : 0x14));
    pk->TID(read<u16>(record, xd ? 0x26 : 0x16));
    pk->otName(gcName(record, xd ? 0x38 : 0x18, 11));
    pk->nickname(gcName(record, xd ? 0x64 : 0x44, 11));
    pk->experience(read<u32>(record, xd ? 0x20 : 0x5C));
    pk->otFriendship(record[xd ? 7 : 0xB1]);
    static_cast<PK3&>(*pk).heldItem3(read<u16>(record, xd ? 2 : 0x88));
    for (int i = 0; i < 4; ++i)
    {
        const auto moveOffset = (xd ? 0x80 : 0x78) + i * 4;
        pk->move(i, Move(read<u16>(record, moveOffset)));
        pk->PP(i, record[moveOffset + 2]);
    }
    pk->updatePartyData();
    pk->refreshChecksum();
    return pk;
}
}
