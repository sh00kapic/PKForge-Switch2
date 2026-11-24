// SPDX-License-Identifier: GPL-3.0-or-later
#include <switch.h>

#include "workspace.hpp"
#include "engine_host.hpp"
#include <sstream>
#include "editors.hpp"
#include "library.hpp"
#include "gift_album.hpp"
#include "qrcodegen.h"
#include "ui.hpp"
#include "touch.hpp"
#include "utils/i18n.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <sys/stat.h>

namespace
{
using pksm::Language;
using pksm::PKX;
using namespace forge::ui;

enum class View {
    Files, Box, Party, Detail, Trainer, Bank, Tools, Editor, Bag, Dex, Events, EventDetail, Transfer, ItemBank, Park, Walker, DexPlan, BagAdd, BagMax,
    BoxTools, BoxActions, BoxReview, BoxAudit, Snapshots, SnapshotReview, CrossSource, CrossReview,
    DexRegister, Showdown, ShowdownReview, Modes, Ribbons, ParkJournal, Nursery, NurseryReview, Hall,
    BagPresets, BagPresetReview, GbaClock, BoxSearch, EditorChoice, Engine
};
enum class Confirm { None, Files, Exit };
constexpr u64 SearchButtons = HidNpadButton_ZL | HidNpadButton_ZR;

char latin1Ascii(unsigned char trail)
{
    constexpr std::string_view accents =
        "AAAAAAACEEEEIIII" "DNOOOOO OUUUUYTs"
        "aaaaaaaceeeeiiii" "dnooooo ouuuuyty";
    return trail >= 0x80 && trail <= 0xbf && accents[trail - 0x80] != ' ' ? accents[trail - 0x80] : '\0';
}

class SearchChord
{
    u64 pending = 0;

public:
    u64 update(u64 down, u64 held, bool available)
    {
        const auto triggers = down & SearchButtons;
        if (available && triggers && (held & SearchButtons) == SearchButtons)
        {
            pending = 0;
            return SearchButtons;
        }
        if (!available || (down & ~SearchButtons))
        {
            pending = 0;
            return down;
        }
        u64 single = 0;
        if (pending && !(held & pending))
        {
            single = pending;
            pending = 0;
        }
        if (triggers)
            pending = triggers;
        return single;
    }
};

std::string label(std::string_view text, size_t width)
{
    std::string result;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (result.size() == width)
        {
            if (width >= 3)
                result.replace(width - 3, 3, "...");
            break;
        }
        const unsigned char ch = text[i];
        if (ch >= 0x20 && ch <= 0x7e)
            result.push_back(ch);
        else if (ch == 0xc3 && i + 1 < text.size() && latin1Ascii(text[i + 1]))
            result.push_back(latin1Ascii(text[++i]));
        else if ((ch & 0xc0) != 0x80)
            result.push_back('?');
    }
    return result;
}

std::string speciesName(const PKX& pokemon)
{
    const auto species = pokemon.species();
    const auto& name = species.localize(Language::ENG);
    return name.empty() ? "#" + std::to_string(static_cast<unsigned>(species)) : name;
}

bool occupied(const PKX& pokemon)
{
    return pokemon.species() != pksm::Species::None;
}

int cycle(int current, int delta, int count)
{
    return (current + delta + count) % count;
}

class SearchQuery
{
    std::vector<std::string> terms;

    static std::string normalize(std::string_view text)
    {
        std::string value;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const unsigned char ch = text[i];
            if (std::isalnum(ch))
                value.push_back(static_cast<char>(std::tolower(ch)));
            else if (ch == 0xc3 && i + 1 < text.size())
            {
                if (const auto ascii = latin1Ascii(text[++i]))
                    value.push_back(static_cast<char>(std::tolower(ascii)));
            }
            else if (std::isspace(ch) && !value.empty() && value.back() != ' ')
                value.push_back(' ');
        }
        return value;
    }

public:
    explicit SearchQuery(std::string_view text)
    {
        const auto words = normalize(text);
        for (size_t first = 0; first < words.size(); )
        {
            const size_t end = words.find(' ', first);
            terms.push_back(words.substr(first, end - first));
            if (end == words.npos)
                break;
            first = end + 1;
        }
    }

    bool empty() const { return terms.empty(); }

    bool matches(std::initializer_list<std::string_view> values) const
    {
        if (empty())
            return true;
        std::string searchable;
        for (const auto value : values)
        {
            auto normalized = normalize(value);
            std::erase(normalized, ' ');
            searchable += normalized;
            searchable += '\n';
        }
        return std::ranges::all_of(terms, [&](const auto& term) { return searchable.find(term) != searchable.npos; });
    }
};

struct Browser
{
    std::vector<forge::File> files;
    int index = 0;

    void load(const char* path, bool bank)
    {
        files = forge::listFiles(path, bank, true);
        index = std::clamp(index, 0, std::max(0, static_cast<int>(files.size()) - 1));
    }

    void move(int delta)
    {
        index = std::clamp(index + delta, 0, std::max(0, static_cast<int>(files.size()) - 1));
    }
};

class App
{
    View view = View::Files;
    View detailFrom = View::Box;
    View bankFrom = View::Files;
    Confirm confirm = Confirm::None;
    Browser saves;
    struct ShelfGame { unsigned artwork = 255; std::string title; };
    std::unordered_map<std::string, ShelfGame> gameIcons;
    Browser bank;
    std::unordered_map<View, std::string> searches;
    using SearchKeyboard = std::optional<std::string> (*)(const char*, std::string_view, int, bool);
    SearchKeyboard searchKeyboard;
    bool searchKeyboardOpened = false;
    struct BoxMatch { int box, slot; std::string name, nickname; };
    std::vector<BoxMatch> boxMatches;
    int boxMatch = 0;
    std::array<std::unique_ptr<PKX>, 12> bankCards;
    int bankPage = -1;
    forge::Save save{};
    int box = 0;
    int slot = 0;
    int party = 0;
    unsigned moneyDraft = 0;
    std::array<int, 3> timeDraft{};
    u16 tidDraft = 0, sidDraft = 0;
    pksm::Gender genderDraft = pksm::Gender::Male;
    std::string trainerNameDraft;
    int trainerRow = 0;
    std::array<std::string, 30> names;
    std::array<unsigned, 30> species{};
    std::array<bool, 30> rare{};
    std::unique_ptr<PKX> detail;
    std::unique_ptr<PKX> clipboard;
    std::string status;
    bool exit = false;
    View toolsFrom = View::Files;
    int tool = 0;
    std::unique_ptr<PKX> edit;
    std::unique_ptr<PKX> transfer;
    std::vector<std::pair<std::string, std::string>> transferChanges;
    int transferRow = 0;
    std::vector<forge::Field> fields;
    int editRow = 0;
    std::vector<unsigned> editorChoices;
    std::string editorQuery;
    int editorChoice = 0;
    std::vector<pksm::Ribbon> ribbons;
    int ribbonRow = 0;
    bool editDirty = false;
    bool editedMoves = false;
    bool editedStats = false;
    int bagPouch = 0;
    int bagRow = 0;
    unsigned bagDraft = 0;
    struct BagChoice { int pouch; unsigned id; };
    std::vector<BagChoice> bagOptions;
    int bagOption = 0;
    unsigned bagAddCount = 1;
    int bagMaxMode = 0;
    Browser bagPresets;
    std::vector<forge::BagPresetEntry> bagPresetDraft;
    std::vector<int> bagSlots;
    std::array<unsigned, 1026> dexCounts{};
    std::vector<unsigned> dexEntries;
    int dexRow = 0;
    bool shinyDex = false;
    bool missingDex = false;
    forge::DexPlan dexPlan;
    int planRow = 0;
    int eventGen = 4;
    int eventRow = 0;
    forge::GiftAlbum album;
    int eventCopy=0, eventFilterRow=0;
    bool eventFilters=false;
    std::vector<forge::StoredItem> storedItems;
    int storedRow = 0;
    std::vector<forge::Resident> residents;
    int residentRow = 0;
    unsigned walkSteps = 0;
    int walkX = 404, walkY = 365;
    std::array<bool, 30> marked{};
    int boxToolSlot = 0, actionsRow = 0, wallpaperDraft = 0;
    std::vector<forge::BoxIssue> issues;
    int issuesRow = 0;
    Browser snapshots, donors;
    pksm::GameVersion snapshotGame = pksm::GameVersion::INVALID;
    std::optional<forge::SaveDifference> snapshotDiff;
    forge::CrossDexPlan crossPlan;
    int crossRow = 0;
    std::array<uint8_t, qrcodegen_BUFFER_LEN_FOR_VERSION(25)> qr{};
    std::string showdownText;
    std::vector<forge::ShowdownEntry> showdownDraft;
    int showdownRow = 0;
    int mode = 0, modeRow = 0;
    forge::EditorPreferences preferences;
    std::optional<forge::File> clipboardSource;
    bool bankDepositReview = false;
    void updateMode()
    {
        mode=preferences.hardcore ? 2 : preferences.hax ? 1 : 0;
        if (save.data) save.writable=sourceWritable && !preferences.hardcore;
    }
    std::vector<forge::ParkEvent> journal;
    int journalRow = 0;
    forge::Nursery nurseryView{};
    int parentRow = 0;
    std::vector<forge::HallTeam> hall;
    int hallRow = 0;
    forge::ClockFields clockDraft{};
    int clockRow = 0;
    bool sourceWritable = false;

    struct TouchTarget
    {
        TouchRect rect;
        u64 keys = 0;
        int row = -1;
        bool openControls = false;
        int* selection = nullptr;
        int index = 0;
        int giftFilter = -1;
    };
    std::vector<TouchTarget> touchTargets;
    int selectedTouchRow = -1;
    bool touchControlsVisible = false;

    void touchButton(int x, int y, int w, int h, u64 keys)
    {
        touchTargets.push_back({{x, y, w, h}, keys});
    }

    void touchSelection(int x, int y, int w, int h, int& selection, int index)
    {
        touchTargets.push_back({{x, y, w, h}, HidNpadButton_A, -1, false, &selection, index});
    }

    void touchRow(int x, int y, int w, int h, int row, bool selected)
    {
        touchTargets.push_back({{x, y, w, h}, 0, row});
        if (selected)
            selectedTouchRow = row;
    }

    void touchControlsButton(Canvas& c, int x, int y)
    {
        c.panel(x, y, 128, 42, Band, Edge, 7);
        c.text(x + 13, y + 4, "Controls", Font::Small, Ink);
        touchTargets.push_back({{x, y, 128, 42}, 0, -1, true});
    }

    void renderTouchControls(Canvas& c)
    {
        touchTargets.clear();
        c.suspendPortrait();
        c.panel(334, 174, 612, 367, Well, Cyan, 13);
        c.text(358, 187, "Touch controls", Font::Title);
        struct Button { std::string_view text; u64 keys; };
        constexpr std::array buttons{
            Button{"Up", HidNpadButton_Up}, Button{"Down", HidNpadButton_Down},
            Button{"Left", HidNpadButton_Left}, Button{"Right", HidNpadButton_Right},
            Button{"A", HidNpadButton_A}, Button{"B", HidNpadButton_B},
            Button{"X", HidNpadButton_X}, Button{"Y", HidNpadButton_Y},
            Button{"L", HidNpadButton_L}, Button{"R", HidNpadButton_R},
            Button{"ZL", HidNpadButton_ZL}, Button{"ZR", HidNpadButton_ZR},
            Button{"Minus", HidNpadButton_Minus}, Button{"Plus", HidNpadButton_Plus},
            Button{"Search", SearchButtons}, Button{"Motion", HidNpadButton_StickR},
        };
        for (unsigned i = 0; i < buttons.size(); ++i)
        {
            const int x = 356 + 144 * (i % 4);
            const int y = 245 + 68 * (i / 4);
            const auto& [label, key] = buttons[i];
            if (key == SearchButtons && !searchAvailable())
                continue;
            c.panel(x, y, 134, 56, Deck, Edge, 7);
            c.text(x + (134 - c.measure(label, Font::Body)) / 2, y + 12, label, Font::Body);
            touchButton(x, y, 134, 56, key);
        }
        c.panel(802, 187, 116, 46, Deck, Edge, 7);
        c.text(828, 193, "Close", Font::Small);
        touchTargets.push_back({{802, 187, 116, 46}, 0, -1, true});
    }

    int boxCount() const { return std::max(1, save.data->maxBoxes()); }

    int slots() const { return forge::boxSlotCount(*save.data, box); }

    int partyCount() const { return std::min(6, static_cast<int>(save.data->partyCount())); }

    void refreshBox()
    {
        slot = std::min(slot, slots() - 1);
        for (int i = 0; i < slots(); ++i)
        {
            const auto pokemon = save.data->pkm(box, i);
            names[i] = occupied(*pokemon) ? speciesName(*pokemon) : "-";
            species[i] = speciesId(*pokemon);
            rare[i] = species[i] && pokemon->shiny();
        }
    }

    void enterBox()
    {
        view = View::Box;
        refreshBox();
    }

    std::string_view query(View target) const
    {
        const auto it = searches.find(target);
        return it == searches.end() ? std::string_view{} : it->second;
    }

    void resetSearch(View target) { searches.erase(target); }

    void filterFiles(Browser& list, View target, bool byGame = false)
    {
        const SearchQuery filter(query(target));
        if (!filter.empty())
            std::erase_if(list.files, [&](const auto& file)
            {
                return !filter.matches({file.name}) &&
                    (!byGame || !filter.matches({file.name, shelfGame(file).title}));
            });
        list.move(0);
    }

    void reloadSaves()
    {
        saves.load(forge::Saves, false);
        filterFiles(saves, View::Files, true);
    }

    void reloadBank()
    {
        bank.load(forge::Bank, true);
        const SearchQuery filter(query(View::Bank));
        if (!filter.empty())
        {
            std::erase_if(bank.files, [&](const auto& file)
            {
                if (filter.matches({file.name}))
                    return false;
                try
                {
                    const auto pk = forge::bankPokemon(file);
                    return !filter.matches({file.name, speciesName(*pk), pk->nickname(),
                        std::to_string(speciesId(*pk)), pk->shiny() ? "Shiny" : ""});
                }
                catch (const std::runtime_error&)
                {
                    return true;
                }
            });
        }
        bank.move(0);
        bankPage = -1;
    }

    void showBank()
    {
        reloadBank();
        bankFrom = view;
        view = View::Bank;
    }

    void showDetail(View from, std::unique_ptr<PKX> pokemon)
    {
        if (!pokemon || !occupied(*pokemon))
            throw std::runtime_error("Choose an occupied slot.");
        detail = std::move(pokemon);
        detailFrom = from;
        view = View::Detail;
    }

    void closeToFiles()
    {
        reloadSaves();
        album={};
        save = {};
        detail.reset();
        view = View::Files;
    }

    void requestClose(Confirm action)
    {
        if (save.dirty)
            confirm = action;
        else if (action == Confirm::Exit)
            exit = true;
        else
            closeToFiles();
    }

    void handleFiles(u64 keys)
    {
        if (keys & HidNpadButton_ZL)
            showTools();
        else if (keys & HidNpadButton_X)
        {
            gameIcons.clear();
            reloadSaves();
        }
        else if (keys & HidNpadButton_Y)
            showBank();
        else if (keys & (HidNpadButton_Up | HidNpadButton_Left))
            saves.move(-1);
        else if (keys & (HidNpadButton_Down | HidNpadButton_Right))
            saves.move(1);
        else if (keys & HidNpadButton_L)
            saves.move(-4);
        else if (keys & HidNpadButton_R)
            saves.move(4);
        else if ((keys & HidNpadButton_A) && !saves.files.empty())
        {
            save = forge::openSave(saves.files[saves.index]);
            resetSearch(View::BoxSearch);
            resetSearch(View::Bag);
            resetSearch(View::BagAdd);
            sourceWritable = save.writable;
            save.writable &= mode != 2;
            box = std::min(static_cast<int>(save.data->currentBox()), boxCount() - 1);
            slot = party = 0;
            enterBox();
        }
    }

    void handleBox(u64 keys)
    {
        if (keys & HidNpadButton_B)
            requestClose(Confirm::Files);
        else if (keys & HidNpadButton_Y)
            showBank();
        else if (keys & HidNpadButton_ZL)
            showTools();
        else if (keys & HidNpadButton_X)
            view = View::Party;
        else if (keys & HidNpadButton_Minus)
        {
            openTrainer();
        }
        else if (keys & HidNpadButton_A)
            showDetail(View::Box, save.data->pkm(box, slot));
        else if ((keys & HidNpadButton_ZR) && clipboard)
        {
            ManagementAccess permission(save,mode==2 && sourceWritable);
            if (mode==2 && !clipboardSource) throw std::runtime_error("Select a Bank entry to move.");
            auto copy = forge::previewTransfer(save, box, slot, *clipboard);
            if (mode!=2 && forge::sameFormat(*copy, *clipboard))
            {
                forge::putPokemon(save, box, slot, *copy);
                refreshBox();
                status = "Copied to the box. Press + to export edits.";
            }
            else
            {
                transfer = std::move(copy);
                transferChanges.clear();
                transferRow = 0;
                for (const auto& field : forge::pokemonFields(*transfer))
                {
                    if (field.kind == forge::Field::Kind::Tera && !forge::sameFormat(*clipboard, *transfer))
                        continue;
                    const auto before = forge::fieldValue(*clipboard, field);
                    const auto after = forge::fieldValue(*transfer, field);
                    if (before != after)
                        transferChanges.emplace_back(field.label, before + " > " + after);
                }
                view = View::Transfer;
            }
        }
        else if (keys & (HidNpadButton_L | HidNpadButton_R))
        {
            box = cycle(box, keys & HidNpadButton_L ? -1 : 1, boxCount());
            refreshBox();
        }
        else if (keys & HidNpadButton_Left)
            slot = std::max(0, slot - 1);
        else if (keys & HidNpadButton_Right)
            slot = std::min(slots() - 1, slot + 1);
        else if (keys & HidNpadButton_Up)
            slot = std::max(0, slot - 6);
        else if (keys & HidNpadButton_Down)
            slot = std::min(slots() - 1, slot + 6);
    }

    void handleParty(u64 keys)
    {
        if (keys & (HidNpadButton_B | HidNpadButton_X))
            enterBox();
        else if (partyCount() && (keys & HidNpadButton_A))
            showDetail(View::Party, save.data->pkm(party));
        else if (keys & (HidNpadButton_Up | HidNpadButton_Left))
            party = std::max(0, party - (keys & HidNpadButton_Up ? 3 : 1));
        else if ((keys & (HidNpadButton_Down | HidNpadButton_Right)) && partyCount())
            party = std::min(partyCount() - 1, party + (keys & HidNpadButton_Down ? 3 : 1));
    }

    bool detailEditable() const
    {
        return save.writable && (detailFrom == View::Box || detailFrom == View::Party);
    }

    void handleDetail(u64 keys)
    {
        if (keys & HidNpadButton_B)
        {
            view = detailFrom;
            detail.reset();
        }
        else if ((keys & HidNpadButton_Minus) && (detailFrom==View::Party || detailFrom==View::Box))
            openEngine("slot",speciesName(*detail),detailFrom==View::Party?-1:box,detailFrom==View::Party?party:slot);
        else if (keys & HidNpadButton_ZL)
        {
            if (mode==2) throw std::runtime_error("Hardcore does not allow a park copy.");
            if (detailFrom != View::Bank && !forge::canDeposit(*save.data))
                throw std::runtime_error("GameCube story records can be inspected in the save.");
            forge::moveCopyToPark(*detail);
            status = "A copy joined the park. Open Pokepark from Save tools.";
        }
        else if ((keys & HidNpadButton_X) && detailEditable())
            openEditor();
        else if (keys & HidNpadButton_Y)
        {
            if (mode==2 && detailFrom!=View::Bank && sourceWritable)
            {
                bankDepositReview=true; transfer=detail->clone(); transferChanges.clear(); transferRow=0;
                view=View::Transfer; return;
            }
            if (mode==2) throw std::runtime_error("Select a Pokemon from an editable save.");
            if (detailFrom == View::Bank)
                throw std::runtime_error("This Pokemon is already in the Bank.");
            if (!forge::canDeposit(*save.data))
                throw std::runtime_error("GameCube story records can be inspected in the save.");
            forge::deposit(*detail);
            status = "Copied to the Bank.";
        }
        else if (keys & HidNpadButton_A && detailFrom == View::Bank)
        {
            clipboardSource=bank.files.at(bank.index);
            clipboard = detail->clone();
            detail.reset();
            view = bankFrom;
            status = "Selected for paste. In Boxes, choose an empty slot and press ZR.";
        }
        else if ((keys & (HidNpadButton_Left | HidNpadButton_Right |
            HidNpadButton_L | HidNpadButton_R)) &&
            detailEditable())
        {
            const auto before = detail->currentFriendship();
            const int amount = keys & (HidNpadButton_L | HidNpadButton_R) ? 10 : 1;
            const bool decrease = keys & (HidNpadButton_Left | HidNpadButton_L);
            const int value = std::clamp(before + (decrease ? -amount : amount), 0, 255);
            if (value == before)
                return;
            detail->currentFriendship(value);
            detail->refreshChecksum();
            if (detailFrom == View::Party)
                forge::editPartyMember(save, party, *detail, false);
            else
                save.data->pkm(*detail, box, slot, false);
            save.dirty = true;
            status = "Friendship changed. Press + to export edits.";
        }
    }

    void openTrainer()
    {
        trainerRow = 0;
        moneyDraft = save.data->money();
        timeDraft = {save.data->playedHours(), save.data->playedMinutes(), save.data->playedSeconds()};
        tidDraft = save.data->TID();
        sidDraft = save.data->SID();
        genderDraft = save.data->gender();
        trainerNameDraft = save.data->otName();
        view = View::Trainer;
    }

    void handleTrainer(u64 keys)
    {
        if (keys & HidNpadButton_Y) { openEngine("trainer","Trainer profile and card",box,slot); return; }
        if (keys & HidNpadButton_B)
        {
            enterBox();
            return;
        }
        if (!save.writable)
            return;
        if (keys & (HidNpadButton_Up | HidNpadButton_Down))
            trainerRow = cycle(trainerRow, keys & HidNpadButton_Up ? -1 : 1, 7);
        else if (keys & HidNpadButton_X)
        {
            const auto gen = save.data->generation();
            const auto maxLength = gen >= pksm::Generation::SIX ? 12 :
                gen <= pksm::Generation::THREE && save.data->language() == Language::JPN ? 5 : 7;
            if (auto value = keyboardText("Trainer name", trainerNameDraft, maxLength))
                trainerNameDraft = std::move(*value);
        }
        else if (keys & HidNpadButton_A)
        {
            const auto apply = [&](auto desired, auto before, auto setter)
            {
                if (desired != before)
                {
                    setter(desired);
                    save.dirty = true;
                }
            };
            apply(moneyDraft, save.data->money(), [&](auto value) { save.data->money(value); });
            apply(tidDraft, save.data->TID(), [&](auto value) { save.data->TID(value); });
            apply(sidDraft, save.data->SID(), [&](auto value) { save.data->SID(value); });
            apply(genderDraft, save.data->gender(), [&](auto value) { save.data->gender(value); });
            apply(trainerNameDraft, save.data->otName(), [&](auto value) { save.data->otName(value); });
            if (timeDraft[0] != save.data->playedHours())
            {
                save.data->playedHours(timeDraft[0]);
                save.dirty |= timeDraft[0] == save.data->playedHours();
            }
            if (timeDraft[1] != save.data->playedMinutes())
            {
                save.data->playedMinutes(timeDraft[1]);
                save.dirty |= timeDraft[1] == save.data->playedMinutes();
            }
            if (timeDraft[2] != save.data->playedSeconds())
            {
                save.data->playedSeconds(timeDraft[2]);
                save.dirty |= timeDraft[2] == save.data->playedSeconds();
            }
            openTrainer();
            if (save.dirty)
                status = "Trainer changes applied. Press + to export.";
        }
        else
        {
            int delta = adjustment(keys);
            if (keys & HidNpadButton_ZL) delta = -100;
            if (keys & HidNpadButton_ZR) delta = 100;
            if (!delta)
                return;
            if (trainerRow == 0)
            {
                const auto version = save.data->version();
                const bool bdsp = version == pksm::GameVersion::BD || version == pksm::GameVersion::SP;
                const auto cap = !bdsp && save.data->generation() >= pksm::Generation::FIVE
                    ? 9999999u : 999999u;
                moneyDraft = std::clamp(static_cast<long long>(moneyDraft) + 100ll * delta, 0ll,
                    static_cast<long long>(std::max(cap, save.data->money())));
            }
            else if (trainerRow == 1)
                tidDraft = std::clamp(static_cast<int>(tidDraft) + delta, 0, 65535);
            else if (trainerRow == 2)
                sidDraft = std::clamp(static_cast<int>(sidDraft) + delta, 0, 65535);
            else if (trainerRow == 3)
                genderDraft = genderDraft == pksm::Gender::Female ? pksm::Gender::Male : pksm::Gender::Female;
            else
            {
                const int index = trainerRow - 4;
                timeDraft[index] = std::clamp(timeDraft[index] + delta, 0, index == 0 ?
                    (save.data->generation() == pksm::Generation::ONE ? 255 : std::max<int>(999, save.data->playedHours())) : 59);
            }
        }
    }

    void handleBank(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = bankFrom;
        else if (keys & HidNpadButton_X)
            reloadBank();
        else if (keys & HidNpadButton_ZL)
            openSearch();
        else if (keys & (HidNpadButton_Up | HidNpadButton_Left))
            bank.move(keys & HidNpadButton_Up ? -4 : -1);
        else if (keys & (HidNpadButton_Down | HidNpadButton_Right))
            bank.move(keys & HidNpadButton_Down ? 4 : 1);
        else if (keys & HidNpadButton_L)
            bank.move(-12);
        else if (keys & HidNpadButton_R)
            bank.move(12);
        else if ((keys & HidNpadButton_A) && !bank.files.empty())
            showDetail(View::Bank, forge::bankPokemon(bank.files[bank.index]));
    }

    void showTools()
    {
        toolsFrom = view;
        tool = 0;
        view = View::Tools;
    }

    void reloadBag()
    {
        bagSlots.clear();
        const auto pouches = save.data->pouches();
        if (pouches.empty())
            return;
        const auto [pouch, count] = pouches[bagPouch];
        const SearchQuery filter(query(View::Bag));
        for (int i = 0; i < count; ++i)
        {
            const auto item = forge::bagSlot(*save.data, pouch, i);
            if (item.id && item.count && matchesItem(filter, item.id))
                bagSlots.push_back(i);
        }
        bagRow = 0;
        resetBagDraft();
    }

    static bool matchesItem(const SearchQuery& filter, unsigned id, std::string_view pouch = {})
    {
        return filter.empty() || filter.matches({i18n::item(Language::ENG, id), std::to_string(id), pouch});
    }

    void reloadBagOptions()
    {
        bagOptions.clear();
        bagOption = 0;
        const SearchQuery filter(query(View::BagAdd));
        const auto pouches = save.data->pouches();
        for (int i = 0; i < static_cast<int>(pouches.size()); ++i)
        {
            if (filter.empty() && i != bagPouch)
                continue;
            const auto pouch = pouches[i].first;
            for (const auto id : forge::bagCatalog(*save.data, pouch))
                if (matchesItem(filter, id, forge::pouchName(pouch)))
                    bagOptions.push_back({i, id});
        }
        limitBagAddCount();
    }

    void limitBagAddCount()
    {
        if (!bagOptions.empty())
        {
            const auto pouch = save.data->pouches()[bagOptions[bagOption].pouch].first;
            bagAddCount = std::min<unsigned>(bagAddCount, save.data->maxCount(pouch));
        }
    }

    void reloadStoredItems()
    {
        storedItems = forge::loadItems();
        const SearchQuery filter(query(View::ItemBank));
        if (!filter.empty())
            std::erase_if(storedItems, [&](const auto& item) { return !matchesItem(filter, item.id); });
        storedRow = 0;
    }

    void resetBagDraft()
    {
        if (!bagSlots.empty())
        {
            const auto pouch = save.data->pouches()[bagPouch].first;
            bagDraft = forge::bagSlot(*save.data, pouch, bagSlots[bagRow]).count;
        }
    }

    void computeDex()
    {
        unsigned skipped = 0;
        const auto files = forge::listFiles(forge::Bank, true, true);
        dexCounts = forge::livingDex(save.data.get(), files, shinyDex, &skipped);
        if (skipped)
            status = std::to_string(skipped) + " unreadable Bank records were skipped.";
        selectDexEntries();
    }

    void selectDexEntries()
    {
        dexEntries.clear();
        const SearchQuery filter(query(View::Dex));
        for (unsigned id = 1; id < dexCounts.size(); ++id)
            if ((!missingDex || !dexCounts[id]) && (filter.empty() ||
                filter.matches({pksm::Species(id).localize(Language::ENG), std::to_string(id)})))
                dexEntries.push_back(id);
        dexRow = 0;
    }

    std::string_view preferredGiftLanguage() const
    {
        int language = save.data ? static_cast<int>(save.data->language()) : 0;
        static constexpr std::array tags{"","JPN","ENG","FRE","ITA","GER","","SPA","KOR","CHS","CHT"};
        return language>=0 && language<int(tags.size())?tags[language]:"";
    }

    void filterEvents()
    {
        album.filter(query(View::Events),preferredGiftLanguage());
        eventRow=eventCopy=0;
    }

    void loadEvents()
    {
        const auto generation = "gen" + std::to_string(eventGen);
        const auto local = "sdmc:/switch/PKForge/media/events/" + generation;
        struct stat info{};
        const bool missing = ::stat((local + ".bin").c_str(), &info) != 0 && errno == ENOENT;
        album.load(missing ? "romfs:/events/" + generation : local, eventGen, save);
        filterEvents();
    }

    const forge::AlbumCard& eventCard() const
    {
        return album.cards[album.groups[eventRow].copies[eventCopy]];
    }

    std::array<std::string,9> filterValues() const
    {
        return {album.fitsOnly?"Fits save":"Any compatibility",album.kinds[album.kind],
            album.shinyOnly?"Shiny only":"Any shininess",album.statuses[album.status],
            album.language.empty()?"Any language":album.language,album.collapse?"Group copies":"Separate copies",
            album.sorts[album.sort],album.groupings[album.grouping],"Gen "+std::to_string(eventGen)};
    }

    void changeGiftFilter(int direction)
    {
        switch(eventFilterRow) {
            case 0:album.fitsOnly=!album.fitsOnly;break;
            case 1:album.kind=cycle(album.kind,direction,album.kinds.size());break;
            case 2:album.shinyOnly=!album.shinyOnly;break;
            case 3:album.status=cycle(album.status,direction,album.statuses.size());break;
            case 4:{
                auto it=std::find(album.languages.begin(),album.languages.end(),album.language);
                int index=it==album.languages.end()?0:it-album.languages.begin();
                index=cycle(index,direction,album.languages.size());
                album.language=index?album.languages[index]:"";break;
            }
            case 5:album.collapse=!album.collapse;break;
            case 6:album.sort=cycle(album.sort,direction,album.sorts.size());break;
            case 7:album.grouping=cycle(album.grouping,direction,album.groupings.size());break;
            case 8:eventGen=4+cycle(eventGen-4,direction,6);loadEvents();return;
        }
        filterEvents();
    }

    void openPark(bool walking)
    {
        residents = forge::parkResidents();
        residentRow = 0;
        walkX = 404;
        walkY = 365;
        walkSteps = 0;
        view = walking ? View::Walker : View::Park;
    }

    void handlePark(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Tools;
        else if (!residents.empty())
        {
            if (keys & (HidNpadButton_Left | HidNpadButton_Right))
                residentRow = cycle(residentRow, keys & HidNpadButton_Left ? -1 : 1, residents.size());
            else if (keys & HidNpadButton_A)
            {
                forge::logParkActivity(residents[residentRow], 1, 0, 0);
                ++residents[residentRow].feeds;
                status = "Shared a snack with your resident.";
            }
            else if (keys & HidNpadButton_Y)
            {
                forge::logParkActivity(residents[residentRow], 0, 1, 0);
                ++residents[residentRow].plays;
                status = "Played with your resident.";
            }
            else if (keys & HidNpadButton_ZL)
            {
                reloadJournal();
                view = View::ParkJournal;
            }
            else if (keys & HidNpadButton_X)
            {
                walkSteps = 0;
                walkX = 404;
                walkY = 365;
                view = View::Walker;
            }
        }
    }

    void handleJournal(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Park;
        else
            scroll(keys, journalRow, journal.size());
    }

    static std::string journalDate(const forge::ParkEvent& event)
    {
        std::array<char, 36> date{};
        if (const auto* local = std::localtime(&event.when))
            std::strftime(date.data(), date.size(), "%Y-%m-%d %H:%M", local);
        return date.data();
    }

    static std::string journalAction(const forge::ParkEvent& event)
    {
        return event.steps ? std::to_string(event.steps) + " steps" :
            event.feeds ? "Shared a snack" : "Played together";
    }

    void reloadJournal()
    {
        journal = forge::parkJournal(residents[residentRow]);
        journalRow = 0;
        const SearchQuery filter(query(View::ParkJournal));
        if (!filter.empty())
            std::erase_if(journal, [&](const auto& event)
            {
                return !filter.matches({journalAction(event), journalDate(event)});
            });
    }

    void handleNursery(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Tools;
        else if (keys & (HidNpadButton_Up | HidNpadButton_Down))
            parentRow = 1 - parentRow;
        else if ((keys & HidNpadButton_A) && save.writable && occupied(*nurseryView.parents[parentRow]))
        {
            if (occupied(*save.data->pkm(box, slot)))
                throw std::runtime_error("Choose an empty box slot first.");
            if (nurseryView.eggReady)
                throw std::runtime_error("Collect the waiting egg in-game first.");
            view = View::NurseryReview;
        }
    }

    void handleNurseryReview(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Nursery;
        else if (keys & HidNpadButton_A)
        {
            forge::withdrawNursery(save, parentRow, box, slot);
            enterBox();
            status = "Withdrawn to the selected box slot. Press + to export.";
        }
    }

    void saveWalk()
    {
        if (!walkSteps || residents.empty())
            return;
        forge::logParkActivity(residents[residentRow], 0, 0, walkSteps);
        residents[residentRow].steps += walkSteps;
        walkSteps = 0;
    }

    void handleWalker(u64 keys)
    {
        if (keys & HidNpadButton_B)
        {
            saveWalk();
            view = View::Park;
        }
        else if (!residents.empty())
        {
            if (keys & HidNpadButton_A)
            {
                saveWalk();
                status = "Walk saved.";
            }
            else if (keys & (HidNpadButton_L | HidNpadButton_R))
            {
                saveWalk();
                residentRow = cycle(residentRow, keys & HidNpadButton_L ? -1 : 1, residents.size());
            }
            else if (walkSteps < 65535)
            {
                const int x = walkX, y = walkY;
                if (keys & HidNpadButton_Left) walkX = std::max(102, walkX - 18);
                if (keys & HidNpadButton_Right) walkX = std::min(685, walkX + 18);
                if (keys & HidNpadButton_Up) walkY = std::max(234, walkY - 18);
                if (keys & HidNpadButton_Down) walkY = std::min(514, walkY + 18);
                if (x != walkX || y != walkY)
                    ++walkSteps;
            }
        }
    }

    void handleItemBank(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Tools;
        else if ((keys & HidNpadButton_A) && !storedItems.empty() && save.writable)
        {
            forge::copyItemToSave(save, storedItems[storedRow]);
            status = "Item copied to the bag. Press + to export.";
        }
        else
            scroll(keys, storedRow, storedItems.size());
    }

    struct EngineRow { char kind; std::string key, name, value; };
    struct EnginePage
    {
        std::string title;
        std::vector<EngineRow> rows;
        int selected = 0;
    };
    std::vector<EnginePage> enginePages;
    std::vector<int> engineVisible;
    std::unique_ptr<forge::EngineJob> engineJob;
    std::shared_ptr<const std::vector<u8>> engineSource;
    std::vector<u8> engineCandidate;
    std::string engineTitle;
    bool engineManagement = false;
    View engineFrom = View::Tools;

    void filterEngine()
    {
        engineVisible.clear();
        if (enginePages.empty()) return;
        auto& page = enginePages.back();
        const SearchQuery filter(query(View::Engine));
        for (size_t i=0; i<page.rows.size(); ++i)
            if (filter.matches({page.rows[i].name,page.rows[i].value}))
                engineVisible.push_back(i);
        page.selected = std::clamp(page.selected, 0, std::max(0, int(engineVisible.size())-1));
    }

    void pushEnginePage(EnginePage page)
    {
        resetSearch(View::Engine);
        enginePages.push_back(std::move(page));
        filterEngine();
    }

    void startEngine(std::string key, const std::string& title, std::string value = {})
    {
        engineTitle = title;
        engineManagement = key.contains(":withdraw/") || key.contains(":depositparent/");
        engineJob = std::make_unique<forge::EngineJob>(engineSource,
            std::vector<std::string>{"ForgeTools",std::to_string(mode),std::move(key),std::move(value),preferences.trainerOwnership?"owner":"other",save.file.name});
        view = View::Engine;
    }

    void openEngine(std::string action, std::string title, int b, int s)
    {
        engineSource=std::make_shared<const std::vector<u8>>(forge::engineSnapshot(save));
        engineFrom = view;
        enginePages.clear();
        engineCandidate.clear();
        resetSearch(View::Engine);
        startEngine(std::to_string(b)+","+std::to_string(s)+":"+action,title);
    }

    void finishEngine()
    {
        auto job = std::move(engineJob);
        try
        {
            auto result = job->take();
            EnginePage page{engineTitle};
            std::istringstream lines(result.text);
            for (std::string line; std::getline(lines,line); )
            {
                size_t a=line.find('\t'), b=line.find('\t',a+1), c=line.find('\t',b+1);
                if (a!=1 || b==line.npos || c==line.npos) continue;
                page.rows.push_back({line[0],line.substr(a+1,b-a-1),line.substr(b+1,c-b-1),line.substr(c+1)});
            }
            engineCandidate = std::move(result.changed);
            for (auto& row : page.rows) if (row.kind=='o') row.value=preferences.trainerOwnership?"On":"Off";
            if (page.rows.empty()) page.rows.push_back({'t',{},"No entries for this save.",{}});
            pushEnginePage(std::move(page));
        }
        catch (const std::exception& e)
        {
            pushEnginePage({"Operation unavailable",{{'t',{},e.what(),{}}}});
        }
    }

    struct ManagementAccess
    {
        forge::Save& save;
        bool previous;
        ManagementAccess(forge::Save& save, bool allow) : save(save), previous(save.writable)
        { save.writable |= allow; }
        ~ManagementAccess() { save.writable = previous; }
    };

    void handleEngine(u64 keys)
    {
        if (engineJob) { if (engineJob->ready()) finishEngine(); return; }
        if (keys & HidNpadButton_B)
        {
            engineCandidate.clear();
            enginePages.pop_back();
            resetSearch(View::Engine);
            if (enginePages.empty()) { engineSource.reset(); view=engineFrom; }
            else filterEngine();
            return;
        }
        auto& page=enginePages.back();
        if (!engineCandidate.empty() && (keys & HidNpadButton_X))
        {
            ManagementAccess permission(save,engineManagement && sourceWritable);
            forge::acceptEngineCopy(save,engineCandidate);
            engineCandidate.clear(); enginePages.clear(); engineSource.reset(); detail.reset();
            party=std::max(0,std::min(party,partyCount()-1));
            enterBox();
            status="Changes applied. Press + to export a copy.";
            return;
        }
        if (!(keys & HidNpadButton_A) || engineVisible.empty())
        { scroll(keys,page.selected,engineVisible.size()); return; }
        const auto row=page.rows[engineVisible[page.selected]];
        if (row.kind=='t') return;
        if (row.kind=='o')
        {
            auto next=preferences;
            next.trainerOwnership=!next.trainerOwnership;
            forge::writeEditorPreferences(next);
            preferences=next;
            page.rows[engineVisible[page.selected]].value=next.trainerOwnership?"On":"Off";
            return;
        }
        if (row.kind=='d')
        {
            pushEnginePage({"Delete trainer profile",{{'D',{},row.name,"A Delete   B Keep"}}});
            return;
        }
        if (row.kind=='D')
        {
            forge::deleteTrainerProfile(row.name);
            enginePages.pop_back(); enginePages.pop_back(); filterEngine();
            status="Trainer profile deleted.";
            return;
        }
        if (row.kind=='p')
        {
            if (auto name=keyboardText("Save trainer profile as","",40))
            {
                forge::storeTrainerProfile(*name,row.value);
                status="Trainer profile saved on the SD card.";
            }
            return;
        }
        if (row.kind=='l')
        {
            EnginePage profiles{"Choose trainer profile"};
            for (const auto& [name,token]:forge::trainerProfiles())
                { profiles.rows.push_back({'v',row.key,name,token}); profiles.rows.push_back({'d',{},name,"Delete profile"}); }
            if (profiles.rows.empty()) profiles.rows.push_back({'t',{},"Save a named profile from Trainer profile and card first.",{}});
            pushEnginePage(std::move(profiles)); return;
        }
        if ((row.kind=='w'||row.kind=='i'||row.kind=='v') && !save.writable &&
            !(sourceWritable && (row.key.contains(":withdraw/") || row.key.contains(":depositparent/"))))
            throw std::runtime_error("This operation is unavailable in read-only or Hardcore mode.");
        std::string value;
        if (row.kind=='q' || row.kind=='i')
        {
            const bool hint=row.value.starts_with("Species name") || row.value.starts_with("Separate lines");
            auto answer=searchKeyboard(row.name.c_str(),hint?"":row.value,500,false);
            if (!answer) return;
            value=std::move(*answer);
        }
        if (row.kind=='v') value=row.value;
        startEngine(row.key,row.name,std::move(value));
    }

    void renderEngine(Canvas& c)
    {
        toolFrame(c,engineJob?engineTitle:enginePages.back().title,engineCandidate.empty()?"SAVE TOOLS":"REVIEW CHANGES");
        if (engineJob)
        {
            c.text(75,220,"Reading save data...",Font::Title,Ink,638);
            c.text(800,239,"Working offline",Font::Title,Ink,410);
            c.text(800,308,"A full sweep checks each occupied slot.",Font::Small,Soft,410);
            return;
        }
        if (engineVisible.empty()) { noMatches(c); return; }
        const auto& page=enginePages.back();
        listReview(c,page.selected,engineVisible.size(),[&](int i) {
            const auto& r=page.rows[engineVisible[i]];
            toolRow(c,i%7,label(r.name,100),(r.kind=='p'||r.kind=='v')?"":label(r.value,75),i==page.selected);
        });
        const auto& row=page.rows[engineVisible[page.selected]];
        std::string description=label(row.name,500);
        if (row.kind!='v' && row.kind!='p' && !row.value.empty()) description+="   "+label(row.value,500);
        for (int y=242; y<505 && !description.empty(); y+=35)
        {
            size_t count=std::min<size_t>(37,description.size());
            if (count<description.size())
                if (auto space=description.rfind(' ',count);space!=description.npos && space>0) count=space;
            c.text(800,y,description.substr(0,count),Font::Body,Ink,410);
            description.erase(0,count);
            while (!description.empty() && description.front()==' ') description.erase(0,1);
        }
        c.text(800,525,std::to_string(page.selected+1)+" / "+std::to_string(engineVisible.size()),Font::Small,Cyan);
    }

    void handleTools(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = toolsFrom;
        else if (keys & (HidNpadButton_Up | HidNpadButton_Down))
            tool = cycle(tool, keys & HidNpadButton_Up ? -1 : 1, 14);
        else if (keys & HidNpadButton_A)
        {
            if (tool == 0)
            {
                if (!save.data || save.data->pouches().empty())
                    throw std::runtime_error("Open a save with inventory first.");
                bagPouch = 0;
                reloadBag();
                view = View::Bag;
            }
            else if (tool == 1)
            {
                reloadStoredItems();
                view = View::ItemBank;
            }
            else if (tool == 2)
            {
                computeDex();
                view = View::Dex;
            }
            else if (tool == 3)
            {
                const auto generation = save.data ?
                    std::string(static_cast<const char*>(save.data->generation())) : "";
                eventGen = generation.size() == 1 && generation[0] >= '4' && generation[0] <= '9'
                    ? generation[0] - '0' : generation == "LGPE" ? 7 : 4;
                eventFilters=false;
                loadEvents();
                view = View::Events;
            }
            else if (tool < 6)
                openPark(tool == 5);
            else if (tool == 6)
                openBoxTools();
            else if (tool == 7)
                loadSnapshots();
            else if (tool == 8)
            {
                showdownText.clear();
                qr[0] = 0;
                view = View::Showdown;
            }
            else if (tool == 9)
            {
                modeRow = mode;
                view = View::Modes;
            }
            else if (tool == 10)
            {
                openEngine("nursery","Day Care and breeding",box,slot);
            }
            else if (tool == 11)
            {
                reloadHall();
                view = View::Hall;
            }
            else if (tool == 13) openEngine("home","Advanced save tools",box,slot);
            else
            {
                clockDraft = forge::gbaClock(save);
                clockRow = 0;
                view = View::GbaClock;
            }
        }
    }

    static std::optional<std::string> keyboardText(
        const char* heading, std::string_view current, int maxLength, bool allowEmpty = false)
    {
#ifdef __SWITCH2__
        SwkbdConfig keyboard{};
        if (R_FAILED(swkbdCreate(&keyboard, 0)))
            throw std::runtime_error("Cannot open the keyboard.");
        swkbdConfigMakePresetDefault(&keyboard);
        swkbdConfigSetHeaderText(&keyboard, heading);
        if (allowEmpty)
        {
            swkbdConfigSetGuideText(&keyboard, "Leave blank to show all");
            swkbdConfigSetOkButtonText(&keyboard, "Search");
        }
        const std::string initial(current);
        swkbdConfigSetInitialText(&keyboard, initial.c_str());
        swkbdConfigSetStringLenMin(&keyboard, allowEmpty ? 0 : 1);
        swkbdConfigSetStringLenMax(&keyboard, maxLength);
        std::vector<char> result(static_cast<size_t>(maxLength) * 4 + 1);
        const auto code = swkbdShow(&keyboard, result.data(), result.size());
        swkbdClose(&keyboard);
        if (R_SUCCEEDED(code) && (allowEmpty || result[0]))
            return std::string(result.data());
#endif
        return std::nullopt;
    }

    void openBoxTools()
    {
        if (!save.data)
            throw std::runtime_error("Open a save first.");
        marked.fill(false);
        boxToolSlot = slot;
        actionsRow = 0;
        view = View::BoxTools;
    }

    void handleBoxTools(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Tools;
        else if (keys & HidNpadButton_A)
        {
            actionsRow = 0;
            wallpaperDraft = save.data->boxWallpaper(box);
            view = View::BoxActions;
        }
        else if (keys & HidNpadButton_Y)
        {
            if (species[boxToolSlot])
                marked[boxToolSlot] = !marked[boxToolSlot];
        }
        else if (keys & HidNpadButton_X)
        {
            for (int i = 0; i < slots(); ++i)
                marked[i] = species[i] != 0;
        }
        else
            scroll(keys, boxToolSlot, slots());
    }

    std::vector<int> markedSlots() const
    {
        std::vector<int> result;
        for (int i = 0; i < slots(); ++i)
            if (marked[i])
                result.push_back(i);
        return result;
    }

    static constexpr std::array boxOperations{
        "Copy marked to Bank", "Move marked to next box", "Duplicate marked in this box",
        "Sort this box by species", "Rename this box", "Wallpaper", "Release marked", "Audit this box"
    };

    void handleBoxActions(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::BoxTools;
        else if ((keys & HidNpadButton_A) && actionsRow == 7)
        {
            reloadBoxAudit();
            view = View::BoxAudit;
        }
        else if (keys & HidNpadButton_A)
        {
            if (mode==2 && actionsRow!=1 && actionsRow!=3 && actionsRow!=6)
                throw std::runtime_error("Hardcore permits moves, sorting and releases.");
            ManagementAccess permission(save,mode==2 && sourceWritable);
            if (!save.writable && actionsRow != 0)
                throw std::runtime_error("The save is read-only.");
            if (actionsRow == 4)
            {
                if (auto name = keyboardText("Box name", save.data->boxName(box), 12))
                {
                    save.data->boxName(box, *name);
                    save.dirty = true;
                    status = "Box renamed. Press + to export.";
                }
            }
            else if (actionsRow == 5)
            {
                const int before = save.data->boxWallpaper(box);
                save.data->boxWallpaper(box, wallpaperDraft);
                if (save.data->boxWallpaper(box) == before && before != wallpaperDraft)
                    throw std::runtime_error("This save did not accept the wallpaper.");
                if (before != wallpaperDraft)
                {
                    save.dirty = true;
                    status = "Wallpaper updated. Press + to export.";
                }
            }
            else
                view = View::BoxReview;
        }
        else if (actionsRow == 5 && save.writable && (keys & (HidNpadButton_Left | HidNpadButton_Right)))
            wallpaperDraft = cycle(wallpaperDraft, keys & HidNpadButton_Left ? -1 : 1, 16);
        else
            scroll(keys, actionsRow, boxOperations.size());
    }

    void handleBoxReview(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::BoxActions;
        else if (keys & HidNpadButton_A)
        {
            using enum forge::BoxAction;
            constexpr std::array operations{CopyToBank, Move, Duplicate, Sort, Sort, Sort, Release};
            ManagementAccess permission(save,mode==2 && sourceWritable && (actionsRow==1 || actionsRow==3 || actionsRow==6));
            const auto count = forge::organizeBox(save, box, markedSlots(), operations[actionsRow]);
            marked.fill(false);
            refreshBox();
            view = View::BoxTools;
            status = std::to_string(count) + (actionsRow == 0 ?
                " Pokemon copied to the Bank." : " Pokemon affected. Press + to export.");
        }
    }

    void reloadBoxAudit()
    {
        issues = forge::auditBox(save, box);
        issuesRow = 0;
        const SearchQuery filter(query(View::BoxAudit));
        if (!filter.empty())
            std::erase_if(issues, [&](const auto& issue)
            {
                return !filter.matches({issue.message, "Slot " + std::to_string(issue.slot)});
            });
    }

    void loadSnapshots()
    {
        snapshots.load(forge::Snapshots, false);
        filterFiles(snapshots, View::Snapshots);
        view = View::Snapshots;
    }

    void handleSnapshots(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Tools;
        else if ((keys & HidNpadButton_X) && save.data)
        {
            if (auto name = keyboardText("Snapshot name", "", 40))
            {
                const auto saved = forge::snapshot(save, *name);
                loadSnapshots();
                status = "Created snapshot " + saved.name;
            }
        }
        else if ((keys & HidNpadButton_A) && !snapshots.files.empty())
        {
            const auto previous = forge::openSave(snapshots.files[snapshots.index]);
            snapshotGame = previous.data->version();
            snapshotDiff.reset();
            if (save.data && save.data->version() == snapshotGame)
                snapshotDiff = forge::compareSaves(save, previous);
            view = View::SnapshotReview;
        }
        else
            scroll(keys, snapshots.index, snapshots.files.size());
    }

    void handleSnapshotReview(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Snapshots;
        else if (keys & HidNpadButton_A)
        {
            if (save.dirty)
                throw std::runtime_error("Export the active draft before restoring a snapshot.");
            std::string path;
            {
                ManagementAccess permission(save,mode==2 && sourceWritable);
                path=forge::restoreSnapshot(save,snapshots.files[snapshots.index]);
            }
            sourceWritable=true; updateMode();
            box = slot = party = 0;
            enterBox();
            status = "Restored as " + path.substr(std::string_view(forge::Root).size() + 1);
        }
    }

    void openCrossSources()
    {
        if (!sourceWritable || dexEntries.empty())
            throw std::runtime_error("Open an editable destination and select a species.");
        donors.load(forge::Saves, false);
        std::erase_if(donors.files, [&](const auto& entry) { return entry.path == save.file.path; });
        filterFiles(donors, View::CrossSource, true);
        donors.index = 0;
        view = View::CrossSource;
    }

    void handleCrossSource(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Dex;
        else if ((keys & HidNpadButton_A) && !donors.files.empty())
        {
            ManagementAccess permission(save,mode==2 && sourceWritable);
            crossPlan = forge::planCrossDex(save, donors.files[donors.index], dexEntries[dexRow], shinyDex);
            crossRow = 0;
            view = View::CrossReview;
        }
        else
            scroll(keys, donors.index, donors.files.size());
    }

    void handleCrossReview(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::CrossSource;
        else if ((keys & HidNpadButton_A) && !crossPlan.moves.empty())
        {
            ManagementAccess permission(save,mode==2 && sourceWritable);
            const auto path = forge::applyCrossDex(save, crossPlan);
            const int destination = crossPlan.targetBox;
            box = destination;
            enterBox();
            status = "Both copies exported to " + path.substr(std::string_view(forge::Root).size() + 1);
        }
        else
            scroll(keys, crossRow, crossPlan.moves.size());
    }

    void handleDexRegister(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Dex;
        else if (keys & HidNpadButton_A)
        {
            const auto added = forge::registerOwned(save);
            status = std::to_string(added) + " new caught entries staged.";
            view = View::Dex;
        }
    }

    void makeQR()
    {
        std::array<uint8_t, qrcodegen_BUFFER_LEN_FOR_VERSION(25)> temp{};
        if (!qrcodegen_encodeText(showdownText.c_str(), temp.data(), qr.data(), qrcodegen_Ecc_LOW,
            1, 25, qrcodegen_Mask_AUTO, true))
        {
            qr[0] = 0;
            status = "This text is too long for the on-screen QR. Press X to save it.";
        }
    }

    void handleShowdown(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Tools;
        else if (!save.data)
            throw std::runtime_error("Open a save first.");
        else if (keys & HidNpadButton_A)
        {
            showdownText = forge::showdownParty(*save.data);
            if (showdownText.empty())
                throw std::runtime_error("The open save has an empty party.");
            makeQR();
        }
        else if (keys & HidNpadButton_Y)
        {
            showdownText = forge::showdownSet(*save.data->pkm(box, slot));
            makeQR();
        }
        else if (keys & HidNpadButton_X)
            status = "Saved " + forge::exportShowdown(showdownText).substr(std::string_view(forge::Root).size() + 1);
        else if (keys & HidNpadButton_ZL)
        {
            showdownDraft = forge::previewShowdownTeam(save, forge::readShowdown(), box, slot);
            showdownRow = 0;
            view = View::ShowdownReview;
        }
    }

    void handleShowdownReview(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Showdown;
        else if (keys & HidNpadButton_A)
        {
            forge::applyShowdownTeam(save, showdownDraft);
            const auto count = showdownDraft.size();
            showdownDraft.clear();
            enterBox();
            status = std::to_string(count) + " Pokemon imported. Press + to export.";
        }
        else
            scroll(keys, showdownRow, showdownDraft.size());
    }

    void handleModes(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Tools;
        else if (keys & (HidNpadButton_Up | HidNpadButton_Down))
            modeRow = cycle(modeRow, keys & HidNpadButton_Up ? -1 : 1, 3);
        else if (keys & HidNpadButton_A)
        {
            if (modeRow == 2 && !preferences.hardcore && save.dirty)
                throw std::runtime_error("Export your pending edits before entering Hardcore.");
            auto next=preferences;
            if (modeRow==0) next.hax=next.hardcore=false;
            else if (modeRow==1) next.hax=!next.hax;
            else next.hardcore=!next.hardcore;
            forge::writeEditorPreferences(next);
            preferences=next;
            updateMode();
            status = "Session editing mode changed.";
            view = View::Tools;
        }
    }


    void openEditor()
    {
        if (!detailEditable())
            throw std::runtime_error("Open a Pokemon from an editable save.");
        edit = detail->clone();
        fields = forge::pokemonFields(*edit);
        editRow = 0;
        editDirty = false;
        editedMoves = false;
        editedStats = false;
        view = View::Editor;
    }

    void openRibbons()
    {
        ribbons.clear();
        const SearchQuery filter(query(View::Ribbons));
        for (unsigned i = 0; i <= static_cast<unsigned>(pksm::Ribbon::Partner); ++i)
        {
            const auto ribbon = pksm::Ribbon(i);
            if (edit->hasRibbon(ribbon) && (filter.empty() ||
                filter.matches({ribbon.localize(Language::ENG), std::to_string(i)})))
                ribbons.push_back(ribbon);
        }
        ribbonRow = 0;
        view = View::Ribbons;
    }

    const char* searchHeading() const
    {
        switch (view)
        {
            case View::Files: return "Search save files";
            case View::Box:
            case View::BoxSearch: return "Search all boxes";
            case View::Bank: return "Search Pokemon Bank";
            case View::Bag: return "Search this pouch";
            case View::BagAdd: return "Search all addable items";
            case View::ItemBank: return "Search Item Bank";
            case View::Events: return eventFilters ? nullptr : "Search Mystery Gifts";
            case View::Dex: return "Search Living Dex";
            case View::Ribbons: return "Search ribbons and marks";
            case View::Snapshots: return "Search snapshots";
            case View::CrossSource: return "Search source saves";
            case View::BagPresets: return "Search bag presets";
            case View::Hall: return "Search Hall of Fame";
            case View::Engine: return !engineJob && engineCandidate.empty() ? "Search these tools and results" : nullptr;
            case View::BoxAudit: return "Search audit findings";
            case View::ParkJournal: return "Search park journal";
            case View::Editor:
            case View::EditorChoice:
                using Kind = forge::Field::Kind;
                switch (fields[editRow].kind)
                {
                    case Kind::Move: return "Search moves";
                    case Kind::HeldItem: return "Search held items";
                    case Kind::Ball: return "Search Poke Balls";
                    case Kind::Form: return "Search forms";
                    case Kind::Origin: return "Search origin games";
                    default: return nullptr;
                }
            default: return nullptr;
        }
    }

    void findBoxMatches()
    {
        boxMatches.clear();
        boxMatch = 0;
        const SearchQuery filter(query(View::BoxSearch));
        if (filter.empty())
        {
            enterBox();
            return;
        }
        for (int b = 0; b < boxCount(); ++b)
        {
            const auto boxName = save.data->boxName(b);
            for (int s = 0; s < forge::boxSlotCount(*save.data, b); ++s)
            {
                const auto pk = save.data->pkm(b, s);
                if (!occupied(*pk))
                    continue;
                const auto name = speciesName(*pk);
                const auto nickname = pk->nicknamed() ? pk->nickname() : std::string{};
                if (filter.matches({name, nickname, std::to_string(speciesId(*pk)), boxName,
                    pk->shiny() ? "Shiny" : ""}))
                    boxMatches.push_back({b, s, name, nickname});
            }
        }
        view = View::BoxSearch;
    }

    void handleBoxSearch(u64 keys)
    {
        if (keys & HidNpadButton_B)
            enterBox();
        else if ((keys & HidNpadButton_A) && !boxMatches.empty())
        {
            box = boxMatches[boxMatch].box;
            slot = boxMatches[boxMatch].slot;
            enterBox();
        }
        else
            scroll(keys, boxMatch, boxMatches.size());
    }

    void reloadHall()
    {
        hall = forge::hallOfFame(save);
        hallRow = 0;
        const SearchQuery filter(query(View::Hall));
        if (filter.empty())
            return;
        std::erase_if(hall, [&](const auto& team)
        {
            const auto number = "Record " + std::to_string(team.record);
            return !filter.matches({number}) && std::ranges::none_of(team.members, [&](const auto& member)
            {
                return filter.matches({number, member.species.localize(Language::ENG),
                    std::to_string(static_cast<unsigned>(member.species)), member.shiny ? "Shiny" : ""});
            });
        });
    }

    void reloadEditorChoices()
    {
        const auto field = fields[editRow];
        editorChoices = forge::fieldOptions(*save.data, *edit, field);
        const SearchQuery filter(editorQuery);
        if (!filter.empty())
            std::erase_if(editorChoices, [&](unsigned value)
            {
                return !filter.matches({forge::fieldOptionName(*edit, field, value), std::to_string(value)});
            });
        editorChoice = 0;
        view = View::EditorChoice;
    }

    void openSearch()
    {
        const auto heading = searchHeading();
        if (!heading)
            return;
        searchKeyboardOpened = true;
        if (view == View::Editor || view == View::EditorChoice)
        {
            const auto initial = view == View::EditorChoice ? std::string_view(editorQuery) : std::string_view{};
            if (auto value = searchKeyboard(heading, initial, 60, true))
            {
                editorQuery = std::move(*value);
                reloadEditorChoices();
            }
            return;
        }

        const auto target = view == View::Box ? View::BoxSearch : view;
        const auto value = searchKeyboard(heading, query(target), 60, true);
        if (!value)
            return;
        if (target == View::Events ? value->find_first_not_of(" \t")==value->npos : SearchQuery(*value).empty())
            resetSearch(target);
        else
            searches[target] = *value;

        switch (target)
        {
            case View::Files: saves.index = 0; reloadSaves(); break;
            case View::Bank: bank.index = 0; reloadBank(); break;
            case View::BoxSearch: findBoxMatches(); break;
            case View::Bag: reloadBag(); break;
            case View::BagAdd: reloadBagOptions(); break;
            case View::ItemBank: reloadStoredItems(); break;
            case View::Dex: selectDexEntries(); break;
            case View::Events: filterEvents(); break;
            case View::Ribbons: openRibbons(); break;
            case View::Snapshots: snapshots.index = 0; loadSnapshots(); break;
            case View::CrossSource: openCrossSources(); break;
            case View::Hall: reloadHall(); break;
            case View::Engine: enginePages.back().selected=0; filterEngine(); break;
            case View::BoxAudit: reloadBoxAudit(); break;
            case View::ParkJournal: reloadJournal(); break;
            case View::BagPresets:
                bagPresets.index = 0;
                bagPresets.load(forge::BagPresets, false);
                filterFiles(bagPresets, View::BagPresets);
                break;
            default: break;
        }
    }

    void handleEditorChoice(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Editor;
        else if ((keys & HidNpadButton_A) && !editorChoices.empty())
        {
            const auto field = fields[editRow];
            auto draft = edit->clone();
            forge::setFieldOption(*draft, field, editorChoices[editorChoice]);
            keepEditorDraft(std::move(draft), field.kind);
            view = View::Editor;
        }
        else
            scroll(keys, editorChoice, editorChoices.size());
    }

    void keepEditorDraft(std::unique_ptr<PKX> draft, forge::Field::Kind kind)
    {
        if (std::ranges::equal(edit->rawData(), draft->rawData()))
            return;
        edit = std::move(draft);
        editDirty = true;
        using enum forge::Field::Kind;
        editedMoves |= kind == Move;
        editedStats |= kind == Level || kind == Nature || kind == IV || kind == EV ||
            kind == HyperTraining || kind == Form || kind == SecondaryStat ||
            (kind == Shiny && edit->generation() <= pksm::Generation::TWO);
    }

    void handleRibbons(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Editor;
        else if ((keys & HidNpadButton_A) && !ribbons.empty())
        {
            const auto ribbon = ribbons[ribbonRow];
            edit->ribbon(ribbon, !edit->ribbon(ribbon));
            editDirty = true;
        }
        else
            scroll(keys, ribbonRow, ribbons.size());
    }

    void handleEditor(u64 keys)
    {
        if (keys & HidNpadButton_B)
        {
            edit.reset();
            view = View::Detail;
        }
        else if (keys & HidNpadButton_A)
        {
            if (editDirty)
            {
                if (editedMoves)
                    edit->fixMoves();
                edit->refreshChecksum();
                if (detailFrom == View::Party)
                {
                    forge::editPartyMember(save, party, *edit, editedStats);
                    detail = save.data->pkm(party);
                }
                else
                {
                    if (editedStats && edit->isParty())
                    {
                        edit->updatePartyData();
                        edit->refreshChecksum();
                    }
                    save.data->pkm(*edit, box, slot, false);
                    detail = save.data->pkm(box, slot);
                }
                save.dirty = true;
                status = "Pokemon edits applied. Press + to export.";
            }
            edit.reset();
            view = View::Detail;
        }
        else if (keys & HidNpadButton_ZL)
            openRibbons();
        else if (keys & (HidNpadButton_X | HidNpadButton_ZR))
        {
            const bool nickname = keys & HidNpadButton_X;
            const auto gen = edit->generation();
            const bool japanese = edit->language() == Language::JPN;
            const int length = gen <= pksm::Generation::TWO ? (japanese ? 5 : nickname ? 10 : 7) :
                gen == pksm::Generation::THREE ? (nickname ? (japanese ? 5 : 10) : (japanese ? 5 : 7)) :
                gen <= pksm::Generation::FIVE ? (nickname ? 10 : 7) : 12;
            if (auto value = keyboardText(nickname ? "Pokemon nickname" : "Original trainer",
                nickname ? edit->nickname() : edit->otName(), length))
            {
                if (nickname)
                {
                    edit->nickname(*value);
                    edit->nicknamed(true);
                }
                else
                    edit->otName(*value);
                editDirty = true;
            }
        }
        else if (keys & (HidNpadButton_Up | HidNpadButton_Down))
            editRow = cycle(editRow, keys & HidNpadButton_Up ? -1 : 1, fields.size());
        else if (const int delta = adjustment(keys); delta)
        {
            auto draft = edit->clone();
            forge::adjustField(*save.data, *draft, fields[editRow], delta, mode == 1);
            keepEditorDraft(std::move(draft), fields[editRow].kind);
        }
    }

    static int adjustment(u64 keys)
    {
        if (keys & HidNpadButton_Left) return -1;
        if (keys & HidNpadButton_Right) return 1;
        if (keys & HidNpadButton_L) return -10;
        if (keys & HidNpadButton_R) return 10;
        return 0;
    }

    void handleBag(u64 keys)
    {
        if (keys & HidNpadButton_B)
        {
            view = View::Tools;
            return;
        }
        const auto pouches = save.data->pouches();
        if (keys & (HidNpadButton_L | HidNpadButton_R))
        {
            bagPouch = cycle(bagPouch, keys & HidNpadButton_L ? -1 : 1, pouches.size());
            reloadBag();
            return;
        }
        if (save.writable && (keys & HidNpadButton_Y))
        {
            bagAddCount = 1;
            reloadBagOptions();
            view = View::BagAdd;
            return;
        }
        if (save.writable && (keys & HidNpadButton_Minus))
        {
            bagMaxMode = 0;
            view = View::BagMax;
            return;
        }
        if (bagSlots.empty())
            return;
        const auto pouch = pouches[bagPouch].first;
        const int slotIndex = bagSlots[bagRow];
        if (keys & (HidNpadButton_Up | HidNpadButton_Down))
        {
            bagRow = cycle(bagRow, keys & HidNpadButton_Up ? -1 : 1, bagSlots.size());
            resetBagDraft();
        }
        else if (keys & HidNpadButton_X)
        {
            if (mode==2) throw std::runtime_error("Hardcore does not allow item copies.");
            if (pouch == pksm::Sav::Pouch::KeyItem)
                throw std::runtime_error("Key items stay in their original save.");
            const auto item = forge::bagSlot(*save.data, pouch, slotIndex);
            forge::storeItem({item.id, item.count});
            status = "Copied item to the Item Bank.";
        }
        else if (save.writable)
        {
            const auto item = forge::bagSlot(*save.data, pouch, slotIndex);
            if (keys & HidNpadButton_A)
            {
                if (bagDraft != item.count)
                {
                    forge::changeBagCount(save, pouch, slotIndex, bagDraft);
                    status = "Bag updated. Press + to export.";
                }
            }
            else
            {
                auto amount = adjustment(keys);
                if (keys & HidNpadButton_ZL) amount = -10;
                if (keys & HidNpadButton_ZR) amount = 10;
                bagDraft = std::clamp(static_cast<int>(bagDraft) + amount, 1,
                    std::max(1, static_cast<int>(item.limit)));
            }
        }
    }

    void handleBagAdd(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Bag;
        else if ((keys & HidNpadButton_A) && !bagOptions.empty())
        {
            const auto chosen = bagOptions[bagOption];
            const auto pouch = save.data->pouches()[chosen.pouch].first;
            forge::addBagItem(save, pouch, chosen.id, bagAddCount);
            bagPouch = chosen.pouch;
            reloadBag();
            view = View::Bag;
            status = "Item added. Press + to export.";
        }
        else if (keys & (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_ZL | HidNpadButton_ZR))
        {
            scroll(keys, bagOption, bagOptions.size());
            limitBagAddCount();
        }
        else if (const int delta = adjustment(keys); delta && !bagOptions.empty())
        {
            const auto pouch = save.data->pouches()[bagOptions[bagOption].pouch].first;
            bagAddCount = std::clamp(static_cast<int>(bagAddCount) + delta, 1,
                static_cast<int>(save.data->maxCount(pouch)));
        }
    }

    void handleBagMax(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Bag;
        else if (keys & HidNpadButton_A)
        {
            const auto pouch = save.data->pouches()[bagPouch].first;
            if (bagMaxMode == 4)
            {
                if (const auto title = keyboardText("Bag preset name", {}, 36))
                    status = "Saved preset " + forge::storeBagPreset(save, *title).name;
                return;
            }
            if (bagMaxMode == 5)
            {
                bagPresets.load(forge::BagPresets, false);
                filterFiles(bagPresets, View::BagPresets);
                view = View::BagPresets;
                return;
            }
            if (bagMaxMode == 3)
            {
                if (bagSlots.empty())
                    throw std::runtime_error("Select an item stack first.");
                forge::removeBagStack(save, pouch, bagSlots[bagRow]);
                reloadBag();
                view = View::Bag;
                status = "Stack removed. Press + to export.";
                return;
            }
            const auto changed = forge::maxBag(save, bagMaxMode == 0 ? std::optional(pouch) :
                std::nullopt, bagMaxMode == 2);
            reloadBag();
            view = View::Bag;
            status = std::to_string(changed.stacks) + " stacks updated";
            if (changed.skipped)
                status += ", " + std::to_string(changed.skipped) + " could not fit or be stored";
            status += changed.stacks ? ". Press + to export." : ".";
        }
        else if (keys & (HidNpadButton_Up | HidNpadButton_Down))
            bagMaxMode = cycle(bagMaxMode, keys & HidNpadButton_Up ? -1 : 1, 6);
    }

    void handleBagPresets(u64 keys, bool reviewing)
    {
        if (keys & HidNpadButton_B)
        {
            view = reviewing ? View::BagPresets : View::BagMax;
            return;
        }
        if (keys & HidNpadButton_A && reviewing)
        {
            forge::applyBagPreset(save, bagPresetDraft);
            reloadBag();
            view = View::Bag;
            status = "Bag preset applied. Press + to export.";
        }
        else if (keys & HidNpadButton_A && !bagPresets.files.empty())
        {
            bagPresetDraft = forge::readBagPreset(bagPresets.files[bagPresets.index], *save.data);
            view = View::BagPresetReview;
        }
        else if (!reviewing)
            scroll(keys, bagPresets.index, bagPresets.files.size());
    }

    static void scroll(u64 keys, int& row, int length, int page = 7)
    {
        if (!length)
            return;
        if (keys & HidNpadButton_Up)
            row = std::max(0, row - 1);
        if (keys & HidNpadButton_Down)
            row = std::min(length - 1, row + 1);
        if (keys & HidNpadButton_ZL)
            row = std::max(0, row - page);
        if (keys & HidNpadButton_ZR)
            row = std::min(length - 1, row + page);
    }

    void handleDex(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Tools;
        else if (keys & HidNpadButton_X)
        {
            shinyDex = !shinyDex;
            computeDex();
        }
        else if (keys & HidNpadButton_Y)
        {
            missingDex = !missingDex;
            selectDexEntries();
        }
        else if ((keys & HidNpadButton_L) && sourceWritable)
            openCrossSources();
        else if ((keys & HidNpadButton_R) && save.writable)
            view = View::DexRegister;
        else if ((keys & HidNpadButton_A) && sourceWritable && !dexEntries.empty())
        {
            ManagementAccess permission(save,mode==2 && sourceWritable);
            dexPlan = forge::planDexBox(save, dexEntries[dexRow], shinyDex);
            planRow = 0;
            view = View::DexPlan;
        }
        else
            scroll(keys, dexRow, dexEntries.size());
    }

    void handleDexPlan(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Dex;
        else if ((keys & HidNpadButton_X) && !dexPlan.moves.empty())
        {
            ManagementAccess permission(save,mode==2 && sourceWritable);
            forge::applyDexBox(save, dexPlan);
            view = View::Dex;
            status = "Living Dex box staged. Press + to export.";
        }
        else
            scroll(keys, planRow, dexPlan.moves.size());
    }

    void handleEvents(u64 keys)
    {
        if (eventFilters) {
            if (keys & (HidNpadButton_B|HidNpadButton_Y)) eventFilters=false;
            else if(keys & HidNpadButton_Up) eventFilterRow=cycle(eventFilterRow,-1,9);
            else if(keys & HidNpadButton_Down) eventFilterRow=cycle(eventFilterRow,1,9);
            else if(keys & (HidNpadButton_A|HidNpadButton_Left|HidNpadButton_Right))
                changeGiftFilter(keys&HidNpadButton_Left?-1:1);
            return;
        }
        if (keys & HidNpadButton_B) view=View::Tools;
        else if (keys & HidNpadButton_X) openSearch();
        else if (keys & HidNpadButton_Y) {eventFilters=true;eventFilterRow=0;}
        else if (keys & HidNpadButton_Plus) {album.sort=cycle(album.sort,1,album.sorts.size());filterEvents();}
        else if ((keys & HidNpadButton_A) && !album.groups.empty()) view=View::EventDetail;
        else if (!album.groups.empty() && (keys & (HidNpadButton_L|HidNpadButton_R))) {
            int direction=keys&HidNpadButton_L?-1:1;
            const auto& section=album.groups[eventRow].section;
            int next=eventRow;
            while(next+direction>=0 && next+direction<int(album.groups.size()) && album.groups[next].section==section)
                next+=direction;
            if (album.groups[next].section!=section)
                while(next>0 && album.groups[next-1].section==album.groups[next].section) --next;
            eventRow=next; eventCopy=0;
        } else {
            scroll(keys,eventRow,album.groups.size(),6); eventCopy=0;
        }
    }

    const char* eventQueueBlocker()
    {
        if (album.groups.empty())
            return "No gift selected.";
        if (save.data && sourceWritable && preferences.hardcore)
            return "Switch out of Hardcore mode to stage a gift.";
        const auto& card = eventCard();
        if (const auto* reason = forge::giftQueueBlocker(save, album.archive[card.archive]))
            return reason;
        return card.fits ? nullptr : "This copy is for a different save language. Try L/R.";
    }

    bool canInject() { return !eventQueueBlocker(); }

    void handleEventDetail(u64 keys)
    {
        if (keys & HidNpadButton_B) {view=View::Events;eventCopy=0;}
        else if (keys & (HidNpadButton_L|HidNpadButton_R))
            eventCopy=cycle(eventCopy,keys&HidNpadButton_L?-1:1,album.groups[eventRow].copies.size());
        else if ((keys & HidNpadButton_X) && canInject()) {
            const auto& card=eventCard();
            forge::injectGift(save,album.archive[card.archive]);
            album.staged.insert(card.key);
            if (album.status) filterEvents();
            view=View::Events;eventCopy=0;
            status="Gift staged. Press + from Boxes to export.";
        }
    }
    void handleTransfer(u64 keys)
    {
        if (keys & HidNpadButton_B)
        {
            transfer.reset();
            if (bankDepositReview) { bankDepositReview=false; view=View::Detail; } else enterBox();
        }
        else if ((keys & HidNpadButton_A) && mode==2)
        {
            ManagementAccess permission(save,sourceWritable);
            if (bankDepositReview) forge::moveToBank(save,detailFrom==View::Party?-1:box,detailFrom==View::Party?party:slot);
            else forge::moveFromBank(save,*clipboardSource,box,slot,*clipboard);
            bankDepositReview=false; clipboard.reset(); clipboardSource.reset(); transfer.reset(); detail.reset();
            party=std::max(0,std::min(party,partyCount()-1)); enterBox();
            status="Bank move complete. The edited save is in exports.";
        }
        else if (keys & HidNpadButton_A)
        {
            forge::putPokemon(save, box, slot, *transfer);
            transfer.reset();
            enterBox();
            status = "Converted Pokemon copied to the box. Press + to export.";
        }
        else
            scroll(keys, transferRow, transferChanges.size(), 5);
    }

    void loadBankPage(int first, int end)
    {
        if (bankPage == first)
            return;
        for (auto& card : bankCards)
            card.reset();
        for (int i = first; i < end; ++i)
        {
            try
            {
                bankCards[i - first] = forge::bankPokemon(bank.files[i]);
            }
            catch (const std::exception&)
            {
                // Keep invalid files visible so inspecting one can show its diagnostic.
            }
        }
        bankPage = first;
    }

    const ShelfGame& shelfGame(const forge::File& file)
    {
        if (const auto known = gameIcons.find(file.path); known != gameIcons.end())
            return known->second;
        ShelfGame game;
        try
        {
            const auto save = forge::openSave(file);
            game = {forge::gameArtwork(file, *save.data), forge::gameTitle(file, *save.data)};
        }
        catch (const std::exception&) {}
        return gameIcons.emplace(file.path, std::move(game)).first->second;
    }

    void renderList(Canvas& c, const Browser& list, bool isBank)
    {
        const int total = static_cast<int>(list.files.size());
        const int pageSize = isBank ? 12 : 4;
        const int first = list.index / pageSize * pageSize;
        c.text(44, 110, isBank ? "Pokemon Bank" : "Games", Font::Title);
        if (total)
            c.text(1050, 127, std::to_string(list.index + 1) + " / " + std::to_string(total),
                Font::Body, Soft);

        if (!total)
        {
            c.panel(44, 170, 1192, 265, Well);
            c.art(Art::Chrome, isBank ? 1 : 2, 640, 248, 76);
            const bool filtering = !query(view).empty();
            const auto* title = filtering ? "No matches" : isBank ? "The Bank is empty" : "No save files found";
            c.text(640 - c.measure(title, Font::Title) / 2, 298, title, Font::Title);
            const std::string_view help = filtering ? "Press ZL + ZR to change or clear the search."
                : isBank ? "Inspect a Pokemon and press Y to copy it to the Bank."
                : "Copy a save to /switch/PKForge/saves and press X to refresh.";
            c.text(640 - c.measure(help) / 2, 348, help, Font::Body, Soft);
        }
        else if (isBank)
        {
            c.panel(44, 170, 710, 405, Well);
            const int end = std::min(first + pageSize, total);
            loadBankPage(first, end);
            for (int i = first; i < end; ++i)
            {
                const int j = i - first;
                const int x = 57 + j % 4 * 173;
                const int y = 184 + j / 4 * 126;
                const bool active = i == list.index;
                touchSelection(x, y, 162, 112, bank.index, i);
                if (const auto& pk = bankCards[j])
                    tile(c, x, y, 162, 112, speciesName(*pk), speciesId(*pk), pk->shiny(), active);
                else
                    tile(c, x, y, 162, 112, label(list.files[i].name, 15), 0, false, active);
            }
            const auto& selected = bankCards[list.index - first];
            inspector(c, selected.get(), "Selected entry", selected
                ? "A  Inspect to select for paste" : "This Bank record could not be read.", true);
        }
        else
        {
            for (int i = first; i < std::min(first + pageSize, total); ++i)
            {
                const int x = 44 + (i - first) * 301;
                const bool active = i == list.index;
                const int y = active ? 165 : 174;
                touchSelection(x, y, 286, 237, saves.index, i);
                c.panel(x, y + 6, 286, 237, Band, Band, 14);
                c.panel(x, y, 286, 237, active ? rgb(0x254577) : Deck, active ? Cyan : Edge, 14);
                c.panel(x + 25, y + 22, 236, 132, Well, Grid, 10);
                const auto& game = shelfGame(list.files[i]);
                if (!c.art(Art::Game, game.artwork, x + 143, y + 88, 132))
                {
                    c.panel(x + 91, y + 42, 104, 93, rgb(0x315690), Edge, 13);
                    c.panel(x + 104, y + 54, 78, 56, rgb(0x18365d), Cyan, 6);
                    c.text(x + 104 + (78 - c.measure("SAVE", Font::Small)) / 2,
                        y + 67, "SAVE", Font::Small, Ink);
                    for (int contact = 0; contact < 5; ++contact)
                        c.rect(x + 112 + contact * 13, y + 121, 6, 5, Soft);
                }
                auto title = label(list.files[i].name, 70);
                c.text(x + 20, y + 166, title, Font::Body, Ink, 246);
                c.text(x + 20, y + 202, game.title.empty() ? "SD card save" : game.title,
                    Font::Small, active ? Cyan : Soft, 246);
                if (active)
                    c.focus(x, y, 286, 237);
            }
        }

        if (!isBank)
        {
            touchButton(44, 466, 584, 104, HidNpadButton_Y);
            touchButton(652, 466, 584, 104, HidNpadButton_X);
            c.panel(44, 466, 584, 104, Deck);
            c.art(Art::Chrome, 1, 105, 518, 58);
            c.text(152, 478, "Bank", Font::Title);
            c.text(154, 526, "Y  Open your stored Pokemon", Font::Small, Soft);
            c.panel(652, 466, 584, 104, Deck);
            c.art(Art::Chrome, 2, 713, 518, 58);
            c.text(760, 478, "Save files", Font::Title);
            c.text(762, 526, "X  Refresh files from the SD card", Font::Small, Soft);
        }
    }

    static unsigned speciesId(const PKX& pk) { return static_cast<unsigned>(pk.species()); }

    static void tile(Canvas& c, int x, int y, int w, int h, std::string_view name,
        unsigned species, bool shiny, bool selected)
    {
        const bool empty = !species && name == "-";
        c.panel(x, y, w, h, selected ? rgb(0x244778) : rgb(0x122b51),
            selected ? Cyan : Grid, 9);
        if (species)
            c.art(shiny ? Art::Shiny : Art::Species, species, x + w / 2, y + (h - 21) / 2,
                std::min(68, h - 18));
        else if (empty)
        {
            c.rect(x + w / 2 - 7, y + h / 2, 14, 2, Edge);
            c.rect(x + w / 2 - 1, y + h / 2 - 6, 2, 14, Edge);
        }
        if (!empty)
        {
            const int left = std::max(x + 5, x + (w - c.measure(name, Font::Small)) / 2);
            c.text(left, y + h - 27, name, Font::Small, selected ? Ink : Soft, w - 10);
        }
        if (selected)
            c.focus(x, y, w, h);
    }

    static void field(Canvas& c, int y, std::string_view name, std::string_view value,
        int x = 797, int width = 421)
    {
        c.panel(x, y, width, 39, rgb(0x14294f), Grid, 7);
        c.text(x + 12, y + 7, name, Font::Small, Soft, 150);
        c.text(x + 165, y + 3, value, Font::Body, Ink, width - 178);
    }

    static void drawPortrait(Canvas& c, const PKX& pk, int x, int y, int size, bool home)
    {
        const auto id = speciesId(pk);
        const auto form = pk.alternativeForm();
        // The generation-six Pikachu outfits use different form IDs than later caps.
        const bool cosplay = id == 25 && form != 0 && pk.generation() == pksm::Generation::SIX;
        const auto sweet = id == 869 ? std::min<u32>(6, pk.formArgument()) : 0;
        if (home && c.portrait(pk.egg() ? PortraitLook{} : PortraitLook{id, form, sweet,
                pk.shiny(), pk.gender() == pksm::Gender::Female, cosplay}, x, y, size))
            return;
        c.art(pk.shiny() ? Art::Shiny : Art::Species, id, x, y, size);
    }

    void inspector(Canvas& c, const PKX* pk, std::string_view heading, std::string_view hint,
        bool home = false)
    {
        c.panel(778, 170, 458, 405, Deck);
        c.text(800, 184, heading, Font::Small, Cyan, 412);
        if (!pk)
        {
            c.text(800, 231, "Empty slot", Font::Title);
            c.text(800, 510, hint, Font::Small, Soft, 413);
            return;
        }
        const auto species = speciesName(*pk);
        c.panel(797, 219, 132, 124, Well);
        if (view == View::Box || view == View::Party || view == View::Bank)
            touchButton(797, 219, 421, 124, HidNpadButton_A);
        drawPortrait(c, *pk, 863, 281, 116, home);
        c.text(944, 227, species, Font::Title, Ink, 270);
        c.text(946, 279, "Lv. " + std::to_string(pk->level()) +
            "     #" + std::to_string(speciesId(*pk)), Font::Body, Soft, 265);
        if (pk->shiny())
            c.text(946, 315, "SHINY", Font::Small, Gold);
        field(c, 356, "Nickname", label(pk->nickname(), 48));
        field(c, 401, "Friendship", std::to_string(pk->currentFriendship()));
        field(c, 446, "Held item ID", std::to_string(pk->heldItem()));
        c.text(799, 521, hint, Font::Small, Soft, 411);
    }

    void renderBox(Canvas& c)
    {
        const auto title = label(save.data->boxName(box), 48);
        c.text(44, 110, "Boxes", Font::Title);
        c.panel(236, 114, 518, 45, rgb(0x28538f), Edge, 9);
        c.text(254, 117, "<  " + std::to_string(box + 1) + " / " + std::to_string(boxCount()) +
            "     " + (title.empty() || title.find_first_not_of('?') == title.npos
                ? "Box " + std::to_string(box + 1) : title), Font::Body, Ink, 444);
        c.text(724, 117, ">", Font::Body);
        touchButton(236, 108, 90, 55, HidNpadButton_L);
        touchButton(690, 108, 64, 55, HidNpadButton_R);
        c.panel(44, 170, 710, 405, Well);
        for (int i = 0; i < slots(); ++i)
        {
            const int x = 57 + i % 6 * 115;
            const int y = 183 + i / 6 * 77;
            touchSelection(x, y, 105, 69, slot, i);
            tile(c, x, y, 105, 69, names[i], species[i], rare[i], i == slot);
        }
        const auto selected = save.data->pkm(box, slot);
        const auto heading = "BOX " + std::to_string(box + 1) + "   /   SLOT " + std::to_string(slot + 1);
        inspector(c, occupied(*selected) ? selected.get() : nullptr, heading,
            clipboard ? "Bank selection: " + speciesName(*clipboard) :
                (occupied(*selected) ? "A  Open Pokemon details" : "Select a Pokemon to inspect it."));
    }

    void renderParty(Canvas& c)
    {
        c.text(44, 110, "Party", Font::Title);
        c.panel(44, 170, 710, 405, Well);
        const int count = partyCount();
        for (int i = 0; i < count; ++i)
        {
            const auto pk = save.data->pkm(i);
            touchSelection(61 + i % 3 * 229, 188 + i / 3 * 188, 213, 168, party, i);
            tile(c, 61 + i % 3 * 229, 188 + i / 3 * 188, 213, 168,
                speciesName(*pk), speciesId(*pk), pk->shiny(), i == party);
        }
        if (!count)
            c.text(75, 205, "This save has an empty party.", Font::Body, Soft);
        const auto pk = count ? save.data->pkm(party) : nullptr;
        inspector(c, pk.get(), "PARTY", "A  Open Pokemon details", true);
    }

    void renderDetail(Canvas& c)
    {
        c.text(44, 110, "Pokemon summary", Font::Title);
        c.panel(44, 170, 710, 405, Well);
        c.panel(78, 200, 286, 286, rgb(0x153661), Edge, 18);
        c.round(109, 231, 224, 224, 112, rgb(0x204b7a));
        c.round(124, 246, 194, 194, 97, rgb(0x285783));
        drawPortrait(c, *detail, 221, 343, 258, true);
        c.text(392, 227, speciesName(*detail), Font::Hero, Ink, 324);
        c.text(395, 304, "Level " + std::to_string(detail->level()), Font::Title, Cyan, 313);
        c.text(397, 367, detail->egg() ? "Egg" : "Pokedex #" +
            std::to_string(speciesId(*detail)), Font::Body, Soft, 290);
        if (detail->shiny())
            c.text(397, 419, "SHINY", Font::Small, Gold);
        if (detail->species() == pksm::Species::Alcremie && !detail->egg())
            c.text(397, 456, forge::fieldValue(*detail, {forge::Field::Kind::Decoration, "Sweet decoration"}),
                Font::Body, Soft, 325);
        c.panel(778, 170, 458, 405, Deck);
        c.text(800, 189, "Details", Font::Title);
        field(c, 252, "Nickname", label(detail->nickname(), 48));
        field(c, 299, "Held item ID", std::to_string(detail->heldItem()));
        const bool editable = detailEditable();
        field(c, 346, "Friendship", std::to_string(detail->currentFriendship()));
        if (editable)
            c.focus(797, 346, 421, 39);
        c.round(800, 411, 414, 13, 6, Band);
        const int width = detail->currentFriendship() * 414 / 255;
        if (width)
            c.round(800, 411, width, 13, 6, Cyan);
        c.text(800, 444, editable ? "D-pad and shoulders adjust friendship." : "Friendship",
            Font::Small, Soft, 413);
    }

    void renderTrainer(Canvas& c)
    {
        toolFrame(c, "Trainer", save.writable ? "TRAINER CARD" : "READ ONLY");
        const auto labelGender = genderDraft == pksm::Gender::Female ? "Female" : "Male";
        toolRow(c, 0, "Money", std::to_string(moneyDraft), trainerRow == 0);
        toolRow(c, 1, "Trainer ID (raw)", std::to_string(tidDraft), trainerRow == 1);
        toolRow(c, 2, "Secret ID (raw)", std::to_string(sidDraft), trainerRow == 2);
        toolRow(c, 3, "Gender", labelGender, trainerRow == 3);
        if (save.writable)
        {
            constexpr std::array labels{"Played hours", "Played minutes", "Played seconds"};
            for (int i = 0; i < 3; ++i)
                toolRow(c, i + 4, labels[i], std::to_string(timeDraft[i]), trainerRow == i + 4);
        }
        c.art(Art::Chrome, 0, 1005, 272, 88);
        c.text(802, 320, label(trainerNameDraft, 48), Font::Title, Ink, 404);
        field(c, 387, "Time played", std::to_string(save.data->playedHours()) + "h " +
            std::to_string(save.data->playedMinutes()) + "m");
        c.text(801, 449, "Trainer IDs affect Pokemon ownership.", Font::Small, Gold, 413);
        if (save.writable)
            c.text(801, 487, "X Rename   A Apply to save copy", Font::Small, Cyan, 413);
    }

    void toolFrame(Canvas& c, std::string_view title, std::string_view right)
    {
        c.text(44, 110, title, Font::Title);
        c.panel(44, 170, 710, 405, Well);
        c.panel(778, 170, 458, 405, Deck);
        c.text(800, 185, right, Font::Small, Cyan, 411);
    }

    void toolRow(Canvas& c, int i, std::string_view title, std::string_view value, bool selected)
    {
        const int y = 181 + i * 55;
        touchRow(57, y, 684, 49, i, selected);
        c.panel(57, y, 684, 49, selected ? rgb(0x244778) : rgb(0x14294f),
            selected ? Cyan : Grid, 7);
        c.text(72, y + 9, title, Font::Body, Ink, value.empty() ? 646 : 353);
        c.text(438, y + 9, value, Font::Body, selected ? Ink : Soft, 288);
        if (selected)
            c.focus(57, y, 684, 49);
    }

    static void noMatches(Canvas& c)
    {
        c.text(75, 211, "No matches", Font::Title, Ink);
        c.text(75, 270, "Press ZL + ZR to change or clear the search.", Font::Body, Soft, 640);
    }

    bool renderNoMatches(Canvas& c, View target)
    {
        if (query(target).empty())
            return false;
        noMatches(c);
        return true;
    }

    void renderBoxSearch(Canvas& c)
    {
        toolFrame(c, "Find boxed Pokemon", "ALL BOXES");
        if (boxMatches.empty())
        {
            noMatches(c);
            return;
        }
        listReview(c, boxMatch, boxMatches.size(), [&](int i)
        {
            const auto& match = boxMatches[i];
            toolRow(c, i % 7, match.name, "Box " + std::to_string(match.box + 1) +
                "  /  Slot " + std::to_string(match.slot + 1), i == boxMatch);
        });
        const auto& match = boxMatches[boxMatch];
        const auto pk = save.data->pkm(match.box, match.slot);
        drawPortrait(c, *pk, 1007, 322, 128, true);
        c.text(805, 222, std::to_string(boxMatch + 1) + " / " + std::to_string(boxMatches.size()),
            Font::Small, Soft);
        c.text(805, 416, match.nickname.empty() ? match.name : label(match.nickname, 30),
            Font::Title, Ink, 400);
    }

    void renderItemBank(Canvas& c)
    {
        toolFrame(c, "Item Bank", "STORED ITEMS");
        if (storedItems.empty())
        {
            if (renderNoMatches(c, View::ItemBank))
                return;
            c.text(75, 217, "No items stored.", Font::Title, Ink);
            c.text(75, 288, "Select an item in the Bag and press X to store a copy.",
                Font::Body, Soft, 620);
            return;
        }
        const int first = storedRow / 7 * 7;
        for (int i = first; i < std::min(first + 7, static_cast<int>(storedItems.size())); ++i)
            toolRow(c, i - first, bagItemName(storedItems[i].id),
                "x" + std::to_string(storedItems[i].count), storedRow == i);
        const auto& item = storedItems[storedRow];
        c.text(802, 228, bagItemName(item.id), Font::Title, Ink, 410);
        c.text(802, 289, "Quantity  " + std::to_string(item.count), Font::Body, Soft, 410);
        c.text(802, 442, save.writable ? "A  Copy to compatible pouch" :
            "Open an editable save to copy items.", Font::Small, Cyan, 410);
    }

    void renderPark(Canvas& c, bool walking)
    {
        toolFrame(c, walking ? "Pokewalker" : "Pokepark", walking ? "WALK ROUTE" : "RESIDENT");
        c.panel(57, 182, 684, 380, rgb(0x274f55), rgb(0x568273), 13);
        c.gradient(60, 205, 678, 333, rgb(0x30535c), rgb(0x386660));
        c.round(176, 294, 470, 155, 76, rgb(0x4a6b61));
        c.round(210, 314, 400, 115, 57, rgb(0x315a57));
        for (const int x : {94, 131, 682, 710})
            for (const int y : {222, 488})
            {
                c.rect(x - 2, y + 24, 7, 17, rgb(0x6f5650));
                c.round(x - 12, y + 4, 26, 26, 13, rgb(0x5e9876));
                c.round(x - 9, y, 20, 20, 10, rgb(0x78ad81));
            }
        if (residents.empty())
        {
            c.text(93, 241, "No residents yet", Font::Title, Ink, 605);
            c.text(93, 312, "Inspect a Pokemon and press ZL to add a copy.",
                Font::Body, Ink, 605);
            return;
        }
        if (!walking)
        {
            const int first = residentRow / 6 * 6;
            for (int i = first; i < std::min(first + 6, static_cast<int>(residents.size())); ++i)
            {
                try
                {
                    const auto pk = forge::bankPokemon(residents[i].pokemon);
                    const int x = 157 + (i - first) % 3 * 223;
                    const int y = 287 + (i - first) / 3 * 166;
                    if (i == residentRow)
                        c.focus(x - 44, y - 47, 88, 93);
                    c.art(pk->shiny() ? Art::Shiny : Art::Species, speciesId(*pk), x, y, 84);
                }
                catch (const std::exception&) {}
            }
        }
        const auto& resident = residents[residentRow];
        try
        {
            const auto pk = forge::bankPokemon(resident.pokemon);
            if (walking)
                c.art(pk->shiny() ? Art::Shiny : Art::Species, speciesId(*pk), walkX, walkY, 88);
            c.text(800, 230, label(speciesName(*pk), 45), Font::Title, Ink, 414);
        }
        catch (const std::exception&)
        {
            c.text(800, 230, "Unreadable resident", Font::Title, Ink, 414);
        }
        c.text(801, 284, std::to_string(residentRow + 1) + " / " +
            std::to_string(residents.size()), Font::Body, Soft);
        if (walking)
        {
            field(c, 345, "Route steps", std::to_string(walkSteps));
            field(c, 393, "Saved steps", std::to_string(resident.steps));
            c.text(801, 486, "Move around the route with the D-pad.", Font::Small, Soft, 414);
        }
        else
        {
            field(c, 343, "Snacks", std::to_string(resident.feeds));
            field(c, 389, "Play sessions", std::to_string(resident.plays));
            field(c, 435, "Walk steps", std::to_string(resident.steps));
        }
    }

    void renderTools(Canvas& c)
    {
        toolFrame(c, "Save tools", "PKFORGE");
        static constexpr std::array labels{"Bag", "Item Bank", "Living Dex", "Mystery Gifts", "Pokepark",
            "Pokewalker", "Box tools", "Snapshots", "Showdown", "Editing modes", "Day Care and breeding", "Hall of Fame", "GBA clock", "Advanced save tools"};
        static constexpr std::array descriptions{"Browse pouches and edit quantities.",
            "Store and copy compatible items across games.",
            "Track species from the Bank and open save.", "Browse the offline Wonder Card catalog.",
            "Spend time with your park residents.", "Take a resident on a controller-driven walk.",
            "Select, organize, and audit boxed Pokemon.", "Name, compare, and restore save copies.",
            "Share text teams and import a set.", "Choose Standard, HaX, or Hardcore.",
            "Inspect parents, breed eggs and hatch Pokemon.",
            "View champion teams from GBA saves.", "Review or adjust the Hoenn save clock.",
            "Trainer profiles, encounters, legality and RNG."};
        const int first = tool / 7 * 7;
        for (int i = first; i < std::min(first + 7, static_cast<int>(labels.size())); ++i)
            toolRow(c, i - first, labels[i], "", tool == i);
        c.text(800, 225, labels[tool], Font::Title, Ink, 410);
        c.text(800, 288, descriptions[tool], Font::Small, Soft, 412);
        c.art(Art::Chrome, tool == 0 ? 2 : tool == 1 ? 1 : 0, 1004, 432, 150);
    }

    void renderBoxTools(Canvas& c)
    {
        toolFrame(c, "Box tools", "MULTI-SELECT");
        const int first = boxToolSlot / 7 * 7;
        for (int i = first; i < std::min(first + 7, slots()); ++i)
            toolRow(c, i - first, std::to_string(i + 1) + "   " + names[i],
                marked[i] ? "Marked" : "", boxToolSlot == i);
        c.text(803, 223, "Box " + std::to_string(box + 1), Font::Title);
        c.text(803, 278, std::to_string(markedSlots().size()) + " marked", Font::Body, Cyan);
        c.text(803, 332, "Y toggles an occupied slot.", Font::Small, Soft);
        c.text(803, 366, "X marks every occupied slot.", Font::Small, Soft);
    }

    void renderBoxActions(Canvas& c)
    {
        toolFrame(c, "Box actions", "BOX " + std::to_string(box + 1));
        const int first = actionsRow / 7 * 7;
        for (int i = first; i < std::min(first + 7, static_cast<int>(boxOperations.size())); ++i)
            toolRow(c, i - first, boxOperations[i], i == 5 ? std::to_string(wallpaperDraft + 1) : "",
                i == actionsRow);
        c.text(802, 228, std::to_string(markedSlots().size()) + " marked", Font::Title);
        if (actionsRow == 5)
            c.text(804, 298, "Left / right chooses a base wallpaper.", Font::Small, Soft, 413);
        else
            c.text(804, 298, "Apply changes to the working save.", Font::Small, Soft, 413);
    }

    void renderBoxReview(Canvas& c)
    {
        toolFrame(c, "Review box operation", actionsRow == 6 ? "RELEASE CONFIRMATION" : "CONFIRM");
        c.text(73, 218, boxOperations[actionsRow], Font::Title, Ink, 635);
        c.text(73, 282, std::to_string(markedSlots().size()) + " marked in Box " +
            std::to_string(box + 1), Font::Body, Soft);
        if (actionsRow == 1)
        {
            const int unlocked = std::clamp<int>(save.data->unlockedBoxes(), 1, boxCount());
            c.text(73, 327, unlocked > 1 ? "Destination box " + std::to_string((box + 1) % unlocked + 1) :
                "Unlock a second box first.", Font::Body, Cyan);
        }
        c.text(804, 229, actionsRow == 6 ? "Release these Pokemon?" : "Apply this operation?",
            Font::Body, Ink, 411);
        c.text(804, 288, "Your original save stays on the SD card.", Font::Small, Soft, 412);
    }

    void renderBoxAudit(Canvas& c)
    {
        toolFrame(c, "Box compatibility audit", "BASIC CHECKS");
        listReview(c, issuesRow, issues.size(), [&](int i)
        {
            toolRow(c, i % 7, "Slot " + std::to_string(issues[i].slot), issues[i].message, i == issuesRow);
        });
        if (issues.empty())
            renderNoMatches(c, View::BoxAudit);
        c.text(802, 226, std::to_string(issues.size()) + " findings", Font::Title, Ink, 412);
        if (!issues.empty())
            c.text(804, 289, issues[issuesRow].message, Font::Small, Gold, 411);
        c.text(804, 355, "Encounter legality is not checked.", Font::Small, Soft, 411);
    }

    template<class Draw>
    static void listReview(Canvas&, int row, size_t count, Draw draw)
    {
        const int first = row / 7 * 7;
        for (int i = first; i < std::min(first + 7, static_cast<int>(count)); ++i)
            draw(i);
    }

    void renderSnapshots(Canvas& c)
    {
        toolFrame(c, "Save snapshots", "RESTORE POINTS");
        listReview(c, snapshots.index, snapshots.files.size(), [&](int i)
        {
            toolRow(c, i % 7, label(snapshots.files[i].name, 88), "", i == snapshots.index);
        });
        if (snapshots.files.empty() && !renderNoMatches(c, View::Snapshots))
            c.text(74, 217, "No saved snapshots yet.", Font::Body, Soft);
        c.text(804, 228, save.data ? "X  Name the current snapshot" : "Open a save to make a snapshot.",
            Font::Body, Ink, 411);
        c.text(804, 293, "A  Compare or restore a separate copy", Font::Small, Soft, 411);
    }

    void renderSnapshotReview(Canvas& c)
    {
        toolFrame(c, "Snapshot review", "COMPARED WITH OPEN SAVE");
        c.text(74, 219, label(snapshots.files[snapshots.index].name, 80), Font::Title, Ink, 632);
        c.text(74, 283, i18n::game(Language::ENG, snapshotGame), Font::Body, Soft, 632);
        if (snapshotDiff)
        {
            field(c, 235, "Party slots", std::to_string(snapshotDiff->party) + " changed");
            field(c, 283, "Box slots", std::to_string(snapshotDiff->boxes) + " changed");
            field(c, 331, "Money delta", std::to_string(snapshotDiff->money));
            field(c, 379, "Trainer data", snapshotDiff->trainer ? "Changed" : "Same");
        }
        else
            c.text(804, 228, "Open a save for the same game to compare.", Font::Small, Soft, 413);
        c.text(804, 492, "Restoring writes an independent save copy.", Font::Small, Cyan, 414);
    }

    void renderCrossSources(Canvas& c)
    {
        toolFrame(c, "Living Dex source save", "CROSS-SAVE PLAN");
        listReview(c, donors.index, donors.files.size(), [&](int i)
        {
            toolRow(c, i % 7, label(donors.files[i].name, 88), "", i == donors.index);
        });
        if (donors.files.empty() && !renderNoMatches(c, View::CrossSource))
            c.text(74, 217, "No other save copies in the saves folder.", Font::Body, Soft, 628);
        c.text(803, 236, "Choose the source save.", Font::Body, Ink, 413);
        c.text(803, 295, "The open save is the destination.", Font::Small, Soft, 413);
    }

    void renderCrossReview(Canvas& c)
    {
        toolFrame(c, "Cross-save move plan", "EXPORT BOTH COPIES");
        listReview(c, crossRow, crossPlan.moves.size(), [&](int i)
        {
            const auto& move = crossPlan.moves[i];
            toolRow(c, i % 7, pksm::Species(move.species).localize(Language::ENG),
                "Slot " + std::to_string(move.targetSlot + 1), i == crossRow);
        });
        if (crossPlan.moves.empty())
            c.text(74, 217, "No compatible boxed Pokemon in this range.", Font::Body, Soft, 628);
        c.text(803, 227, std::to_string(crossPlan.moves.size()) + " planned moves", Font::Title, Ink, 414);
        c.text(803, 291, "To Box " + std::to_string(crossPlan.targetBox + 1), Font::Body, Cyan);
        c.text(803, 352, "The export includes source and destination.", Font::Small, Soft, 414);
        c.text(803, 388, "Both input files stay intact.", Font::Small, Soft, 414);
    }

    void renderDexRegister(Canvas& c)
    {
        toolFrame(c, "Register owned Pokemon", "SAVE POKEDEX");
        c.text(72, 221, "Add party and box species as caught", Font::Title, Ink, 633);
        c.text(72, 288, "Eggs are skipped.", Font::Body, Soft);
        c.text(803, 227, "Caught now", Font::Body, Soft);
        c.text(803, 265, std::to_string(save.data->dexCaught()), Font::Hero, Cyan);
        c.text(803, 388, "Changes require an export.", Font::Small, Soft);
    }

    void renderShowdown(Canvas& c)
    {
        toolFrame(c, "Showdown", "TEAM TEXT AND QR");
        if (qr[0])
        {
            const auto side = qrcodegen_getSize(qr.data());
            const int unit = 360 / (side + 8);
            const int total = (side + 8) * unit;
            const int x = 399 - total / 2;
            const int y = 372 - total / 2;
            c.rect(x, y, total, total, Ink);
            for (int row = 0; row < side; ++row)
                for (int col = 0; col < side; ++col)
                    if (qrcodegen_getModule(qr.data(), col, row))
                        c.rect(x + (col + 4) * unit, y + (row + 4) * unit, unit, unit, 0);
        }
        else
            c.text(75, 218, "Choose a party or boxed Pokemon to share.", Font::Body, Soft, 633);
        c.text(803, 230, "A  Current party", Font::Body, Ink);
        c.text(803, 273, "Y  Selected boxed Pokemon", Font::Body, Ink);
        c.text(803, 316, "X  Save the displayed text", Font::Body, Ink);
        c.text(803, 415, "Import up to six sets from imports/showdown.txt.", Font::Small, Soft, 413);
        c.text(803, 447, "Choose an empty box slot before importing.", Font::Small, Soft, 413);
    }

    void renderShowdownReview(Canvas& c)
    {
        toolFrame(c, "Showdown import preview", "DESTINATION");
        for (int i = 0; i < static_cast<int>(showdownDraft.size()); ++i)
        {
            const auto& entry = showdownDraft[i];
            toolRow(c, i, speciesName(*entry.pokemon),
                "Box " + std::to_string(entry.box + 1) + " / Slot " + std::to_string(entry.slot + 1),
                i == showdownRow);
        }
        const auto& draft = *showdownDraft[showdownRow].pokemon;
        c.text(804, 218, speciesName(draft) + "  Lv. " + std::to_string(draft.level()), Font::Title, Ink, 413);
        if (draft.alternativeForm())
            c.text(804, 258, i18n::form(Language::ENG, save.data->version(), draft.species(),
                draft.alternativeForm()), Font::Small, Cyan, 414);
        for (int i = 0; i < 4; ++i)
            if (draft.move(i) != pksm::Move::None)
                c.text(804, 285 + 34 * i, draft.move(i).localize(Language::ENG), Font::Small, Soft, 413);
        c.text(804, 465, "Review encounter details before use.", Font::Small, Gold, 414);
    }

    void renderModes(Canvas& c)
    {
        toolFrame(c, "Editing modes", "SAVED PREFERENCES");
        constexpr std::array labels{"Standard", "HaX", "Hardcore"};
        constexpr std::array descriptions{"Keep the ordinary stat limits.",
            "Expand editor choices and allow raw stats.", "Allow moves, releases, backups and exports."};
        for (int i = 0; i < 3; ++i)
            toolRow(c, i, labels[i], (i==0 ? mode==0 : i==1 ? preferences.hax : preferences.hardcore) ? "Active" : "", i == modeRow);
        c.text(804, 226, labels[modeRow], Font::Title, Ink, 412);
        c.text(804, 290, descriptions[modeRow], Font::Small, Soft, 412);
    }


    void renderJournal(Canvas& c)
    {
        const auto pk = forge::bankPokemon(residents[residentRow].pokemon);
        toolFrame(c, "Park journal", speciesName(*pk));
        listReview(c, journalRow, journal.size(), [&](int i)
        {
            const auto& event = journal[i];
            toolRow(c, i % 7, journalAction(event), journalDate(event), i == journalRow);
        });
        if (journal.empty() && !renderNoMatches(c, View::ParkJournal))
            c.text(74, 218, "No care or walk entries yet.", Font::Body, Soft);
        c.art(pk->shiny() ? Art::Shiny : Art::Species, speciesId(*pk), 1007, 306, 136);
        c.text(803, 408, std::to_string(journal.size()) + " saved activities", Font::Body, Soft);
    }

    void renderNursery(Canvas& c, bool review)
    {
        toolFrame(c, review ? "Review withdrawal" : "Nursery", "BRILLIANT DIAMOND / SHINING PEARL");
        for (int i = 0; i < 2; ++i)
        {
            const auto& pk = *nurseryView.parents[i];
            toolRow(c, i, occupied(pk) ? speciesName(pk) : "Empty", occupied(pk) ?
                "Lv. " + std::to_string(pk.level()) : "", parentRow == i);
        }
        const auto& pk = *nurseryView.parents[parentRow];
        if (occupied(pk))
            c.art(pk.shiny() ? Art::Shiny : Art::Species, speciesId(pk), 1006, 285, 122);
        c.text(803, 361, nurseryView.eggReady ? "An Egg is waiting" : "No Egg waiting", Font::Title, Ink, 415);
        const auto destination = "Box " + std::to_string(box + 1) + " / Slot " + std::to_string(slot + 1);
        if (review)
            c.text(804, 421, "Withdraw to " + destination, Font::Body, Cyan, 412);
        else if (nurseryView.eggReady)
            c.text(804, 421, "Collect it in the game.", Font::Small, Soft, 412);
        else if (save.writable)
            c.text(804, 421, "Selected destination  " + destination, Font::Small, Soft, 412);
    }

    void handleGbaClock(u64 keys)
    {
        if (keys & HidNpadButton_B)
            view = View::Tools;
        else if (keys & (HidNpadButton_Up | HidNpadButton_Down))
            clockRow = cycle(clockRow, keys & HidNpadButton_Up ? -1 : 1, 8);
        else if (keys & (HidNpadButton_ZL | HidNpadButton_ZR))
            clockRow = (clockRow + 4) % 8;
        else if (save.writable)
        {
            if (keys & HidNpadButton_A)
            {
                forge::applyGbaClock(save, clockDraft);
                view = View::Tools;
                if (save.dirty)
                    status = "Clock changes applied. Press + to export.";
            }
            else if (keys & HidNpadButton_X)
                clockDraft.fill(0);
            else if (keys & HidNpadButton_Y)
                clockDraft[4] = std::max(734u, clockDraft[4]);
            else if (const int delta = adjustment(keys); delta)
            {
                constexpr std::array limits{65535, 23, 59, 59};
                auto& value = clockDraft[clockRow];
                value = std::clamp(int(value) + delta, 0, limits[clockRow % 4]);
            }
        }
    }

    void renderGbaClock(Canvas& c)
    {
        const int start = clockRow / 4 * 4;
        toolFrame(c, start ? "Elapsed clock" : "Initial clock", forge::gameTitle(save.file, *save.data));
        constexpr std::array labels{"Days", "Hours", "Minutes", "Seconds"};
        for (int i = 0; i < 4; ++i)
            toolRow(c, i, labels[i], std::to_string(clockDraft[i + start]), clockRow == i + start);
        c.text(804, 228, save.writable ? "Clock values" : "Clock values (read-only)", Font::Title, Ink, 414);
        c.text(804, 292, "ZL / ZR switches clock pages.", Font::Small, Soft, 414);
        if (save.writable)
        {
            c.text(804, 335, "X sets both clocks to zero.", Font::Small, Soft, 414);
            c.text(804, 373, "Y advances elapsed days to at least 734.", Font::Small, Soft, 414);
        }
    }

    void renderHall(Canvas& c)
    {
        toolFrame(c, "Hall of Fame", forge::gameTitle(save.file, *save.data));
        listReview(c, hallRow, hall.size(), [&](int i)
        {
            const auto& team = hall[i];
            toolRow(c, i % 7, "Record " + std::to_string(team.record), "", hallRow == i);
        });
        if (hall.empty())
        {
            if (!renderNoMatches(c, View::Hall))
                c.text(74, 218, "No teams are recorded in this save.", Font::Body, Soft);
            return;
        }
        const auto& team = hall[hallRow];
        for (size_t i = 0; i < team.members.size(); ++i)
        {
            const auto& member = team.members[i];
            const int x = 830 + (i % 2) * 210;
            const int y = 235 + (i / 2) * 115;
            c.art(member.shiny ? Art::Shiny : Art::Species, unsigned(member.species), x + 54, y + 24, 66);
            c.text(x, y + 58, member.species.localize(Language::ENG), Font::Small, Ink, 148);
            c.text(x, y + 82, "Lv. " + std::to_string(member.level), Font::Small, Soft, 120);
        }
    }

    void renderRibbons(Canvas& c)
    {
        toolFrame(c, "Ribbons and marks", speciesName(*edit));
        listReview(c, ribbonRow, ribbons.size(), [&](int i)
        {
            const auto ribbon = ribbons[i];
            const auto& name = ribbon.localize(Language::ENG);
            toolRow(c, i % 7, name.empty() ? "Ribbon " + std::to_string(static_cast<unsigned>(ribbon)) : label(name, 64),
                edit->ribbon(ribbon) ? "Present" : "", i == ribbonRow);
        });
        if (ribbons.empty() && !renderNoMatches(c, View::Ribbons))
            c.text(74, 218, "This format has no editable ribbon fields.", Font::Body, Soft, 632);
        c.text(804, 230, "A  Toggle selected ribbon", Font::Body, Ink, 412);
        c.text(804, 280, "Apply from the Pokemon editor when finished.", Font::Small, Soft, 412);
    }

    void renderEditor(Canvas& c)
    {
        toolFrame(c, "Pokemon editor", editDirty ? "UNAPPLIED EDITS" :
            (detailFrom == View::Party ? "PARTY POKEMON" : "BOXED POKEMON"));
        const int first = editRow / 7 * 7;
        for (int i = first; i < std::min(first + 7, static_cast<int>(fields.size())); ++i)
            toolRow(c, i - first, fields[i].label, label(forge::fieldValue(*edit, fields[i]), 50),
                editRow == i);
        drawPortrait(c, *edit, 1008, 285, 130, true);
        c.text(802, 350, speciesName(*edit), Font::Title, Ink, 414);
        c.text(804, 400, "Compatibility checks", Font::Body, Soft);
        const auto notes = forge::compatibility(*save.data, *edit);
        if (notes.empty())
            c.text(804, 436, "Basic checks passed.", Font::Small, Cyan, 414);
        for (size_t i = 0; i < notes.size() && i < 3; ++i)
            c.text(804, 436 + i * 26, notes[i], Font::Small, Gold, 414);
        if (fields[editRow].kind == forge::Field::Kind::Shiny)
            c.text(804, 540, edit->generation() <= pksm::Generation::TWO
                ? "Left/Right toggles shininess by changing DVs."
                : "Left/Right toggles shininess.", Font::Small, Cyan, 414);
        else
            c.text(804, 540, "ZL  Ribbons and marks", Font::Small, Cyan, 414);
    }

    void renderEditorChoice(Canvas& c)
    {
        const auto field = fields[editRow];
        toolFrame(c, field.label, speciesName(*edit));
        if (editorChoices.empty())
        {
            noMatches(c);
            return;
        }
        listReview(c, editorChoice, editorChoices.size(), [&](int i)
        {
            const auto id = editorChoices[i];
            const auto name = forge::fieldOptionName(*edit, field, id);
            toolRow(c, i % 7, name.empty() ? "#" + std::to_string(id) : label(name, 64), "",
                i == editorChoice);
        });
        c.text(804, 232, std::to_string(editorChoice + 1) + " / " + std::to_string(editorChoices.size()),
            Font::Title, Ink);
        c.text(804, 294, "Current value", Font::Body, Soft);
        c.text(804, 331, label(forge::fieldValue(*edit, field), 45), Font::Body, Ink, 414);
        c.text(804, 465, "A selects a value for the editor draft.", Font::Small, Cyan, 414);
    }

    std::string bagItemName(unsigned id) const
    {
        const auto& name = i18n::item(Language::ENG, id);
        return name.empty() ? "Item #" + std::to_string(id) : label(name, 48);
    }

    void renderBag(Canvas& c)
    {
        const auto pouches = save.data->pouches();
        const auto [pouch, count] = pouches[bagPouch];
        toolFrame(c, "Bag   /   " + std::string(forge::pouchName(pouch)), "POUCH");
        c.text(804, 230, forge::pouchName(pouch), Font::Title, Ink, 410);
        c.text(804, 288, std::to_string(bagPouch + 1) + " / " +
            std::to_string(pouches.size()) + " pouches", Font::Body, Soft);
        if (bagSlots.empty())
        {
            if (!renderNoMatches(c, View::Bag))
                c.text(76, 209, "This pouch is empty.", Font::Body, Soft);
            return;
        }
        const int first = bagRow / 7 * 7;
        for (int i = first; i < std::min(first + 7, static_cast<int>(bagSlots.size())); ++i)
        {
            const auto item = forge::bagSlot(*save.data, pouch, bagSlots[i]);
            toolRow(c, i - first, bagItemName(item.id), "x" + std::to_string(item.count), i == bagRow);
        }
        const auto item = forge::bagSlot(*save.data, pouch, bagSlots[bagRow]);
        c.text(804, 361, bagItemName(item.id), Font::Body, Ink, 414);
        c.text(804, 404, "x" + std::to_string(bagDraft), Font::Hero, Cyan);
        c.text(804, 494, bagDraft != item.count ? "Apply to update the save copy." :
            "Maximum quantity: " + std::to_string(item.limit), Font::Small, Soft, 413);
    }

    void renderBagAdd(Canvas& c)
    {
        const auto pouches = save.data->pouches();
        toolFrame(c, "Add item", "DESTINATION POUCH");
        if (bagOptions.empty())
        {
            if (!renderNoMatches(c, View::BagAdd))
                c.text(75, 211, "No addable items in this pouch.", Font::Body, Soft);
            c.text(804, 232, "Search across all pouches", Font::Body, Cyan, 414);
            return;
        }
        const int first = bagOption / 7 * 7;
        for (int i = first; i < std::min(first + 7, static_cast<int>(bagOptions.size())); ++i)
        {
            const auto choice = bagOptions[i];
            toolRow(c, i - first, bagItemName(choice.id), forge::pouchName(pouches[choice.pouch].first),
                i == bagOption);
        }
        const auto selected = bagOptions[bagOption];
        c.text(804, 215, forge::pouchName(pouches[selected.pouch].first), Font::Body, Soft, 414);
        c.text(804, 251, bagItemName(selected.id), Font::Title, Ink, 414);
        c.text(804, 305, "Quantity", Font::Body, Soft);
        c.text(804, 343, std::to_string(bagAddCount), Font::Hero, Cyan);
        c.text(804, 463, "Uses the selected pouch's stack limit.", Font::Small, Soft, 414);
    }

    void renderBagMax(Canvas& c)
    {
        toolFrame(c, "Bag actions", "SAVE COPY");
        constexpr std::array options{"Max existing stacks in this pouch", "Max existing stacks in every pouch",
            "Fill pouches and max stacks", "Remove the selected stack", "Save a bag preset", "Load a bag preset"};
        for (int i = 0; i < static_cast<int>(options.size()); ++i)
            toolRow(c, i, options[i], "", i == bagMaxMode);
        if (bagMaxMode >= 4)
        {
            c.text(804, 226, "Bag presets", Font::Title, Ink, 414);
            c.text(804, 292, bagMaxMode == 4 ? "Save quantities from supported pouches." :
                "Restore the preset's item quantities.", Font::Small, Soft, 414);
            return;
        }
        c.text(804, 222, bagMaxMode == 3 ? "Review removal" : "Review refill", Font::Title, Ink, 414);
        if (bagMaxMode == 3)
        {
            if (!bagSlots.empty())
            {
                const auto pouch = save.data->pouches()[bagPouch].first;
                const auto entry = forge::bagSlot(*save.data, pouch, bagSlots[bagRow]);
                c.text(804, 288, i18n::item(Language::ENG, entry.id), Font::Body, Gold, 414);
                c.text(804, 325, std::to_string(entry.count) + " in this stack", Font::Small, Soft, 414);
            }
            return;
        }
        c.text(804, 285, "Key and special items stay untouched.", Font::Small, Soft, 414);
        c.text(804, 318, "Adds permitted items where space allows.", Font::Small, Soft, 414);
        c.text(804, 386, "Press + after applying to export a copy.", Font::Small, Cyan, 414);
    }

    void renderBagPresets(Canvas& c, bool reviewing)
    {
        toolFrame(c, reviewing ? "Review bag preset" : "Bag presets", forge::gameTitle(save.file, *save.data));
        if (reviewing)
        {
            c.text(74, 232, std::to_string(bagPresetDraft.size()) + " item stacks", Font::Title, Ink, 640);
            c.text(74, 296, "A restores the saved quantities to the bag draft.", Font::Body, Soft, 640);
        }
        else
        {
            listReview(c, bagPresets.index, bagPresets.files.size(), [&](int i)
            {
                const auto& name = bagPresets.files[i].name;
                toolRow(c, i % 7, label(name.substr(0, name.find('/')), 38), "", i == bagPresets.index);
            });
            if (bagPresets.files.empty() && !renderNoMatches(c, View::BagPresets))
                c.text(74, 228, "Save a bag preset from Bag actions.", Font::Body, Soft, 640);
        }
        c.text(804, 240, "Applies to the same game.", Font::Small, Soft, 414);
        c.text(804, 281, "Other bag items are kept.", Font::Small, Soft, 414);
    }

    void renderDex(Canvas& c)
    {
        toolFrame(c, shinyDex ? "Shiny Living Dex" : "Living Dex", "OPEN SAVE + BANK");
        if (dexEntries.empty() && !renderNoMatches(c, View::Dex))
            c.text(75, 211, "No missing species in this view.", Font::Body, Soft);
        const int first = dexRow / 7 * 7;
        for (int i = first; i < std::min(first + 7, static_cast<int>(dexEntries.size())); ++i)
        {
            const auto id = dexEntries[i];
            const auto title = "#" + std::to_string(id) + "   " +
                pksm::Species(id).localize(Language::ENG);
            toolRow(c, i - first, label(title, 50),
                dexCounts[id] ? std::to_string(dexCounts[id]) + " owned" : "Missing", i == dexRow);
        }
        const auto tracked = std::ranges::count_if(dexCounts, [](unsigned n) { return n > 0; });
        c.text(803, 226, std::to_string(tracked) + " / 1025", Font::Hero, Cyan);
        if (!dexEntries.empty())
        {
            const auto id = dexEntries[dexRow];
            c.art(shinyDex ? Art::Shiny : Art::Species, id, 1006, 386, 152);
            c.text(805, 483, label(pksm::Species(id).localize(Language::ENG), 45),
                Font::Title, Ink, 412);
        }
    }

    void renderDexPlan(Canvas& c)
    {
        toolFrame(c, "Living Dex box plan", "SAME-SAVE MOVES");
        const int first = planRow / 7 * 7;
        for (int i = first; i < std::min(first + 7, static_cast<int>(dexPlan.moves.size())); ++i)
        {
            const auto& move = dexPlan.moves[i];
            toolRow(c, i - first, pksm::Species(move.species).localize(Language::ENG),
                "Box " + std::to_string(move.fromBox + 1) + " > Slot " + std::to_string(move.toSlot + 1),
                planRow == i);
        }
        if (dexPlan.moves.empty())
            c.text(74, 215, "No boxed Pokemon match this range.", Font::Body, Soft, 638);
        c.text(803, 221, "Box " + std::to_string(dexPlan.box + 1), Font::Title, Ink);
        c.text(803, 282, "Species #" + std::to_string(dexPlan.start) + " - #" +
            std::to_string(std::min(1025u, dexPlan.start + 29)), Font::Body, Soft);
        c.text(803, 329, std::to_string(dexPlan.moves.size()) + " planned moves", Font::Body, Cyan);
        c.text(803, 396, "Only this open save will change.", Font::Small, Soft, 415);
        c.text(803, 429, "Apply, then export a separate save copy.", Font::Small, Soft, 415);
    }

    static std::string cardNumber(unsigned value)
    {
        std::string result=std::to_string(value);
        return std::string(result.size()<4?4-result.size():0,'0')+result;
    }

    static void giftArt(Canvas& c,const forge::AlbumCard& card,int x,int y,int size)
    {
        if(card.species) c.art(card.shiny?Art::Shiny:Art::Species,card.species,x,y,size);
        else c.art(Art::Chrome,4,x,y,size);
    }

    static void giftTitle(Canvas& c, int x,int y,int width,const std::string& value)
    {
        if(c.measure(value,Font::Album)<=width) {c.text(x,y,value,Font::Album,Ink,width);return;}
        size_t split=0;
        for(size_t i=value.find(' ');i!=value.npos;i=value.find(' ',i+1)) {
            if(c.measure(std::string_view(value).substr(0,i),Font::Album)>width) break;
            split=i;
        }
        c.text(x,y,value.substr(0,split?split:value.size()),Font::Album,Ink,width);
        if(split) c.text(x,y+26,value.substr(split+1),Font::Album,Ink,width);
    }

    void renderEvents(Canvas& c)
    {
        const bool detailView=view==View::EventDetail;
        const auto plum=rgb(0x412943), well=rgb(0x191b30), row=rgb(0x23263c);
        const auto steel=rgb(0x385389), lilac=rgb(0xba98b9), faded=rgb(0xacb9d6);
        c.rect(0,0,1280,720,rgb(0x0d1021));
        c.panel(12,12,1256,696,plum,steel,12);
        c.panel(30,29,824,46,rgb(0x835371),well,6);
        c.gradient(33,32,818,36,rgb(0xa47791),rgb(0x79516b));
        c.text(43,35,"Mystery Gift",Font::Album);
        auto total=std::to_string(album.groups.size())+" cards";
        c.text(838-c.measure(total,Font::Album),35,total,Font::Album);
        c.rect(869,28,378,49,well);
        if (!eventFilters && !detailView) touchButton(869,28,378,49,HidNpadButton_X);
        auto search=query(View::Events);
        c.text(878,40,search.empty()?"Search title, Pokémon, OT, move...":search,Font::Caption,search.empty()?faded:Ink,360);
        c.rect(878,65,357,2,faded);
        auto values=filterValues();
        std::array<std::string,7> chips{"Fits save",values[1],"Shiny",values[3],"More...", "Sort: "+values[6],"Group: "+values[7]};
        int left=30;
        for(size_t i=0;i<chips.size();++i) {
            int w=c.measure(chips[i],Font::Caption)+22;
            bool on=(i==0 && album.fitsOnly)||(i==2 && album.shinyOnly)||(i==4 && (!album.language.empty()||!album.collapse));
            c.panel(left,90,w,34,on?steel:row,on?Cyan:steel,6);
            c.text(left+11,95,chips[i],Font::Caption,on?Ink:faded);
            if (!detailView && !eventFilters)
            {
                constexpr std::array filterForChip{0, 1, 2, 3, -1, 6, 7};
                if (filterForChip[i] < 0)
                    touchButton(left, 85, w, 44, HidNpadButton_Y);
                else
                    touchTargets.push_back({{left, 85, w, 44}, 0, -1, false, nullptr, 0, filterForChip[i]});
            }
            left+=w+9;
        }
        c.panel(30,139,813,492,well,steel,11);
        c.panel(858,139,389,492,rgb(0x432b46),rgb(0x94708d),15);
        c.panel(864,146,377,478,rgb(0x36273e),rgb(0x765d77),11);
        c.gradient(873,151,358,29,rgb(0x996b86),rgb(0x68435f));
        c.text(878,154,"WONDER CARD",Font::Caption);
        if (album.groups.empty()) {
            c.text(53,197,album.archive.empty()?"No local card catalog":"No matching cards",Font::Album);
            c.text(53,237,album.archive.empty()?"Install your catalog in /switch/PKForge/media/events on the SD card.":"X to change the search or Y to adjust filters.",Font::Caption,faded,737);
        } else {
            const auto& group=album.groups[eventRow];
            int start=eventRow, end=eventRow+1;
            while(start>0 && album.groups[start-1].section==group.section) --start;
            while(end<int(album.groups.size()) && album.groups[end].section==group.section) ++end;
            int first=std::clamp(eventRow-4,start,std::max(start,end-6));
            c.text(45,151,group.section,Font::Album,rgb(0xe4adbd),600);
            auto count=std::to_string(group.sectionCount)+" cards";
            int countX=824-c.measure(count,Font::Caption);
            int lineX=std::min(660,58+c.measure(group.section,Font::Album));
            if(countX>lineX+12) c.rect(lineX,170,countX-lineX-12,2,steel);
            c.text(countX,155,count,Font::Caption,faded);
            for(int i=first;i<std::min(first+6,end);++i) {
                const auto& g=album.groups[i]; const auto& card=album.cards[g.copies[0]];
                const int y=188+(i-first)*72;
                if (!detailView && !eventFilters) touchRow(40,y,786,65,i-first,i==eventRow);
                if(i==eventRow) {
                    c.panel(40,y,786,65,rgb(0x43558a),rgb(0xa8b9df),7);
                    c.gradient(43,y+3,780,58,rgb(0x52699e),rgb(0x374c83));
                } else c.round(40,y,786,65,6,row);
                c.round(45,y+4,60,57,6,plum); giftArt(c,card,75,y+32,58);
                if(card.shiny) c.text(91,y,"★",Font::Caption,Gold);
                c.text(116,y+4,card.title,Font::Album,Ink,575);
                std::string subject=card.species?card.subject+" · Lv. "+std::to_string(card.level):card.contents;
                c.text(116,y+36,subject+"  ·  No. "+cardNumber(card.number),Font::Caption,faded,570);
                std::string tag=card.language.empty()?"Any":card.language;
                if(g.copies.size()>1) tag+=" +"+std::to_string(g.copies.size()-1);
                c.text(814-c.measure(tag,Font::Caption),y+6,tag,Font::Caption,Ink);
                if(album.staged.contains(card.key)) c.text(745,y+38,"Staged",Font::Caption,Gold);
                else if(!card.language.empty() && save.data && card.language!=preferredGiftLanguage())
                    c.text(720,y+38,"Other lang.",Font::Caption,rgb(0xdd9fa8));
            }
            int barH=std::max(10,415*std::min(6,int(album.groups.size()))/int(album.groups.size()));
            c.round(832,190+(415-barH)*eventRow/std::max(1,int(album.groups.size())-1),4,barH,2,Cyan);
            const auto& card=eventCard();
            std::string id="No. "+cardNumber(card.number);
            c.text(1224-c.measure(id,Font::Caption),154,id,Font::Caption,lilac);
            c.panel(877,192,113,116,rgb(0x4e3c54),rgb(0x847087),53);
            giftArt(c,card,932,249,103);
            if(card.ballId>0 && card.ballId<=37) c.art(Art::Chrome,5+card.ballId,982,296,24,false);
            giftTitle(c,1004,193,224,card.title);
            c.text(1004,266,card.species?card.subject+" · Lv. "+std::to_string(card.level):"Item gift",Font::Caption,faded,222);
            std::string lang=card.language.empty()?"Any language":card.language;
            c.text(881,319,lang+"   "+(album.staged.contains(card.key)?"Staged":save.data && card.fits && save.writable?"Fits save":"Browse only"),Font::Caption,lilac);
            int y=355;
            auto fact=[&](const char* title,const std::string& value) {
                if(value.empty()) return;
                c.text(881,y,title,Font::Caption,faded,65);
                c.text(953,y,value,Font::Caption,Ink,273); y+=25;
            };
            fact("Date",card.date); fact("OT",card.ot+(card.tid.empty()?"":"  ID "+card.tid));
            fact("Event",card.subject.empty()?card.series:card.subject); fact("Moves",card.moves);
            fact("Item",card.species?card.item:card.contents); fact("Ball",card.ball);
            fact("Met",card.met); fact("Game",card.games);
            std::string copies; std::set<std::string> seen;
            for(auto index:group.copies) {
                auto& tag=album.cards[index].language;
                if(!tag.empty() && seen.insert(tag).second) copies+=(copies.empty()?"":"  ")+tag;
            }
            fact("Copies",copies);
        }
        c.rect(34,643,1209,2,steel);
        if(!status.empty()) c.text(44,646,status,Font::Caption,Gold,1180);
        auto hints=[&](const std::vector<std::pair<std::string,std::string>>& pairs) {
            int width=0;
            for(const auto& [key,name]:pairs) width+=std::max(33,c.measure(key,Font::Caption)+12)+c.measure(name,Font::Album)+30;
            int x=(1280-width)/2;
            for(const auto& [key,name]:pairs) {
                int w=std::max(33,c.measure(key,Font::Caption)+12);
                touchHint(key, x - 4, 657, w + c.measure(name,Font::Album) + 25, 51);
                c.round(x,668,w,30,15,Cyan);
                c.text(x+(w-c.measure(key,Font::Caption))/2,670,key,Font::Caption,well); x+=w+8;
                c.text(x,665,name,Font::Album);x+=c.measure(name,Font::Album)+22;
            }
        };
        if(eventFilters) {
            touchTargets.clear();
            c.panel(350,122,580,489,well,lilac,11);
            c.text(375,139,"Album filters",Font::Album);
            static constexpr std::array captions{"Compatibility","Gift type","Shininess","Session status","Language","Language copies","Sort","Group","Archive"};
            for(unsigned i=0;i<captions.size();++i) {
                int y=182+i*45;
                touchRow(367,y,546,40,i,int(i)==eventFilterRow);
                c.round(367,y,546,40,6,int(i)==eventFilterRow?steel:row);
                c.text(384,y+5,captions[i],Font::Album,Ink,226);
                c.text(631,y+7,values[i],Font::Caption,faded,252);
            }
            hints({{"D-pad","Choose"},{"A","Change"},{"B","Done"}});
        } else if(detailView) {
            c.panel(39,187,789,431,well,steel,9);
            const auto& card=eventCard();
            giftArt(c,card,145,304,150);giftTitle(c,251,221,538,card.title);
            c.text(251,291,"No. "+cardNumber(card.number)+"   "+card.language+"   Gen "+std::to_string(eventGen),Font::Album,faded);
            c.text(251,341,card.original,Font::Caption,lilac,545);
            const auto* blocker = eventQueueBlocker();
            const bool allowed = !blocker;
            c.text(66,431,allowed?"Queue this gift":"Browse only",Font::Album,allowed?Cyan:Gold);
            c.text(66,474,allowed?"X stages this card. Export the edited copy from Boxes.":blocker,Font::Caption,faded,737);
            c.text(66,535,"Copy "+std::to_string(eventCopy+1)+" of "+std::to_string(album.groups[eventRow].copies.size()),Font::Caption,Ink);
            if(allowed) hints({{"X","Queue"},{"L/R","Language copy"},{"B","Back"}});
            else hints({{"L/R","Language copy"},{"B","Back"}});
        } else hints({{"A","Open"},{"X","Search"},{"Y","Filters"},{"L/R","Section"},{"+","Sort"},{"B","Back"}});
    }

    void renderTransfer(Canvas& c)
    {
        if (mode==2)
        {
            toolFrame(c,"Review Bank move","HARDCORE");
            toolRow(c,0,speciesName(*transfer),bankDepositReview?"Save to Bank":"Bank to save",true);
            c.text(800,240,"Export and move",Font::Title,Ink,410);
            c.text(800,312,"The edited save is written to exports.",Font::Small,Soft,410);
            c.text(800,360,"The source slot or Bank entry is emptied.",Font::Small,Soft,410);
            return;
        }
        toolFrame(c, "Transfer preview", "DESTINATION");
        c.text(74, 208, speciesName(*transfer), Font::Title, Ink, 630);
        c.text(76, 261, "Generation " + std::string(static_cast<const char*>(clipboard->generation())) +
            "  >  Generation " + static_cast<const char*>(transfer->generation()), Font::Body, Cyan);
        const int first = transferRow / 5 * 5;
        for (int i = first; i < std::min(first + 5, static_cast<int>(transferChanges.size())); ++i)
        {
            const auto& [field, change] = transferChanges[i];
            toolRow(c, i - first + 2, field, label(change, 56), i == transferRow);
        }
        if (transferChanges.empty())
            c.text(76, 329, "Displayed stats and moves are unchanged.", Font::Body, Soft);
        c.text(802, 233, "Box " + std::to_string(box + 1) + " / Slot " + std::to_string(slot + 1),
            Font::Title, Ink, 410);
        c.text(802, 298, "Source stays in the Bank.", Font::Body, Soft, 410);
        c.text(802, 341, std::to_string(transferChanges.size()) + " displayed fields changed.", Font::Body, Soft, 410);
        c.text(802, 391, "Encounter legality is not checked.", Font::Small, Gold, 410);
    }

    void touchHint(std::string_view button, int x, int y, int w, int h)
    {
        if (button == "L/R" || button == "ZL/ZR" || button == "Left/Right")
        {
            const u64 left = button == "L/R" ? HidNpadButton_L :
                button == "ZL/ZR" ? HidNpadButton_ZL : HidNpadButton_Left;
            const u64 right = button == "L/R" ? HidNpadButton_R :
                button == "ZL/ZR" ? HidNpadButton_ZR : HidNpadButton_Right;
            touchButton(x, y, w / 2, h, left);
            touchButton(x + w / 2, y, w - w / 2, h, right);
            return;
        }
        if (button == "D-Pad" || button == "D-pad")
        {
            touchTargets.push_back({{x, y, w, h}, 0, -1, true});
            return;
        }
        const u64 key = button == "A" ? HidNpadButton_A : button == "B" ? HidNpadButton_B :
            button == "X" ? HidNpadButton_X : button == "Y" ? HidNpadButton_Y :
            button == "ZL" ? HidNpadButton_ZL : button == "ZR" ? HidNpadButton_ZR :
            button == "+" ? HidNpadButton_Plus : button == "-" ? HidNpadButton_Minus : 0;
        if (key)
            touchButton(x, y, w, h, key);
    }

    void footer(Canvas& c, std::string_view hints)
    {
        int x = 43;
        c.panel(34, 631, 1212, 55, Band, Edge, 10);
        while (!hints.empty())
        {
            const auto next = hints.find("   ");
            const auto item = hints.substr(0, next);
            const auto divider = item.find(' ');
            const auto button = item.substr(0, divider);
            const int badge = std::max(28, c.measure(button, Font::Small) + 14);
            c.round(x, 644, badge, 28, 14, rgb(0x2666a0));
            c.text(x + (badge - c.measure(button, Font::Small)) / 2, 645, button, Font::Small);
            const auto caption = divider == item.npos ? std::string_view{} : item.substr(divider + 1);
            const int start = x;
            x += badge + 8;
            x += c.text(x, 642, caption, Font::Body) + 23;
            touchHint(button, start - 5, 631, x - start, 55);
            if (next == hints.npos)
                break;
            hints.remove_prefix(next + 3);
        }
    }

    std::string_view controls()
    {
        switch (view)
        {
            case View::Files: return "A Open   X Refresh   Y Bank   L/R Page   ZL Tools   + Quit";
            case View::Box: return clipboard && sourceWritable
                ? "A Inspect   L/R Box   X Party   Y Bank   - Trainer   ZL Tools   ZR Place   B Files"
                : "A Inspect   L/R Box   X Party   Y Bank   - Trainer   ZL Tools   B Files";
            case View::BoxSearch: return boxMatches.empty() ? "B Boxes" : "A Go to box slot   ZL/ZR Page   B Boxes";
            case View::Party: return "A Inspect   B Boxes";
            case View::Bank: return "A Inspect   X Refresh   L/R Page   B Back";
            case View::Detail:
                if (detailFrom == View::Bank)
                    return mode==2 ? "A Select to move   B Bank" : "A Select for paste   ZL Park copy   B Bank";
                if (!forge::canDeposit(*save.data))
                    return "B Back";
                if (detailEditable())
                    return "X Edit   - Advanced   D-Pad +/-1   Y Bank copy   ZL Park copy   B Back";
                return mode==2 ? "- Advanced   Y Move to Bank   B Back" : "- Advanced   Y Copy to Bank   ZL Park copy   B Back";
            case View::ItemBank: return save.writable ? "A Copy to bag   ZL/ZR Page   B Back"
                                                       : "ZL/ZR Page   B Back";
            case View::Park: return "D-Pad Resident   A Feed   Y Play   X Walk   ZL Journal   B Back";
            case View::ParkJournal: return "ZL/ZR Page   B Park";
            case View::Nursery: return save.writable ? "A Review withdrawal   B Back" : "B Back";
            case View::NurseryReview: return "A Withdraw   B Cancel";
            case View::Hall: return "ZL/ZR Page   B Back";
            case View::GbaClock: return save.writable ? "D-Pad Adjust   ZL/ZR Clock   X Zero   Y Berry fix   A Apply   B Back"
                                                     : "ZL/ZR Clock   B Back";
            case View::Walker: return "D-Pad Move   A Save walk   L/R Resident   B Park";
            case View::Tools: return "A Open   B Back";
            case View::BoxTools: return "A Actions   Y Mark   X Mark all   ZL/ZR Page   B Back";
            case View::BoxActions: return "A Select   Left/Right Wallpaper   B Back";
            case View::BoxReview: return actionsRow == 6 ? "A Release   B Cancel" : "A Apply   B Cancel";
            case View::Engine: return engineJob ? "Working offline..." : engineCandidate.empty() ? "A Open   ZL/ZR Page   B Back" : "X Apply to save copy   B Discard";
            case View::BoxAudit: return "ZL/ZR Page   B Back";
            case View::Snapshots: return "A Review   X Snapshot   ZL/ZR Page   B Back";
            case View::SnapshotReview: return "A Restore copy   B Back";
            case View::CrossSource: return "A Plan moves   ZL/ZR Page   B Back";
            case View::CrossReview: return "A Export pair   ZL/ZR Page   B Cancel";
            case View::DexRegister: return "A Register   B Cancel";
            case View::Showdown: return "A Party QR   Y Box QR   X Save text   ZL Import   B Back";
            case View::ShowdownReview: return "A Import   B Cancel";
            case View::Modes: return "A Select mode   B Back";
            case View::Editor: return "D-Pad Edit   L/R +/-10   X Nickname   ZR OT name   A Apply   B Cancel";
            case View::EditorChoice: return editorChoices.empty() ? "B Editor" : "A Select   ZL/ZR Page   B Editor";
            case View::Ribbons: return "A Toggle   ZL/ZR Page   B Editor";
            case View::Bag: return save.writable ? "L/R Pouch   A Apply   Y Add   - Bag actions   X Store copy   B Back"
                                                  : "L/R Pouch   X Store copy   B Back";
            case View::BagAdd: return bagOptions.empty() ? "B Cancel" :
                "D-Pad Select / Qty   L/R +/-10   ZL/ZR Page   A Add   B Cancel";
            case View::BagMax: return "A Select   B Cancel";
            case View::BagPresets: return "A Review   B Back";
            case View::BagPresetReview: return "A Restore quantities   B Back";
            case View::Dex: return save.writable ? "A Plan   L Cross-save   R Register   X Shiny   Y Missing   ZL/ZR Page   B Back"
                                                  : "X Shiny   Y Missing   ZL/ZR Page   B Back";
            case View::DexPlan: return dexPlan.moves.empty() ? "B Cancel" : "X Apply plan   ZL/ZR Page   B Cancel";
            case View::Events: return "A Open   X Search   Y Filters   L/R Section   + Sort   B Back";
            case View::EventDetail: return canInject() ? "X Queue card   B Cancel" : "B Back";
            case View::Transfer: return "A Confirm transfer   ZL/ZR Page   B Cancel";
            case View::Trainer: return save.writable
                ? "D-Pad Adjust   L/R +/-10   X Rename   Y Profile   A Apply   B Boxes"
                : "Y Profile   B Boxes";
        }
        return {};
    }

public:
    bool motion = true;

    explicit App(SearchKeyboard keyboard = keyboardText) : searchKeyboard(keyboard) {}

    bool searchAvailable() const { return confirm == Confirm::None && searchHeading(); }
    bool consumeSearchKeyboardOpened() { return std::exchange(searchKeyboardOpened, false); }

    void start()
    {
        try
        {
            forge::prepareStorage();
            forge::prepareExtras();
            preferences=forge::readEditorPreferences();
            updateMode();
            reloadSaves();
        }
        catch (const std::exception& error)
        {
            status = error.what();
        }
    }

    bool engineReady() const { return engineJob && engineJob->ready(); }
    bool running() const { return !exit; }

    void update(u64 keys)
    {
        if (keys) touchControlsVisible = false;
        if (keys & HidNpadButton_StickR)
        {
            motion = !motion;
            return;
        }
        status.clear();
        try
        {
            if (confirm != Confirm::None)
            {
                if (keys & HidNpadButton_B)
                    confirm = Confirm::None;
                else if (keys & HidNpadButton_A)
                {
                    const auto action = confirm;
                    confirm = Confirm::None;
                    if (action == Confirm::Exit)
                        exit = true;
                    else
                        closeToFiles();
                }
                return;
            }
            if (engineJob) { if (engineJob->ready()) finishEngine(); return; }
            if ((keys & HidNpadButton_Plus) && view!=View::Events)
            {
                if (!engineCandidate.empty()) { status="Apply or discard the preview first."; return; }
                if (view == View::Files)
                    requestClose(Confirm::Exit);
                else if (save.dirty)
                {
                    ManagementAccess permission(save,mode==2 && sourceWritable);
                    const auto path = forge::exportSave(save);
                    save.file.path = path;
                    save.dirty = false;
                    status = "Exported to " + path.substr(std::string_view(forge::Root).size() + 1);
                }
                return;
            }
            if ((keys & SearchButtons) == SearchButtons && searchAvailable())
            {
                openSearch();
                return;
            }
            switch (view)
            {
                case View::Files: handleFiles(keys); break;
                case View::Box: handleBox(keys); break;
                case View::BoxSearch: handleBoxSearch(keys); break;
                case View::Party: handleParty(keys); break;
                case View::Bank: handleBank(keys); break;
                case View::Trainer: handleTrainer(keys); break;
                case View::Detail: handleDetail(keys); break;
                case View::Engine: handleEngine(keys); break;
                case View::Tools: handleTools(keys); break;
                case View::Editor: handleEditor(keys); break;
                case View::EditorChoice: handleEditorChoice(keys); break;
                case View::Ribbons: handleRibbons(keys); break;
                case View::Bag: handleBag(keys); break;
                case View::BagAdd: handleBagAdd(keys); break;
                case View::BagMax: handleBagMax(keys); break;
                case View::BagPresets: handleBagPresets(keys, false); break;
                case View::BagPresetReview: handleBagPresets(keys, true); break;
                case View::Dex: handleDex(keys); break;
                case View::DexPlan: handleDexPlan(keys); break;
                case View::Events: handleEvents(keys); break;
                case View::EventDetail: handleEventDetail(keys); break;
                case View::Transfer: handleTransfer(keys); break;
                case View::ItemBank: handleItemBank(keys); break;
                case View::Park: handlePark(keys); break;
                case View::ParkJournal: handleJournal(keys); break;
                case View::Nursery: handleNursery(keys); break;
                case View::NurseryReview: handleNurseryReview(keys); break;
                case View::GbaClock: handleGbaClock(keys); break;
                case View::Hall:
                    if (keys & HidNpadButton_B) view = View::Tools;
                    else scroll(keys, hallRow, hall.size());
                    break;
                case View::Walker: handleWalker(keys); break;
                case View::BoxTools: handleBoxTools(keys); break;
                case View::BoxActions: handleBoxActions(keys); break;
                case View::BoxReview: handleBoxReview(keys); break;
                case View::BoxAudit:
                    if (keys & HidNpadButton_B)
                        view = View::BoxActions;
                    else
                        scroll(keys, issuesRow, issues.size());
                    break;
                case View::Snapshots: handleSnapshots(keys); break;
                case View::SnapshotReview: handleSnapshotReview(keys); break;
                case View::CrossSource: handleCrossSource(keys); break;
                case View::CrossReview: handleCrossReview(keys); break;
                case View::DexRegister: handleDexRegister(keys); break;
                case View::Showdown: handleShowdown(keys); break;
                case View::ShowdownReview: handleShowdownReview(keys); break;
                case View::Modes: handleModes(keys); break;
            }
        }
        catch (const std::exception& error)
        {
            status = error.what();
        }
    }

    void touch(const TouchAction& gesture)
    {
        if (engineJob)
            return;
        if (gesture.tap())
        {
            for (auto i = touchTargets.rbegin(); i != touchTargets.rend(); ++i)
            {
                if (!i->rect.contains(gesture.start) || !i->rect.contains(gesture.end))
                    continue;
                if (i->openControls)
                    touchControlsVisible = !touchControlsVisible;
                else if (i->selection && *i->selection != i->index)
                    *i->selection = i->index;
                else if (i->giftFilter >= 0)
                {
                    eventFilterRow = i->giftFilter;
                    changeGiftFilter(1);
                }
                else if (i->keys)
                {
                    const int before = screen();
                    constexpr u64 repeatable = HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left |
                        HidNpadButton_Right | HidNpadButton_L | HidNpadButton_R |
                        HidNpadButton_ZL | HidNpadButton_ZR;
                    const bool keep = touchControlsVisible && (i->keys & repeatable) == i->keys;
                    update(i->keys);
                    touchControlsVisible = keep && before == screen() && !engineJob && !searchKeyboardOpened;
                }
                else if (i->row >= 0 && selectedTouchRow >= 0)
                {
                    const int delta = i->row - selectedTouchRow;
                    if (!delta && (view == View::Tools || view == View::Events))
                        update(HidNpadButton_A);
                    else
                        for (int step = 0; step < std::abs(delta); ++step)
                            update(delta < 0 ? HidNpadButton_Up : HidNpadButton_Down);
                }
                return;
            }
            return;
        }
        if (confirm != Confirm::None || touchControlsVisible || view == View::Walker)
            return;
        const int dx = gesture.end.x - gesture.start.x;
        const int dy = gesture.end.y - gesture.start.y;
        if (gesture.start.y < 105 || gesture.start.y >= 585)
            return;
        if (std::abs(dx) >= 55 && std::abs(dx) > std::abs(dy) &&
            (view == View::Files || view == View::Box || view == View::Bank))
            update(dx < 0 ? HidNpadButton_R : HidNpadButton_L);
        else if (std::abs(dy) >= 45 && std::abs(dy) >= std::abs(dx))
        {
            const int before = screen();
            for (int step = 0; step < std::clamp(std::abs(dy) / 55, 1, 7) && before == screen(); ++step)
                update(dy < 0 ? HidNpadButton_Down : HidNpadButton_Up);
        }
    }

    int screen() const { return static_cast<int>(view) + (confirm != Confirm::None ? 64 : 0); }

    void render(Canvas& c)
    {
        touchTargets.clear();
        selectedTouchRow = -1;
        c.background();
        c.gradient(0, 0, 1280, 55, rgb(0x181f40), Band);
        c.rect(0, 53, 1280, 2, Edge);
        c.text(640 - c.measure("PKForge", Font::Title) / 2, 1, "PKForge", Font::Title);
        c.text(43, 12, "SWITCH 2", Font::Small, Soft);
        c.text(1044, 12, motion ? "R3  Motion on" : "R3  Motion off", Font::Small, Soft);
        c.rect(0, 55, 1280, 41, rgb(0x101e3b));
        if (save.data)
        {
            const auto context = label(save.file.name, 45) + "  /  " +
                label(forge::gameTitle(save.file, *save.data), 35);
            c.text(44, 60, context, Font::Body, Ink, 850);
            if (save.dirty)
            {
                c.text(1016, 62, "+  Export edited copy", Font::Small, Gold);
                touchButton(1004, 55, 275, 41, HidNpadButton_Plus);
            }
            else if (!save.writable)
                c.text(1080, 62, (mode==2 && sourceWritable)?"Hardcore":"Read only", Font::Small, Gold);
        }
        else
            c.text(44, 60, "SD card  /  Offline", Font::Body, Soft);

        switch (view)
        {
            case View::Files: renderList(c, saves, false); break;
            case View::Box: renderBox(c); break;
            case View::BoxSearch: renderBoxSearch(c); break;
            case View::Party: renderParty(c); break;
            case View::Bank: renderList(c, bank, true); break;
            case View::Trainer: renderTrainer(c); break;
            case View::Detail: renderDetail(c); break;
            case View::Engine: renderEngine(c); break;
            case View::Tools: renderTools(c); break;
            case View::Editor: renderEditor(c); break;
            case View::EditorChoice: renderEditorChoice(c); break;
            case View::Ribbons: renderRibbons(c); break;
            case View::Bag: renderBag(c); break;
            case View::BagAdd: renderBagAdd(c); break;
            case View::BagMax: renderBagMax(c); break;
            case View::BagPresets: renderBagPresets(c, false); break;
            case View::BagPresetReview: renderBagPresets(c, true); break;
            case View::Dex: renderDex(c); break;
            case View::DexPlan: renderDexPlan(c); break;
            case View::Events:
            case View::EventDetail:
                renderEvents(c);
                touchControlsButton(c, 1105, 85);
                if (touchControlsVisible) renderTouchControls(c);
                return;
            case View::Transfer: renderTransfer(c); break;
            case View::ItemBank: renderItemBank(c); break;
            case View::Park: renderPark(c, false); break;
            case View::ParkJournal: renderJournal(c); break;
            case View::Nursery: renderNursery(c, false); break;
            case View::NurseryReview: renderNursery(c, true); break;
            case View::Hall: renderHall(c); break;
            case View::GbaClock: renderGbaClock(c); break;
            case View::Walker: renderPark(c, true); break;
            case View::BoxTools: renderBoxTools(c); break;
            case View::BoxActions: renderBoxActions(c); break;
            case View::BoxReview: renderBoxReview(c); break;
            case View::BoxAudit: renderBoxAudit(c); break;
            case View::Snapshots: renderSnapshots(c); break;
            case View::SnapshotReview: renderSnapshotReview(c); break;
            case View::CrossSource: renderCrossSources(c); break;
            case View::CrossReview: renderCrossReview(c); break;
            case View::DexRegister: renderDexRegister(c); break;
            case View::Showdown: renderShowdown(c); break;
            case View::ShowdownReview: renderShowdownReview(c); break;
            case View::Modes: renderModes(c); break;
        }

        c.panel(34, 585, 1212, 37, Band, Grid, 7);
        if (searchAvailable())
        {
            const auto filter = view == View::EditorChoice ? std::string_view(editorQuery) : query(view);
            if (status.empty() && !filter.empty())
                c.text(48, 589, "Filter  " + label(filter, 60), Font::Small, Cyan, 960);
            else
                c.text(48, 589, label(status, 130), Font::Small, Soft, 960);
            c.round(1030, 590, 83, 27, 13, rgb(0x2666a0));
            c.text(1040, 590, "ZL+ZR", Font::Small);
            c.text(1126, 589, "Search", Font::Small, Ink);
            touchButton(1020, 578, 220, 49, SearchButtons);
        }
        else
            c.text(48, 589, label(status, 160), Font::Small, Soft, 1184);
        if (confirm == Confirm::None)
        {
            footer(c, controls());
            if (engineJob)
                touchTargets.clear();
            else
            {
                touchButton(1035, 0, 245, 54, HidNpadButton_StickR);
                touchControlsButton(c, 205, 6);
                if (touchControlsVisible) renderTouchControls(c);
            }
            return;
        }
        touchTargets.clear();
        c.suspendPortrait();
        c.panel(308, 222, 664, 222, Band, Cyan, 15);
        c.text(338, 254, "Discard unexported edits?", Font::Title);
        c.text(338, 321, "Changes in this session will be discarded.", Font::Body, Soft);
        c.panel(337, 376, 278, 44, Deck, Cyan, 7);
        c.text(348, 383, "B  Keep editing", Font::Body, Ink);
        c.panel(636, 376, 302, 44, Deck, Edge, 7);
        c.text(648, 383, "A  Discard edits", Font::Body, Gold);
        touchButton(337, 376, 278, 44, HidNpadButton_B);
        touchButton(636, 376, 302, 44, HidNpadButton_A);
        footer(c, "B Keep editing   A Discard edits");
    }

};
}

#ifndef PKFORGE_HOST_PREVIEW
namespace
{
int startupError(const char* message, PadState& pad)
{
    auto* console = consoleInit(nullptr);
    std::printf("\n  PKForge\n\n  %s\n\n  Press + to quit.\n", message);
    while (appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(console);
    }
    consoleExit(console);
    return 1;
}

void run(Framebuffer& framebuffer, PadState& pad, bool touchEnabled)
{
    using Clock = std::chrono::steady_clock;
    Assets assets("romfs:/ui", forge::Models, forge::Portraits);
    App app;
    Canvas canvas(assets);
    app.start();
    app.render(canvas);
    std::vector<Pixel> previous;
    auto start = Clock::now();
    bool slide = false;
    bool redraw = true;
    auto nextIdle = start;
    auto idleStart = start;
    SearchChord searchChord;
    TouchGesture touchGesture;
    bool waitingForRelease = false;
    constexpr u64 controls = HidNpadButton_AnyLeft | HidNpadButton_AnyRight |
        HidNpadButton_AnyUp | HidNpadButton_AnyDown | HidNpadButton_A | HidNpadButton_B |
        HidNpadButton_X | HidNpadButton_Y | HidNpadButton_L | HidNpadButton_R |
        HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Plus | HidNpadButton_Minus |
        HidNpadButton_StickR;
    while (appletMainLoop() && app.running())
    {
        padUpdate(&pad);
        const u64 held = padGetButtons(&pad) & controls;
        waitingForRelease = waitingForRelease && held;
        const u64 keys = waitingForRelease ? 0 :
            searchChord.update(padGetButtonsDown(&pad) & controls, held, app.searchAvailable());
        std::optional<TouchAction> touch;
        const bool engineReady = app.engineReady();
        if (touchEnabled)
        {
            HidTouchScreenState state{};
            if (hidGetTouchScreenStates(&state, 1))
            {
                const auto& finger = state.touches[0];
                touch = touchGesture.sample(state.count, finger.finger_id,
                    {static_cast<int>(finger.x), static_cast<int>(finger.y)},
                    finger.attributes & HidTouchAttribute_End, keys || engineReady);
            }
        }
        if (keys || touch || engineReady)
        {
            const u64 directions = (keys & HidNpadButton_AnyLeft ? HidNpadButton_Left : 0) |
                (keys & HidNpadButton_AnyRight ? HidNpadButton_Right : 0) |
                (keys & HidNpadButton_AnyUp ? HidNpadButton_Up : 0) |
                (keys & HidNpadButton_AnyDown ? HidNpadButton_Down : 0);
            const int oldScreen = app.screen();
            const auto previousPortrait = canvas.animationKey();
            const bool wasMotion = app.motion;
            if (touch)
                app.touch(*touch);
            else
                app.update(keys | directions);
            waitingForRelease = app.consumeSearchKeyboardOpened();
            if (!app.running())
                break;
            if (app.motion)
                previous = canvas.pixels();
            else
                previous.clear();
            slide = app.screen() != oldScreen;
            app.render(canvas);
            start = Clock::now();
            if (canvas.animationKey() != previousPortrait || app.motion != wasMotion)
            {
                idleStart = start;
                nextIdle = start;
            }
            redraw = true;
        }

        const auto now = Clock::now();
        const bool idleFrame = app.motion && canvas.animates() && now >= nextIdle;
        if (!redraw && !idleFrame)
        {
            svcSleepThread(8000000);
            continue;
        }

        const auto elapsed = std::chrono::duration<float>(now - start).count();
        const float duration = slide ? 0.18f : 0.11f;
        const float phase = previous.empty() ? 1.0f : std::min(1.0f, elapsed / duration);
        u32 stride = 0;
        auto* pixels = static_cast<Pixel*>(framebufferBegin(&framebuffer, &stride));
        present(pixels, stride / sizeof(Pixel), canvas.pixels(), previous, phase, slide);
        if (app.motion && (!slide || phase >= 1.0f) && canvas.animates())
        {
            const auto portraitTime = std::chrono::duration<float>(now - idleStart).count();
            canvas.animatePortrait(pixels, stride / sizeof(Pixel), portraitTime);
            nextIdle = now + std::chrono::milliseconds(canvas.animationStepMs());
        }
        framebufferEnd(&framebuffer);
        redraw = phase < 1.0f;
    }
}
}

int main()
{
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad{};
    padInitializeDefault(&pad);
    if (R_FAILED(romfsInit()))
        return startupError("Cannot open PKForge resources. Recopy PKForge.nro.", pad);

    auto* window = nwindowGetDefault();
    Framebuffer framebuffer{};
    if (R_FAILED(nwindowSetDimensions(window, Width, Height)) ||
        R_FAILED(framebufferCreate(&framebuffer, window, Width, Height, PIXEL_FORMAT_RGB_565, 2)))
    {
        romfsExit();
        return startupError("Cannot open the framebuffer.", pad);
    }

    try
    {
        if (R_FAILED(framebufferMakeLinear(&framebuffer)))
            throw std::runtime_error("Cannot initialize the framebuffer.");
        const bool touchEnabled = R_SUCCEEDED(hidSetTouchScreenOutputRanges(Width, Height));
        if (touchEnabled)
            hidInitializeTouchScreen();
        run(framebuffer, pad, touchEnabled);
    }
    catch (const std::exception& error)
    {
        framebufferClose(&framebuffer);
        romfsExit();
        return startupError(error.what(), pad);
    }
    framebufferClose(&framebuffer);
    romfsExit();
    return 0;
}
#endif
