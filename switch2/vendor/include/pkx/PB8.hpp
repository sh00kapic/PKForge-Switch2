// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pkx/PK8.hpp"

namespace pksm
{
class PB8 final : public PK8
{
public:
    using PK8::PK8;
    std::string_view extension() const override { return ".pb8"; }
    std::unique_ptr<PKX> clone() const override;
    std::unique_ptr<PKX> convertToG9(Sav&) const override { return nullptr; }
    Nature nature() const override;
    void nature(Nature value) override;
    u8 level() const override;
    void level(u8 value) override;
    u16 formSpecies() const override;
    u8 baseHP() const override { return personal(0); }
    u8 baseAtk() const override { return personal(1); }
    u8 baseDef() const override { return personal(2); }
    u8 baseSpe() const override { return personal(3); }
    u8 baseSpa() const override { return personal(4); }
    u8 baseSpd() const override { return personal(5); }
    Type type1() const override { return Type(personal(6)); }
    Type type2() const override { return Type(personal(7)); }
    u8 genderType() const override { return personal(0x12); }
    u8 baseFriendship() const override { return personal(0x14); }
    u8 expType() const override { return personal(0x15); }
    Ability abilities(u8 index) const override;
    u16 formStatIndex() const override;
    static u8 formCount(Species species);

private:
    u8 personal(size_t offset) const;
    u16 statImpl(Stat stat) const override;
};
}
