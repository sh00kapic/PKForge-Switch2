// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "editors.hpp"
#include <set>
#include <array>

namespace forge {
struct AlbumCard {
    unsigned archive, species, level, number;
    unsigned ballId;
    bool shiny, fits;
    std::string title, subject, language, key, date, ot, tid, series, moves, item, ball, met, games, contents, original;
};
struct AlbumGroup {
    std::vector<int> copies;
    std::string section;
    int sectionCount = 0;
};
struct GiftAlbum {
    std::vector<Gift> archive;
    std::vector<AlbumCard> cards;
    std::vector<AlbumGroup> groups;
    std::set<std::string> staged;
    int kind = 0, status = 0, sort = 0, grouping = 0;
    bool fitsOnly = false, shinyOnly = false, collapse = true;
    std::string language;
    static constexpr std::array sorts{"Newest", "Oldest", "Card number", "Dex number", "Title", "Level"};
    static constexpr std::array groupings{"Year", "Series", "Game", "Species", "None"};
    static constexpr std::array kinds{"All gifts", "Pokemon", "Items"};
    static constexpr std::array statuses{"Any status", "Unstaged", "Staged"};
    static constexpr std::array languages{"Any language","JPN","ENG","FRE","ITA","GER","SPA","KOR","CHS","CHT"};
    void load(const std::string& base, int generation, const Save& save);
    void filter(std::string_view query, std::string_view preferred);
};
}
