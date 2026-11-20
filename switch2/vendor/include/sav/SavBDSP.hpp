// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sav/Sav.hpp"
#include "wcx/WCX.hpp"

namespace pksm
{
class SavBDSP final : public Sav
{
public:
    using Sav::Sav;
    static bool recognized(const std::shared_ptr<u8[]>& bytes, size_t length);

    void finishEditing() override;
    void beginEditing() override {}
    bool checksumsValid() const override;
    u16 TID() const override;
    void TID(u16) override;
    u16 SID() const override;
    void SID(u16) override;
    GameVersion version() const override;
    void version(GameVersion) override {}
    Gender gender() const override;
    void gender(Gender) override;
    u8 subRegion() const override { return 0; }
    void subRegion(u8) override {}
    u8 country() const override { return 0; }
    void country(u8) override {}
    u8 consoleRegion() const override { return 0; }
    void consoleRegion(u8) override {}
    Language language() const override;
    void language(Language) override;
    std::string otName() const override;
    void otName(const std::string_view&) override;
    u32 money() const override;
    void money(u32) override;
    u32 BP() const override { return 0; }
    void BP(u32) override {}
    u8 badges() const override;
    u16 playedHours() const override;
    void playedHours(u16 value) override;
    u8 playedMinutes() const override;
    void playedMinutes(u8 value) override;
    u8 playedSeconds() const override;
    void playedSeconds(u8 value) override;
    u8 currentBox() const override;
    void currentBox(u8) override;
    u8 unlockedBoxes() const override;
    void unlockedBoxes(u8) override;
    u8 legendBoxUnlockSize() const override { return 0; }
    u32 boxOffset(u8 box, u8 slot) const override;
    u32 partyOffset(u8 slot) const override;
    std::unique_ptr<PKX> pkm(u8 slot) const override;
    void pkm(const PKX&, u8) override;
    std::unique_ptr<PKX> pkm(u8 box, u8 slot) const override;
    void pkm(const PKX&, u8, u8, bool) override;
    void trade(PKX&, const Date&) const override {}
    std::unique_ptr<PKX> emptyPkm() const override;
    void dex(const PKX&) override;
    int dexSeen() const override;
    int dexCaught() const override;
    int currentGiftAmount() const override { return 0; }
    std::unique_ptr<WCX> mysteryGift(int) const override { return {}; }
    void mysteryGift(const WCX&, int&) override {}
    void cryptBoxData(bool) override {}
    std::string boxName(u8 box) const override;
    void boxName(u8, const std::string_view&) override;
    u8 boxWallpaper(u8 box) const override;
    void boxWallpaper(u8, u8) override;
    u8 partyCount() const override;
    void partyCount(u8) override;
    int maxBoxes() const override { return 40; }
    size_t maxWondercards() const override { return 0; }
    Generation generation() const override { return Generation::EIGHT; }
    void item(const Item&, Pouch, u16) override;
    std::unique_ptr<Item> item(Pouch, u16) const override;
    SmallVector<std::pair<Pouch, int>, 15> pouches() const override;
    SmallVector<std::pair<Pouch, std::span<const int>>, 15> validItems() const override;
    bool pouchIndexedByItem(Pouch) const override { return true; }
    const std::set<int>& availableItems() const override;
    const std::set<Move>& availableMoves() const override;
    const std::set<Species>& availableSpecies() const override;
    const std::set<Ability>& availableAbilities() const override;
    const std::set<Ball>& availableBalls() const override;
    u8 formCount(Species species) const override;
};
}
