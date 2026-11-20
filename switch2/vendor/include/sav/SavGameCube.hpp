// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sav/Sav.hpp"
#include "wcx/WCX.hpp"

namespace pksm
{
class SavGameCube final : public Sav
{
    enum class Format { Colosseum, XD, Box };
    Format format;
    std::vector<u8> storage;
    std::vector<unsigned> partySlots;
    size_t trainer = 0, partyStart = 0, boxStart = 0;
    size_t recordSize = 0;
    unsigned playSeconds = 0;
    std::unique_ptr<PKX> pokemonAt(size_t offset) const;

public:
    static std::unique_ptr<Sav> open(const std::shared_ptr<u8[]>& data, size_t size);
    SavGameCube(const std::shared_ptr<u8[]>& data, size_t size, Format format);
    std::string_view title() const;
    unsigned artwork() const { return 53 + static_cast<unsigned>(format); }
    bool canDeposit() const { return format == Format::Box; }

    void finishEditing() override {}
    void beginEditing() override {}
    bool checksumsValid() const override { return true; }
    u16 TID() const override;
    void TID(u16) override {}
    u16 SID() const override;
    void SID(u16) override {}
    GameVersion version() const override { return GameVersion::CXD; }
    void version(GameVersion) override {}
    Gender gender() const override;
    void gender(Gender) override {}
    u8 subRegion() const override { return 0; }
    void subRegion(u8) override {}
    u8 country() const override { return 0; }
    void country(u8) override {}
    u8 consoleRegion() const override { return 0; }
    void consoleRegion(u8) override {}
    Language language() const override;
    void language(Language) override {}
    std::string otName() const override;
    void otName(const std::string_view&) override {}
    u32 money() const override;
    void money(u32) override {}
    u32 BP() const override { return 0; }
    void BP(u32) override {}
    u8 badges() const override { return 0; }
    u16 playedHours() const override { return playSeconds / 3600; }
    void playedHours(u16) override {}
    u8 playedMinutes() const override { return playSeconds / 60 % 60; }
    void playedMinutes(u8) override {}
    u8 playedSeconds() const override { return playSeconds % 60; }
    void playedSeconds(u8) override {}
    u8 currentBox() const override { return 0; }
    void currentBox(u8) override {}
    u8 unlockedBoxes() const override { return maxBoxes(); }
    void unlockedBoxes(u8) override {}
    u8 legendBoxUnlockSize() const override { return 0; }
    u32 boxOffset(u8 box, u8 slot) const override;
    u32 partyOffset(u8 slot) const override;
    std::unique_ptr<PKX> pkm(u8 slot) const override;
    void pkm(const PKX&, u8) override {}
    std::unique_ptr<PKX> pkm(u8 box, u8 slot) const override;
    void pkm(const PKX&, u8, u8, bool) override {}
    void trade(PKX&, const Date&) const override {}
    std::unique_ptr<PKX> emptyPkm() const override;
    void dex(const PKX&) override {}
    int dexSeen() const override { return 0; }
    int dexCaught() const override { return 0; }
    int currentGiftAmount() const override { return 0; }
    std::unique_ptr<WCX> mysteryGift(int) const override { return {}; }
    void mysteryGift(const WCX&, int&) override {}
    void cryptBoxData(bool) override {}
    std::string boxName(u8 box) const override;
    void boxName(u8, const std::string_view&) override {}
    u8 boxWallpaper(u8) const override { return 0; }
    void boxWallpaper(u8, u8) override {}
    u8 partyCount() const override { return partySlots.size(); }
    void partyCount(u8) override {}
    int maxBoxes() const override;
    size_t maxWondercards() const override { return 0; }
    Generation generation() const override { return Generation::THREE; }
    void item(const Item&, Pouch, u16) override {}
    std::unique_ptr<Item> item(Pouch, u16) const override { return {}; }
    SmallVector<std::pair<Pouch, int>, 15> pouches() const override { return {}; }
    SmallVector<std::pair<Pouch, std::span<const int>>, 15> validItems() const override { return {}; }
};
}
