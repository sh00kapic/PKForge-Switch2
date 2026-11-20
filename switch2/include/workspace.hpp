// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pkx/PKX.hpp"
#include "sav/Sav.hpp"

#include <span>
#include <string>
#include <vector>

namespace forge
{
inline constexpr auto Root = "sdmc:/switch/PKForge";
inline constexpr auto Saves = "sdmc:/switch/PKForge/saves";
inline constexpr auto Bank = "sdmc:/switch/PKForge/bank";
inline constexpr auto Exports = "sdmc:/switch/PKForge/exports";
inline constexpr auto Models = "sdmc:/switch/PKForge/models";
inline constexpr auto Portraits = "sdmc:/switch/PKForge/portraits";

struct File
{
    std::string name;
    std::string path;
};

std::vector<File> listFiles(const std::string& directory, bool bank, bool includeFolders = false);
void prepareStorage();

struct Save
{
    File file;
    std::unique_ptr<pksm::Sav> data;
    bool writable;
    bool dirty = false;
};

std::vector<u8> engineSnapshot(const Save& save);
void acceptEngineCopy(Save& save, std::span<const u8> data);
struct EditorPreferences { bool hax = false, hardcore = false, trainerOwnership = true; };
EditorPreferences readEditorPreferences();
void writeEditorPreferences(EditorPreferences preferences);
void deleteTrainerProfile(std::string_view name);
void moveToBank(Save& save, int box, int slot);
void moveFromBank(Save& save, const File& bank, int box, int slot, const pksm::PKX& expected);
void storeTrainerProfile(std::string_view name, std::string_view value);
std::vector<std::pair<std::string,std::string>> trainerProfiles();
Save openSave(const File& file);
pksm::GameVersion displayVersion(const File& file, const pksm::Sav& save);
std::string gameTitle(const File& file, const pksm::Sav& save);
unsigned gameArtwork(const File& file, const pksm::Sav& save);
bool canDeposit(const pksm::Sav& save);
int boxSlotCount(const pksm::Sav& save, int box);
std::string exportSave(const Save& save);
std::string deposit(const pksm::PKX& pokemon);
std::unique_ptr<pksm::PKX> bankPokemon(const File& file);
void putPokemon(Save& save, int box, int slot, pksm::PKX& pokemon);
}
