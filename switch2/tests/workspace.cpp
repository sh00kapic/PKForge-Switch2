// SPDX-License-Identifier: GPL-3.0-or-later
#include "workspace.hpp"
#include "editors.hpp"
#include "wcx/PGT.hpp"
#include "gift_album.hpp"
#include "library.hpp"
#include "qrcodegen.h"
#include "ui.hpp"
#include "sav/SavDP.hpp"
#include "pkx/PK1.hpp"
#include "pkx/PK2.hpp"
#include "pkx/PK3.hpp"
#include "pkx/PK4.hpp"
#include "pkx/PK5.hpp"
#include "pkx/PK6.hpp"
#include "pkx/PK7.hpp"
#include "pkx/PK8.hpp"
#include "pkx/PK9.hpp"
#include "pkx/PA8.hpp"
#include "pkx/PA9.hpp"
#include "pkx/PB7.hpp"
#include "pkx/PB8.hpp"
#include "sav/SavSV.hpp"
#include "utils/crypto.hpp"
#include "wcx/WC9.hpp"
#include "utils/endian.hpp"
#include "wcx/WCX.hpp"

#include <array>
#include <bit>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

constexpr std::array<u8, 3> BcdMoney{0x12, 0x34, 0x56};
static_assert(BigEndian::BCDtoUInteger<u32, 3>(BcdMoney.data()) == 123456);
static_assert(BigEndian::UIntegerToBCD<u32, 3>(123456) == BcdMoney);
static_assert(LittleEndian::BCDtoUInteger<u32, 3>(BcdMoney.data()) == 563412);

int main()
{
    namespace fs = std::filesystem;
    const auto originalDirectory = fs::current_path();
    const auto sandbox = fs::absolute("build/host-test/storage");
    fs::remove_all(sandbox);
    fs::create_directories(sandbox / "sdmc:");
    fs::create_directory_symlink(originalDirectory / "romfs", sandbox / "romfs");
    fs::current_path(sandbox);
    forge::prepareStorage();
    forge::prepareExtras();
    assert(forge::listFiles(forge::Saves, false).empty());

    // Synthetic Ruby/Sapphire-format SRAM with a complete active sector set.
    std::array<u8, 0x20000> sram{};
    for (u16 sector = 0; sector < 14; ++sector)
    {
        auto* footer = &sram[sector * 0x1000 + 0xff4];
        LittleEndian::convertFrom<u16>(footer, sector);
        LittleEndian::convertFrom<u32>(footer + 4, 0x08012025);
        LittleEndian::convertFrom<u32>(footer + 8, 1);
    }

    const auto path = std::string(forge::Saves) + "/fixture.sav";
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char*>(sram.data()), sram.size());
    const auto files = forge::listFiles(forge::Saves, false, true);
    assert(files.size() == 1);
    auto save = forge::openSave(files.front());
    assert(save.writable);
    assert(save.data->money() == 0);
    assert(save.data->generation() == pksm::Generation::THREE);

    using Shiny = forge::Field::Kind;
    const auto testShiny = [&]<typename Record>(pksm::Species species = pksm::Species::Pikachu)
    {
        auto mon = pksm::PKX::getPKM<Record>(nullptr, Record::BOX_LENGTH);
        mon->species(species);
        mon->version(pksm::GameVersion::None);
        mon->TID(0x1234);
        mon->SID(0x4321);
        mon->PID(0x7a234523);
        const auto gen = mon->generation();
        if (species == pksm::Species::Unown)
        {
            const u16 low = mon->PID();
            mon->PID((u32(mon->TID() ^ mon->SID() ^ low) << 16) | low);
            mon->shiny(false);
        }
        const auto nature = mon->nature();
        const auto form = mon->alternativeForm();
        const auto gender = mon->gender();
        const auto ability = mon->abilityNumber();
        const auto encryption = mon->encryptionConstant();
        const auto fields = forge::pokemonFields(*mon);
        const auto field = std::ranges::find(fields, Shiny::Shiny, &forge::Field::kind);
        assert(field != fields.end());
        assert(!mon->shiny());
        forge::adjustField(*save.data, *mon, *field, 1);
        assert(mon->shiny());
        if (gen >= pksm::Generation::THREE)
        {
            assert(mon->nature() == nature && mon->alternativeForm() == form &&
                mon->gender() == gender && mon->abilityNumber() == ability);
            if (gen >= pksm::Generation::SIX)
                assert(mon->encryptionConstant() == encryption);
        }
        forge::adjustField(*save.data, *mon, *field, -1);
        assert(!mon->shiny());
    };
    testShiny.operator()<pksm::PK1>();
    testShiny.operator()<pksm::PK2>();
    testShiny.operator()<pksm::PK3>();
    testShiny.operator()<pksm::PK3>(pksm::Species::Unown);
    testShiny.operator()<pksm::PK4>();
    testShiny.operator()<pksm::PK5>();
    testShiny.operator()<pksm::PK6>();
    testShiny.operator()<pksm::PK7>();
    testShiny.operator()<pksm::PB7>();
    testShiny.operator()<pksm::PK8>();
    testShiny.operator()<pksm::PB8>();
    testShiny.operator()<pksm::PA8>();
    testShiny.operator()<pksm::PK9>();
    testShiny.operator()<pksm::PA9>();
    std::puts("Shiny editor toggles passed across all 13 editable record formats.");

    auto pokemon = pksm::PKX::getPKM<pksm::PK3>(nullptr, pksm::PK3::BOX_LENGTH);
    pokemon->species(pksm::Species::Pikachu);
    pokemon->nickname("Pikachu");
    pokemon->ability(pksm::Ability::Static);
    pokemon->ball(pksm::Ball::Poke);
    pokemon->otFriendship(90);
    pokemon->refreshChecksum();
    forge::deposit(*pokemon);

    const auto records = forge::listFiles(forge::Bank, true, true);
    assert(records.size() == 1);
    auto banked = forge::bankPokemon(records.front());
    assert(banked->species() == pksm::Species::Pikachu);
    forge::putPokemon(save, 0, 0, *banked);
    assert(save.dirty);
    try
    {
        forge::putPokemon(save, 0, 0, *banked);
        assert(false && "An occupied box slot accepted a paste");
    }
    catch (const std::runtime_error&) {}

    save.data->money(123400);
    const auto exported = forge::exportSave(save);
    auto reopened = forge::openSave({"fixture.sav", exported});
    assert(reopened.writable);
    assert(reopened.data->money() == 123400);
    assert(reopened.data->pkm(0, 0)->species() == pksm::Species::Pikachu);
    assert(reopened.data->pkm(0, 0)->otFriendship() == 90);
    save.data->money(123500);
    assert(forge::exportSave(save) != exported);
    assert(forge::openSave(files.front()).data->money() == 0);

    auto corrupt = sram;
    corrupt[0xff6] ^= 1;
    const auto broken = std::string(forge::Saves) + "/broken.sav";
    std::ofstream(broken, std::ios::binary)
        .write(reinterpret_cast<const char*>(corrupt.data()), corrupt.size());
    assert(!forge::openSave({"broken.sav", broken}).writable);
    using Pouch = pksm::Sav::Pouch;
    auto edited = reopened.data->pkm(0, 0);
    forge::adjustField(*reopened.data, *edited, {forge::Field::Kind::Level, "Level"}, 5);
    forge::adjustField(*reopened.data, *edited, {forge::Field::Kind::IV, "HP IV", 0}, 12);
    assert(edited->level() == 6 && edited->iv(pksm::Stat::HP) == 12);
    reopened.data->pkm(*edited, 0, 0, false);
    const auto collection = forge::livingDex(reopened.data.get(), records, false);
    assert(collection[static_cast<unsigned>(pksm::Species::Pikachu)] == 2);
    const auto plan = forge::planDexBox(reopened, 25, false);
    assert(plan.box == 1 && plan.moves.size() == 1);
    forge::applyDexBox(reopened, plan);
    assert(reopened.data->pkm(0, 0)->species() == pksm::Species::None);
    assert(reopened.data->pkm(1, 24)->level() == 6);

    forge::storeItem({17, 9});
    const auto items = forge::loadItems();
    assert(items.size() == 1 && items[0].count == 9);
    forge::copyItemToSave(reopened, items[0]);
    assert(forge::bagSlot(*reopened.data, Pouch::NormalItem, 0).count == 9);
    forge::changeBagCount(reopened, Pouch::NormalItem, 0, 10);
    const auto inventoryExport = forge::exportSave(reopened);
    assert(forge::bagSlot(*forge::openSave({"bag", inventoryExport}).data,
        Pouch::NormalItem, 0).count == 10);

    forge::moveCopyToPark(*edited);
    auto residents = forge::parkResidents();
    assert(residents.size() == 1);
    forge::logParkActivity(residents[0], 2, 1, 18);
    residents = forge::parkResidents();
    assert(residents[0].feeds == 2 && residents[0].plays == 1 && residents[0].steps == 18);

    // Create a checksummed Diamond/Pearl fixture for a cross-generation transfer and event queue.
    const auto ds = std::make_shared<u8[]>(0x80000);
    LittleEndian::convertFrom<u32>(ds.get() + 0xc0f4, 0xc100);
    LittleEndian::convertFrom<u32>(ds.get() + 0xc0f8, 0x20060623);
    pksm::SavDP synthetic(ds);
    synthetic.language(pksm::Language::ENG);
    synthetic.version(pksm::GameVersion::D);
    synthetic.finishEditing();
    const std::string dsPath = std::string(forge::Saves) + "/fixture-ds.sav";
    std::ofstream(dsPath, std::ios::binary).write(reinterpret_cast<const char*>(ds.get()), 0x80000);
    auto target = forge::openSave({"fixture-ds.sav", dsPath});
    assert(target.writable && target.data->generation() == pksm::Generation::FOUR);
    auto converted = forge::previewTransfer(target, 0, 0, *banked);
    assert(converted->generation() == pksm::Generation::FOUR);
    forge::putPokemon(target, 0, 0, *converted);

    forge::Gift testCard{4,"Test gift",".pgt",std::vector<u8>(pksm::PGT::length)};
    testCard.data[0]=3;
    testCard.data[4]=1;
    assert(forge::canQueueGift(target,testCard));
    forge::injectGift(target,testCard);
    const auto dsExport = forge::exportSave(target);
    auto dsEdited = forge::openSave({"edited-ds", dsExport});
    assert(dsEdited.data->currentGiftAmount() == 1);
    assert(dsEdited.data->pkm(0, 0)->species() == pksm::Species::Pikachu);
    forge::GiftAlbum album;
    album.load("sdmc:/switch/PKForge/media/events/gen5",5,target);
    album.filter("","ENG");
    assert(album.groups.empty() && album.archive.empty());

    forge::ui::Assets assets("romfs/ui");
    forge::ui::Canvas canvas(assets);
    assert(canvas.portrait({25}, 220, 240, 128));
    const auto normal = canvas.pixels();
    canvas.background();
    assert(canvas.portrait({25, 0, 0, true}, 220, 240, 128));
    const auto shiny = canvas.pixels();
    assert(normal != shiny);
    canvas.background();
    assert(canvas.portrait({6, 1}, 220, 240, 128) && canvas.pixels() != shiny);
    canvas.background();
    assert(canvas.portrait({25, 1, 0, false, true, true}, 220, 240, 128));
    canvas.background();
    assert(canvas.portrait({869, 8, 1}, 220, 240, 128));
    assert(!canvas.animates());
    canvas.background();
    assert(canvas.portrait({869, 8, 6}, 220, 240, 128));
    assert(!canvas.portrait({25, 256}, 410, 240, 128));
    assert(canvas.portrait({}, 410, 240, 128));
    assert(!canvas.animates());
    auto alcremie = pksm::PKX::getPKM<pksm::PK9>(nullptr, pksm::PK9::BOX_LENGTH);
    alcremie->species(pksm::Species::Alcremie);
    alcremie->version(pksm::GameVersion::VL);
    alcremie->alternativeForm(8);
    alcremie->formArgument(1);
    const auto sweetField = forge::Field{forge::Field::Kind::Decoration, "Sweet decoration"};
    forge::adjustField(*save.data, *alcremie, sweetField, 5);
    assert(alcremie->alternativeForm() == 8 && alcremie->rawData()[0xd0] == 6);
    assert(forge::fieldValue(*alcremie, sweetField) == "Ribbon Sweet");
    auto swordAlcremie = pksm::PKX::getPKM<pksm::PK8>(nullptr, pksm::PK8::BOX_LENGTH);
    swordAlcremie->formArgument(3);
    assert(swordAlcremie->formArgument() == 3 && swordAlcremie->rawData()[0xe4] == 3);

    const auto portraits = fs::path(forge::Portraits);
    fs::copy_file(originalDirectory / "tests/fixtures/three-frames.webp", portraits / "990-0.webp");
    std::ofstream(portraits / "990-0-f.webp") << "incomplete";
    std::ofstream(portraits / "991-0.webp") << "incomplete";
    forge::ui::Assets sdAssets("romfs/ui", {}, forge::Portraits);
    forge::ui::Canvas sdCanvas(sdAssets);
    sdCanvas.background();
    const auto center = 240 * forge::ui::Width + 220;
    // A female falls back to the same palette's unisex file.
    assert(sdCanvas.portrait({990, 0, 0, false, true}, 220, 240, 128));
    assert(sdCanvas.animates() && sdCanvas.pixels()[center] == forge::ui::rgb(0xff0000));
    auto frame = sdCanvas.pixels();
    sdCanvas.animatePortrait(frame.data(), forge::ui::Width, 0.05f);
    assert(frame[center] == forge::ui::rgb(0x0000ff));
    sdCanvas.animatePortrait(frame.data(), forge::ui::Width, 0.10f);
    assert(frame[center] == forge::ui::rgb(0x0000ff));
    sdCanvas.animatePortrait(frame.data(), forge::ui::Width, 0.13f);
    assert(frame[center] == forge::ui::rgb(0x00ff00));
    sdCanvas.animatePortrait(frame.data(), forge::ui::Width, 0.18f);
    assert(frame[center] == forge::ui::rgb(0xff0000));
    assert(frame[center - 50 * forge::ui::Width] == sdCanvas.pixels()[center - 50 * forge::ui::Width]);
    assert(sdCanvas.portrait({990, 0, 0, true}, 220, 240, 128) && !sdCanvas.animates());
    assert(sdCanvas.portrait({991}, 220, 240, 128) && !sdCanvas.animates());

    // A two-joint test mesh with one independently moving segment.
    const auto write = [](std::ofstream& file, const auto& data)
    {
        file.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(data[0]));
    };
    {
        std::ofstream model(std::string(forge::Models) + "/25-0.pfm", std::ios::binary);
        write(model, std::array{'P', 'F', 'M', '1'});
        write(model, std::array<u16, 6>{4, 2, 2, 2, 0, 4});
        for (int vertex = 0; vertex < 4; ++vertex)
        {
            write(model, std::array{vertex & 1 ? 0.55f : -0.55f, 0.0f, vertex & 2 ? 0.45f : -0.45f});
            write(model, std::array<u8, 8>{u8(vertex & 1), 0, 0, 0, 255, 0, 0, 0});
            write(model, std::array<u16, 2>{});
        }
        write(model, std::array<u16, 10>{0, 1, 2, 65535, 0xf800, 1, 3, 2, 65535, 0x07e0});
        constexpr std::array<float, 12> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        for (int i = 0; i < 3; ++i)
            write(model, identity);
        auto joint = identity;
        joint[11] = 0.55f;
        write(model, joint);
    }
    fs::copy_file(portraits / "990-0.webp", portraits / "25-0.webp");
    forge::ui::Assets modelAssets("romfs/ui", forge::Models, forge::Portraits);
    forge::ui::Canvas modelCanvas(modelAssets);
    modelCanvas.background();
    assert(modelCanvas.portrait({25}, 220, 240, 128) && modelCanvas.animationStepMs() == 34);
    auto posed = modelCanvas.pixels();
    modelCanvas.animatePortrait(posed.data(), forge::ui::Width, 0.25f);
    assert(posed != modelCanvas.pixels() && posed.back() == modelCanvas.pixels().back());
    std::ofstream(std::string(forge::Models) + "/926-0.pfm", std::ios::binary) << "bad model";
    modelCanvas.background();
    assert(modelCanvas.portrait({926}, 220, 240, 128) && modelCanvas.animationStepMs() == 52);
    // A real Scarlet/Violet-style SC block container, signed with the normal save algorithm.
    const auto sv = std::make_shared<u8[]>(pksm::SavSV::SIZE_G9SV_0);
    size_t cursor = 0;
    const auto putBlock = [&](u32 key, const std::vector<u8>& payload)
    {
        LittleEndian::convertFrom<u32>(sv.get() + cursor, key);
        cursor += 4;
        std::vector<u8> block(5 + payload.size());
        block[0] = 4; // Object type.
        LittleEndian::convertFrom<u32>(block.data() + 1, payload.size());
        std::ranges::copy(payload, block.begin() + 5);
        const auto advance = [](u32 value)
        {
            value ^= value << 2;
            value ^= value >> 15;
            return value ^ (value << 13);
        };
        auto seed = key;
        for (int i = 0; i < std::popcount(key); ++i)
            seed = advance(seed);
        for (size_t i = 0; i < block.size(); ++i)
        {
            sv[cursor++] = block[i] ^ static_cast<u8>(seed >> (8 * (i % 4)));
            if (i % 4 == 3)
                seed = advance(seed);
        }
    };

    constexpr pksm::Species clodsire = pksm::Species::Clodsire;
    constexpr pksm::Species fidough = pksm::Species::Fidough;
    const auto partyMember = [&](pksm::Species species, u16 storedSpecies, int level)
    {
        auto pk = pksm::PKX::getPKM<pksm::PK9>(nullptr, pksm::PK9::PARTY_LENGTH);
        pk->species(species);
        assert(LittleEndian::convertTo<u16>(pk->rawData().data() + 8) == storedSpecies);
        pk->version(pksm::GameVersion::VL);
        pk->language(pksm::Language::ENG);
        pk->nickname(species.localize(pksm::Language::ENG));
        pk->setAbility(0);
        pk->level(level);
        pk->updatePartyData();
        const auto hp = pk->partyStat(pksm::Stat::HP);
        assert(hp > 5);
        pk->partyCurrHP(hp - 5);
        pk->refreshChecksum();
        pk->encrypt();
        return pk;
    };
    auto clod = partyMember(clodsire, 1009, 24);
    auto dough = partyMember(fidough, 970, 19);
    std::vector<u8> partyData(6 * pksm::PK9::PARTY_LENGTH + 1);
    std::ranges::copy(clod->rawData(), partyData.begin());
    std::ranges::copy(dough->rawData(), partyData.begin() + pksm::PK9::PARTY_LENGTH);
    partyData.back() = 2;
    std::vector<u8> trainer(64);
    trainer[4] = static_cast<u8>(pksm::GameVersion::VL);
    trainer[7] = static_cast<u8>(pksm::Language::ENG);
    putBlock(0x0D66012C, std::vector<u8>(32 * 30 * pksm::PK9::PARTY_LENGTH));
    putBlock(0x19722C89, std::vector<u8>(32 * 0x22));
    putBlock(0x21C9BD44, std::vector<u8>(3000 * 16));
    putBlock(0x3AA1A9AD, partyData);
    putBlock(0x4F35D0DD, std::vector<u8>(4));
    putBlock(0xE3E89BD1, trainer);
    putBlock(0xF0000000, std::vector<u8>(pksm::SavSV::SIZE_G9SV_0 - cursor - 32 - 9));
    assert(cursor + 32 == pksm::SavSV::SIZE_G9SV_0);
    pksm::crypto::swsh::applyXor(sv, pksm::SavSV::SIZE_G9SV_0);
    pksm::crypto::swsh::sign(sv, pksm::SavSV::SIZE_G9SV_0);
    const std::string svPath = std::string(forge::Saves) + "/fixture-violet.bin";
    std::ofstream(svPath, std::ios::binary)
        .write(reinterpret_cast<const char*>(sv.get()), pksm::SavSV::SIZE_G9SV_0);
    auto violet = forge::openSave({"fixture-violet.bin", svPath});
    assert(violet.data->pkm(0)->species() == clodsire);
    assert(violet.data->pkm(1)->species() == fidough);
    auto draft = violet.data->pkm(0);
    draft->level(31);
    forge::editPartyMember(violet, 0, *draft, true);

    forge::Gift gift{9, "Synthetic Clodsire event", ".wc9", std::vector<u8>(pksm::WC9::length)};
    gift.data[0x11] = 1;
    gift.data[0x1C] = static_cast<u8>(pksm::GameVersion::VL);
    LittleEndian::convertFrom<u16>(gift.data.data() + 0x238, 1009);
    gift.data[0x23C] = 35;
    gift.data[0x268] = 12;
    forge::injectGift(violet, gift);
    assert(violet.data->pkm(0, 0)->species() == clodsire);
    assert(violet.data->pkm(0, 0)->level() == 35);
    assert(violet.data->pkm(0, 0)->partyLevel() == 35);
    assert(violet.data->pkm(0, 0)->iv(pksm::Stat::HP) == 12);

    auto catalogViolet = forge::openSave({"fixture-violet.bin", svPath});
    forge::GiftAlbum violetGifts;
    violetGifts.load("romfs/events/gen9", 9, catalogViolet);
    for (const auto [id, species] : {std::pair{1540u, 1007u}, std::pair{1540u, 1008u}, std::pair{9034u, 721u}})
    {
        auto card = std::ranges::find_if(violetGifts.cards, [&](const auto& entry)
        { return entry.number == id && entry.species == species; });
        assert(card != violetGifts.cards.end());
        auto& raw = violetGifts.archive[card->archive];
        const auto* reason = forge::giftQueueBlocker(catalogViolet, raw);
        if (species == 1007)
        {
            if (reason) std::fprintf(stderr, "Koraidon gift was blocked: %s\n", reason);
            assert(card->fits && !reason);
            forge::injectGift(catalogViolet, raw);
            const auto received = catalogViolet.data->pkm(0, 0);
            assert(received->species() == pksm::Species::Koraidon && received->shiny());
        }
        else
        {
            assert(!card->fits && reason);
            assert(std::string_view(reason).contains(species == 721 ? "Legends: Z-A" : "different game version"));
        }
    }


    const auto potionPouch = Pouch::Medicine;
    assert(std::ranges::binary_search(forge::bagCatalog(*violet.data, potionPouch), 17u));
    forge::addBagItem(violet, potionPouch, 17, 3);
    const auto filled = forge::maxBag(violet, potionPouch, true);
    assert(filled.stacks > 1);
    violet.data->otName("JULIANA");
    violet.data->TID(12345);
    violet.data->SID(789);
    const auto svCopy = forge::exportSave(violet);
    auto reopenedSV = forge::openSave({"reopened-violet.bin", svCopy});
    const auto firstMon = reopenedSV.data->pkm(0);
    assert(firstMon->species() == clodsire && firstMon->level() == 31 && firstMon->partyLevel() == 31);
    assert(firstMon->partyStat(pksm::Stat::HP) - firstMon->partyCurrHP() == 5);
    assert(reopenedSV.data->pkm(1)->species() == fidough);
    assert(reopenedSV.data->pkm(0, 0)->level() == 35);
    assert(reopenedSV.data->otName() == "JULIANA" && reopenedSV.data->TID() == 12345 &&
        reopenedSV.data->SID() == 789);
    bool maxPotion = false;
    for (const auto [pouch, capacity] : reopenedSV.data->pouches())
        if (pouch == potionPouch)
            for (int pos = 0; pos < capacity; ++pos)
            {
                const auto item = forge::bagSlot(*reopenedSV.data, pouch, pos);
                if (item.id == 17)
                    maxPotion = item.count && item.count == item.limit;
            }
    assert(maxPotion);

    // Brilliant Diamond 1.3 save layout with an independently calculated MD5.
    std::vector<u8> bd(0xEF0A4);
    LittleEndian::convertFrom<u32>(bd.data(), 0x34);
    constexpr std::array<u8, 8> dawn{'D', 0, 'A', 0, 'W', 0, 'N', 0};
    std::ranges::copy(dawn, bd.begin() + 0x79BB4);
    bd[0x79BB4 + 0x24] = 1;
    LittleEndian::convertFrom<u16>(bd.data() + 0x14098 + 8, 393);
    bd[0x14098 + 0x148] = 19;
    bd[0x14098 + 6 * 0x158] = 1;
    constexpr std::array<u8, 16> digest{0x9f, 0x0f, 0x48, 0x7c, 0x20, 0xbc, 0x7c, 0xe4,
        0xb3, 0xce, 0x92, 0xa5, 0x4a, 0x93, 0xae, 0xfb};
    std::ranges::copy(digest, bd.begin() + 0xE9818);
    const std::string bdPath = std::string(forge::Saves) + "/fixture-bd.bin";
    std::ofstream(bdPath, std::ios::binary).write(reinterpret_cast<const char*>(bd.data()), bd.size());
    auto diamond = forge::openSave({"fixture-bd.bin", bdPath});
    assert(diamond.writable && diamond.data->checksumsValid() &&
        diamond.data->version() == pksm::GameVersion::BD);
    assert(diamond.data->otName() == "DAWN" && diamond.data->partyCount() == 1);
    assert(diamond.data->pkm(0)->species() == pksm::Species::Piplup && diamond.data->pkm(0)->level() == 19);
    auto piplup = diamond.data->pkm(0);
    piplup->version(pksm::GameVersion::BD);
    piplup->language(pksm::Language::ENG);
    piplup->nickname("Pip");
    piplup->nicknamed(true);
    piplup->level(28);
    piplup->setAbility(0);
    piplup->ball(pksm::Ball::Poke);
    forge::editPartyMember(diamond, 0, *piplup, true);
    forge::putPokemon(diamond, 0, 0, *piplup);
    forge::addBagItem(diamond, Pouch::Medicine, 17, 5);
    diamond.data->money(4200);
    diamond.data->playedHours(42);
    diamond.data->playedMinutes(18);
    diamond.data->playedSeconds(35);
    diamond.data->otName("LUCAS");
    diamond.data->boxName(0, "SINNOH");
    diamond.data->boxWallpaper(0, 4);
    const auto diamondCopy = forge::exportSave(diamond);
    auto bdEdited = forge::openSave({"diamond-copy.bin", diamondCopy});
    assert(bdEdited.writable && bdEdited.data->money() == 4200 && bdEdited.data->otName() == "LUCAS");
    assert(bdEdited.data->playedHours() == 42 && bdEdited.data->playedMinutes() == 18 &&
        bdEdited.data->playedSeconds() == 35);
    assert(bdEdited.data->pkm(0)->level() == 28 && bdEdited.data->pkm(0, 0)->nickname() == "Pip");
    assert(bdEdited.data->boxName(0) == "SINNOH" && bdEdited.data->boxWallpaper(0) == 4);
    int potionSlot = -1;
    for (const auto [pouch, capacity] : bdEdited.data->pouches())
        if (pouch == Pouch::Medicine)
            for (int i = 0; i < capacity; ++i)
                if (const auto item = forge::bagSlot(*bdEdited.data, pouch, i); item.id == 17)
                {
                    assert(item.count == 5);
                    potionSlot = i;
                }
    assert(potionSlot >= 0);
    forge::removeBagStack(bdEdited, Pouch::Medicine, potionSlot);
    assert(forge::bagSlot(*bdEdited.data, Pouch::Medicine, potionSlot).count == 0);
    const std::array bdMarked{0};
    assert(forge::organizeBox(bdEdited, 0, bdMarked, forge::BoxAction::Duplicate) == 1);
    assert(bdEdited.data->pkm(0, 1)->species() == pksm::Species::Piplup);
    assert(forge::organizeBox(bdEdited, 0, bdMarked, forge::BoxAction::CopyToBank) == 1);
    const auto pb8 = forge::listFiles(forge::Bank, true, true);
    const auto bankedPB8 = std::ranges::find_if(pb8, [](const auto& entry) { return entry.path.ends_with(".pb8"); });
    assert(bankedPB8 != pb8.end() && forge::bankPokemon(*bankedPB8)->level() == 28);

    const auto point = forge::snapshot(bdEdited, "Before party edits");
    bdEdited.dirty = false;
    bdEdited.data->money(4300);
    bdEdited.data->otName("BARRY");
    const auto named = forge::openSave(point);
    const auto diff = forge::compareSaves(bdEdited, named);
    assert(diff.party == 0 && diff.boxes == 0 && diff.money == 100 && diff.trainer);
    const auto restore = forge::restoreSnapshot(bdEdited, point);
    assert(restore != diamondCopy && bdEdited.data->otName() == "LUCAS" && bdEdited.data->money() == 4200);
    assert(forge::openSave({"unchanged-diamond", bdPath}).data->otName() == "DAWN");
    assert(forge::boxSlotAssignedToTeam(*bdEdited.data, 0, 0));
    const auto boxCountBefore = bdEdited.data->pkm(0, 0)->species();
    try
    {
        forge::organizeBox(bdEdited, 0, bdMarked, forge::BoxAction::Release);
        assert(false && "A battle team member was released");
    }
    catch (const std::runtime_error&) {}
    assert(bdEdited.data->pkm(0, 0)->species() == boxCountBefore && !bdEdited.dirty);
    constexpr size_t nurseryOffset = 0x96080;
    auto encryptedParent = bdEdited.data->pkm(0);
    encryptedParent->refreshChecksum();
    encryptedParent->encrypt();
    auto* const nurseryBytes = bdEdited.data->rawData().get() + nurseryOffset;
    std::ranges::copy(encryptedParent->rawData(), nurseryBytes);
    assert(forge::nursery(bdEdited).parents[0]->level() == 28);
    nurseryBytes[2 * pksm::PK8::PARTY_LENGTH] = 1;
    assert(forge::nursery(bdEdited).eggReady);
    try
    {
        forge::withdrawNursery(bdEdited, 0, 0, 2);
        assert(false && "A parent could be withdrawn while an egg was waiting");
    }
    catch (const std::runtime_error&) {}
    nurseryBytes[2 * pksm::PK8::PARTY_LENGTH] = 0;
    forge::withdrawNursery(bdEdited, 0, 0, 2);
    assert(forge::nursery(bdEdited).parents[0]->species() == pksm::Species::None);
    const auto nurseryCopy = forge::exportSave(bdEdited);
    auto nurseryCheck = forge::openSave({"nursery", nurseryCopy});
    assert(nurseryCheck.data->pkm(0, 2)->species() == pksm::Species::Piplup);

    auto crossTarget = forge::openSave(files.front());
    const forge::File crossDonor{"source.sav", exported};
    auto* hofBytes = crossTarget.data->rawData().get();
    constexpr auto record2 = 0x1D000 + 120;
    LittleEndian::convertFrom<u16>(hofBytes + 0x1C000 + 8, (44 << 9) | 25);
    LittleEndian::convertFrom<u16>(hofBytes + record2, (58 << 9) | 6);
    const auto hall = forge::hallOfFame(crossTarget);
    assert(hall.size() == 2 && hall[0].members[0].species == pksm::Species::Pikachu &&
        hall[0].members[0].level == 44 && hall[1].members[0].species == pksm::Species::Charizard);
    const auto crossPlan = forge::planCrossDex(crossTarget, crossDonor, 25, false);
    assert(crossPlan.targetBox == 0 && crossPlan.moves.size() == 1);
    const auto pair = forge::applyCrossDex(crossTarget, crossPlan);
    auto crossSrc = forge::openSave({"paired-source", pair + "/source/fixture.sav"});
    assert(crossSrc.data->pkm(0, 0)->species() == pksm::Species::None);
    assert(crossTarget.data->pkm(0, 24)->species() == pksm::Species::Pikachu);
    assert(forge::openSave(crossDonor).data->pkm(0, 0)->species() == pksm::Species::Pikachu);

    const std::string text = "Pikachu @ Light Ball\nAbility: Static\nLevel: 50\nTimid Nature\n"
        "EVs: 252 SpA / 252 Spe / 4 HP\n- Thunderbolt\n\n";
    auto showMon = forge::previewShowdown(reopenedSV, text);
    assert(showMon->species() == pksm::Species::Pikachu && showMon->level() == 50 &&
        showMon->ev(pksm::Stat::SPATK) == 252 && showMon->move(0) == pksm::Move::Thunderbolt);
    forge::adjustField(*reopenedSV.data, *showMon, {forge::Field::Kind::EV, "HP EV", 0}, 10);
    assert(showMon->ev(pksm::Stat::HP) == 6);
    forge::adjustField(*reopenedSV.data, *showMon, {forge::Field::Kind::EV, "HP EV", 0}, 10, true);
    assert(showMon->ev(pksm::Stat::HP) == 16);
    const auto formatted = forge::showdownSet(*showMon);
    std::array<uint8_t, qrcodegen_BUFFER_LEN_FOR_VERSION(10)> qr{}, qrTemp{};
    assert(qrcodegen_encodeText(formatted.c_str(), qrTemp.data(), qr.data(), qrcodegen_Ecc_LOW,
        1, 10, qrcodegen_Mask_AUTO, true));
    assert(qrcodegen_getSize(qr.data()) > 21);
    assert(fs::is_regular_file(forge::exportShowdown(formatted)));

    const std::string teamText = "\r\nPikachu\r\nLevel: 25\r\n\r\n"
        "Rotom-Wash\nAbility: Levitate\nLevel: 50\nTera Type: Electric\n- Hydro Pump\n";
    auto team = forge::previewShowdownTeam(reopenedSV, teamText, 0, 1);
    assert(team.size() == 2 && team[0].slot == 1 && team[1].slot == 2);
    assert(team[1].pokemon->species() == pksm::Species::Rotom && team[1].pokemon->alternativeForm() != 0);
    assert(forge::showdownSet(*team[1].pokemon).starts_with("Rotom-Wash"));
    forge::applyShowdownTeam(reopenedSV, team);
    assert(reopenedSV.data->pkm(0, 1)->species() == pksm::Species::Pikachu);
    assert(reopenedSV.data->pkm(0, 2)->species() == pksm::Species::Rotom);

    forge::Gift giftItems{9, "Synthetic Violet bundle", ".wc9", std::vector<u8>(pksm::WC9::length)};
    giftItems.data[0x0E] = 1;
    giftItems.data[0x11] = 2;
    LittleEndian::convertFrom<u16>(giftItems.data.data() + 0x18, 4);
    LittleEndian::convertFrom<u16>(giftItems.data.data() + 0x1A, 7);
    assert(!forge::canQueueGift(reopenedSV, giftItems));
    giftItems.data[0x0E] = 2;
    assert(forge::canQueueGift(reopenedSV, giftItems));
    forge::injectGift(reopenedSV, giftItems);
    bool gotBalls = false;
    for (const auto [pouch, capacity] : reopenedSV.data->pouches())
        if (pouch == Pouch::Ball)
            for (int i = 0; i < capacity; ++i)
                if (const auto entry = forge::bagSlot(*reopenedSV.data, pouch, i); entry.id == 4)
                    gotBalls = entry.count == 7;
    assert(gotBalls);

    canvas.background();
    assert(canvas.portrait({25}, 300, 300, 180) && !canvas.animates());
    auto firstFrame = canvas.pixels(), secondFrame = canvas.pixels();
    canvas.animatePortrait(firstFrame.data(), forge::ui::Width, 0.0f);
    canvas.animatePortrait(secondFrame.data(), forge::ui::Width, 0.6f);
    assert(firstFrame == secondFrame);
    assert(firstFrame.front() == secondFrame.front() && firstFrame.back() == secondFrame.back());
    canvas.background();
    assert(!canvas.animates());
    auto zaMon = pksm::PKX::getPKM<pksm::PA9>(nullptr, pksm::PA9::BOX_LENGTH);
    zaMon->species(pksm::Species::Charmander);
    zaMon->version(pksm::GameVersion::ZA);
    zaMon->level(25);
    zaMon->PID(0x12345678);
    zaMon->shiny(true);
    assert(zaMon->shiny());
    zaMon->shiny(false);
    assert(!zaMon->shiny());
    zaMon->hyperTrain(pksm::Stat::SPD, true);
    assert((zaMon->rawData()[0x126] & 0x20) && !zaMon->hyperTrain(pksm::Stat::SPATK));
    auto zaParty = zaMon->partyClone();
    assert(zaParty->partyLevel() == 25 && zaParty->partyStat(pksm::Stat::HP) > 25);

    // A preset reads from the same-game inventory and restores only listed quantities.
    forge::addBagItem(nurseryCheck, Pouch::Medicine, 17, 4);
    const auto presetFile = forge::storeBagPreset(nurseryCheck, "Test field kit");
    auto preset = forge::readBagPreset(presetFile, *nurseryCheck.data);
    const auto potions = std::ranges::find_if(preset, [](const auto& entry)
        { return entry.pouch == Pouch::Medicine && entry.item == 17; });
    assert(potions != preset.end() && potions->count == 4);
    forge::addBagItem(nurseryCheck, Pouch::Medicine, 17, 3);
    forge::applyBagPreset(nurseryCheck, preset);
    assert(forge::bagSlot(*nurseryCheck.data, Pouch::Medicine, potionSlot).count == 4);
    try
    {
        forge::readBagPreset(presetFile, *reopenedSV.data);
        assert(false && "A different game accepted the preset");
    }
    catch (const std::runtime_error&) {}

    const auto gbaPath = std::string(forge::Saves) + "/Pokemon Ruby 64 KiB.sav";
    std::ofstream(gbaPath, std::ios::binary).write(reinterpret_cast<const char*>(sram.data()), 0x10000);
    auto ruby = forge::openSave({"Pokemon Ruby 64 KiB.sav", gbaPath});
    assert(ruby.writable && forge::gameTitle(ruby.file, *ruby.data) == "Ruby");
    const forge::ClockFields clock{10, 7, 3, 25, 734, 8, 23, 55};
    forge::applyGbaClock(ruby, clock);
    assert(forge::gbaClock(ruby) == clock);
    const auto rubyCopy = forge::exportSave(ruby);
    assert(fs::file_size(rubyCopy) == 0x10000);
    assert(forge::gbaClock(forge::openSave({"ruby-copy", rubyCopy})) == clock);

    // Synthetic Pokémon Box memory card export with one checksummed group.
    std::vector<u8> card(0x76040);
    std::copy_n("GPXE01", 6, card.begin());
    auto entity = banked->clone();
    entity->refreshChecksum();
    entity->encrypt();
    std::ranges::copy(entity->rawData(), card.begin() + 0x40 + 0x2000 + 12 + 8);
    for (unsigned sector = 0; sector < 23; ++sector)
    {
        const size_t off = 0x40 + (sector + 1) * 0x2000;
        BigEndian::convertFrom<u32>(card.data() + off + 4, sector);
        BigEndian::convertFrom<u32>(card.data() + off + 8, 1);
        u16 sum = 0;
        for (size_t word = 4; word < 0x1FFC; word += 2)
            sum += BigEndian::convertTo<u16>(card.data() + off + word);
        BigEndian::convertFrom<u32>(card.data() + off, (u32(sum) << 16) | u16(0xF004 - sum));
    }
    const auto gamecube = std::string(forge::Saves) + "/PokemonBox.gci";
    std::ofstream(gamecube, std::ios::binary).write(reinterpret_cast<const char*>(card.data()), card.size());
    const auto archive = forge::openSave({"PokemonBox.gci", gamecube});
    assert(!archive.writable && forge::canDeposit(*archive.data));
    assert(archive.data->pkm(0, 0)->species() == pksm::Species::Pikachu);
    assert(forge::gameArtwork(archive.file, *archive.data) == 55 && !canvas.art(forge::ui::Art::Game, 55, 200, 200, 128));

    forge::storeTrainerProfile("Hoenn", "first");
    forge::storeTrainerProfile("Hoenn", "replacement");
    auto profiles=forge::trainerProfiles();
    assert(profiles.size()==1 && profiles.front().second=="replacement");
    forge::deleteTrainerProfile("Hoenn");
    assert(forge::trainerProfiles().empty());
    forge::writeEditorPreferences({true,true,false});
    const auto preferences=forge::readEditorPreferences();
    assert(preferences.hax && preferences.hardcore && !preferences.trainerOwnership);

    auto bankMoveSave=forge::openSave({"fixture.sav",exported});
    const auto beforeBank=forge::listFiles(forge::Bank,true,true);
    forge::moveToBank(bankMoveSave,0,0);
    assert(bankMoveSave.data->pkm(0,0)->species()==pksm::Species::None && !bankMoveSave.dirty);
    auto afterBank=forge::listFiles(forge::Bank,true,true);
    std::erase_if(afterBank,[&](const auto& entry){
        return std::ranges::find(beforeBank,entry.path,&forge::File::path)!=beforeBank.end();
    });
    assert(afterBank.size()==1);
    auto moved=forge::bankPokemon(afterBank.front());
    forge::moveFromBank(bankMoveSave,afterBank.front(),0,0,*moved);
    assert(!fs::exists(afterBank.front().path));
    auto afterMove=forge::openSave(bankMoveSave.file);
    assert(afterMove.writable && afterMove.data->pkm(0,0)->species()==moved->species());

    const auto park = forge::parkResidents();
    assert(!park.empty());
    const auto history = forge::parkJournal(park.front());
    assert(!history.empty() && history.front().when > 0);
    std::puts("Writable BDSP, Nursery, box actions, snapshots, cross-save exports, Showdown QR, gift bundles, park journal, and portrait motion passed.");
    const bool bundledCovers = fs::exists("romfs/ui/games.art");
    assert(canvas.art(forge::ui::Art::Game, static_cast<unsigned>(pksm::GameVersion::VL), 200, 200, 128) == bundledCovers);
    assert(canvas.art(forge::ui::Art::Game, static_cast<unsigned>(pksm::GameVersion::BD), 200, 200, 128) == bundledCovers);
    assert(!canvas.art(forge::ui::Art::Chrome, 5 + 16, 200, 200, 24, false));
    assert(canvas.art(forge::ui::Art::Chrome, 5 + 16, 200, 200, 24));
    std::puts("Violet species, event and party levels, trainer, bag refill, BDSP and game logos passed.");

    std::puts("Offline number badges, local WebP playback, Alcremie sweets and joint rendering passed.");
    std::puts("Editor, cross-generation transfer, Living Dex plan, inventory, park and Wonder Card tests passed.");
    std::puts("Save, Bank, paste, export, and checksum checks passed.");
}
