// SPDX-License-Identifier: GPL-3.0-or-later
#include "pkx/PB8.hpp"
#include "PKSMCORE_CONFIG.h"
#include "utils/endian.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace
{
constexpr size_t PersonalSize = 0x44;

const u8* entry(size_t index)
{
    // Table format and data follow PKHeX PersonalInfo8BDSP (GPL-3.0).
    static const auto bytes = []
    {
        struct Close { void operator()(FILE* f) const { std::fclose(f); } };
        const std::unique_ptr<FILE, Close> file(std::fopen(_PKSMCORE_PERSONAL_FOLDER "personal8b", "rb"));
        if (!file || std::fseek(file.get(), 0, SEEK_END) != 0)
            throw std::runtime_error("Cannot open the BDSP personal table.");
        const auto size = std::ftell(file.get());
        if (size < 494 * PersonalSize || size % PersonalSize != 0)
            throw std::runtime_error("Invalid BDSP personal table.");
        std::rewind(file.get());
        std::vector<u8> table(size);
        if (std::fread(table.data(), 1, table.size(), file.get()) != table.size())
            throw std::runtime_error("Could not read the BDSP personal table.");
        return table;
    }();
    return bytes.data() + (index < bytes.size() / PersonalSize ? index * PersonalSize : 0);
}
}

namespace pksm
{
std::unique_ptr<PKX> PB8::clone() const
{
    auto bytes = rawData();
    return PKX::getPKM<PB8>(const_cast<u8*>(bytes.data()), bytes.size());
}

Nature PB8::nature() const { return Nature(rawData()[0x20]); }

void PB8::nature(Nature value)
{
    rawData()[0x20] = rawData()[0x21] = u8(value);
}

u8 PB8::level() const
{
    const auto cached = partyLevel();
    if (experience() == 0 && cached > 0 && cached <= 100)
        return cached;
    return PK8::level();
}

void PB8::level(u8 value)
{
    PK8::level(std::clamp<u8>(value, 1, 100));
    partyLevel(value);
}

u8 PB8::formCount(Species species) { return std::max<u8>(1, entry(u16(species))[0x20]); }

u16 PB8::formStatIndex() const { return LittleEndian::convertTo<u16>(entry(u16(species())) + 0x1E); }

u16 PB8::formSpecies() const
{
    const auto first = formStatIndex();
    const auto form = alternativeForm();
    return first && form && form < formCount(species()) ? first + form - 1 : u16(species());
}

u8 PB8::personal(size_t offset) const { return entry(formSpecies())[offset]; }

Ability PB8::abilities(u8 index) const
{
    return Ability(LittleEndian::convertTo<u16>(entry(formSpecies()) + 0x18 + 2 * std::min<u8>(2, index)));
}

u16 PB8::statImpl(Stat stat) const
{
    const auto index = u8(stat);
    const auto lv = level();
    const auto effectiveIV = hyperTrain(stat) ? 31 : iv(stat);
    const int scaled = (2 * personal(index) + effectiveIV + ev(stat) / 4) * lv / 100;
    if (stat == Stat::HP)
        return scaled + lv + 10;
    const int mintedNature = rawData()[0x21];
    int multiplier = 10;
    if (mintedNature < 25)
    {
        if (mintedNature / 5 + 1 == index) ++multiplier;
        if (mintedNature % 5 + 1 == index) --multiplier;
    }
    return (scaled + 5) * multiplier / 10;
}
}
