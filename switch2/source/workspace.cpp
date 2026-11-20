// SPDX-License-Identifier: GPL-3.0-or-later
#include "workspace.hpp"
#include "utils/i18n.hpp"
#include "utils/endian.hpp"
#include "library.hpp"
#include "editors.hpp"

#include "pkx/PA9.hpp"
#include "pkx/PA8.hpp"
#include "pkx/PB8.hpp"
#include "wcx/WCX.hpp"
#include "sav/SavLGPE.hpp"
#include "sav/SavGameCube.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <set>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace forge
{
namespace
{
using namespace std::string_view_literals;
using pksm::Generation;
using pksm::PKX;
using pksm::Sav;

struct EntityFormat
{
    std::string_view extension;
    Generation generation;
};

constexpr std::array formats{
    EntityFormat{".pk1", Generation::ONE}, EntityFormat{".pk2", Generation::TWO},
    EntityFormat{".pk3", Generation::THREE}, EntityFormat{".pk4", Generation::FOUR},
    EntityFormat{".pk5", Generation::FIVE}, EntityFormat{".pk6", Generation::SIX},
    EntityFormat{".pk7", Generation::SEVEN}, EntityFormat{".pb7", Generation::LGPE},
    EntityFormat{".pk8", Generation::EIGHT}, EntityFormat{".pa8", Generation::EIGHT},
    EntityFormat{".pb8", Generation::EIGHT},
    EntityFormat{".pk9", Generation::NINE}, EntityFormat{".pa9", Generation::NINE},
};

auto formatFor(std::string_view ending)
{
    return std::ranges::find(formats, ending, &EntityFormat::extension);
}

bool isOccupied(const PKX& pokemon)
{
    return pokemon.species() != pksm::Species::None;
}

std::string extension(std::string_view name)
{
    const auto dot = name.find_last_of('.');
    if (dot == name.npos)
        return {};
    std::string ending(name.substr(dot));
    std::ranges::transform(ending, ending.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ending;
}

void makeDirectory(const std::string& path)
{
    if (::mkdir(path.c_str(), 0777) == 0)
        return;
    struct stat info{};
    if (errno != EEXIST || ::stat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
        throw std::runtime_error("Cannot create " + path);
}

void scan(const std::string& directory, std::string_view prefix, bool bank, bool includeFolders,
    std::vector<File>& files)
{
    struct DirCloser { void operator()(DIR* dir) const { ::closedir(dir); } };
    const std::unique_ptr<DIR, DirCloser> stream(::opendir(directory.c_str()));
    if (!stream)
        throw std::runtime_error("Cannot read " + directory);

    constexpr std::array saveExtensions{".sav"sv, ".bin"sv, ".srm"sv, ".dat"sv, ".dsv"sv, ".gci"sv};
    while (const auto* entry = ::readdir(stream.get()))
    {
        const std::string name(entry->d_name);
        if (name.starts_with('.'))
            continue;
        const std::string path = directory + "/" + name;
        if (path.size() > 750)
            continue;
        struct stat info{};
        if (::stat(path.c_str(), &info) != 0)
            continue;
        if (S_ISDIR(info.st_mode) && includeFolders)
            scan(path, std::string(prefix) + name + "/", bank, false, files);
        if (!S_ISREG(info.st_mode))
            continue;
        const std::string ending = extension(name);
        const bool supported = bank
            ? formatFor(ending) != formats.end()
            : (name == "main" || std::ranges::find(saveExtensions, ending) != saveExtensions.end());
        if (supported)
            files.push_back({bank && !prefix.empty() ? name + "  " +
                std::string(prefix.substr(0, prefix.size() - 1)) : std::string(prefix) + name, path});
    }
}

struct Input
{
    std::shared_ptr<u8[]> bytes;
    size_t size;
};

Input readFile(const std::string& path, size_t limit)
{
    const std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path.c_str(), "rb"), std::fclose);
    if (!file || std::fseek(file.get(), 0, SEEK_END) != 0)
        throw std::runtime_error("Cannot open file.");
    const long size = std::ftell(file.get());
    if (size <= 0 || static_cast<unsigned long>(size) > limit)
        throw std::runtime_error("File size is unsupported.");
    if (std::fseek(file.get(), 0, SEEK_SET) != 0)
        throw std::runtime_error("Cannot read file.");
    auto bytes = std::make_shared_for_overwrite<u8[]>(size);
    if (std::fread(bytes.get(), 1, size, file.get()) != static_cast<size_t>(size) ||
        std::fgetc(file.get()) != EOF || std::ferror(file.get()))
        throw std::runtime_error("File changed or could not be read.");
    return {std::move(bytes), static_cast<size_t>(size)};
}

void writeNew(const std::string& path, std::span<const u8> bytes)
{
    // Keep temporary files out of both the save browser and the bank.
    const std::string temp = path + ".partial";
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0)
        throw std::runtime_error("Cannot create output file.");
    size_t written = 0;
    while (written < bytes.size())
    {
        const auto count = ::write(fd, bytes.data() + written, bytes.size() - written);
        if (count <= 0)
            break;
        written += static_cast<size_t>(count);
    }
    const int sync = ::fsync(fd);
    const int closed = ::close(fd);
    if (written == bytes.size() && sync == 0 && closed == 0 &&
        std::rename(temp.c_str(), path.c_str()) == 0)
        return;
    std::remove(temp.c_str());
    throw std::runtime_error("Write failed. Check the SD card and free space.");
}

std::string uniqueDirectory(std::string_view base)
{
    const auto stamp = std::to_string(static_cast<long long>(std::time(nullptr)));
    for (unsigned attempt = 0; attempt < 4096; ++attempt)
    {
        const auto path = std::string(base) + "/" + stamp + "-" + std::to_string(attempt);
        if (::mkdir(path.c_str(), 0777) == 0)
            return path;
        if (errno != EEXIST)
            break;
    }
    throw std::runtime_error("Cannot create a new output folder.");
}
}

void prepareStorage()
{
    for (const auto* path : {"sdmc:/switch", Root, Saves, Bank, Exports, Models, Portraits, Snapshots, Imports, BagPresets})
        makeDirectory(path);
}

std::vector<File> listFiles(const std::string& directory, bool bank, bool includeFolders)
{
    std::vector<File> files;
    scan(directory, "", bank, includeFolders, files);
    std::ranges::sort(files, {}, &File::name);
    return files;
}

Save openSave(const File& file)
{
    const auto input = readFile(file.path, 16 * 1024 * 1024);
    auto save = Sav::getSave(input.bytes, input.size);
    if (!save)
        throw std::runtime_error("Unsupported save or invalid file signature.");
    const bool bdsp = save->version() == pksm::GameVersion::BD || save->version() == pksm::GameVersion::SP;
    if (bdsp && !save->checksumsValid())
        throw std::runtime_error("Brilliant Diamond / Shining Pearl save checksum is invalid.");
    const bool writable = !dynamic_cast<const pksm::SavGameCube*>(save.get()) && save->checksumsValid();
    return {file, std::move(save), writable};
}

pksm::GameVersion displayVersion(const File& file, const Sav& save)
{
    using V = pksm::GameVersion;
    std::string name = " ";
    for (const unsigned char ch : file.name)
        name.push_back(std::isalnum(ch) ? static_cast<char>(std::tolower(ch)) : ' ');
    name.push_back(' ');
    const auto has = [&](std::string_view word) { return name.find(word) != std::string::npos; };
    const auto version = save.version();
    if (version == V::RD)
    {
        if (has(" yellow ")) return V::YW;
        if (has(" green ")) return V::GN;
        if (has(" blue ")) return save.language() == pksm::Language::JPN ? V::BU : V::GN;
    }
    if (version == V::GD && has(" silver ")) return V::SV;
    if (version == V::S && has(" ruby ") && !has(" sapphire ")) return V::R;
    if (version == V::FR && (has(" leafgreen ") || has(" leaf green "))) return V::LG;
    if (version == V::D && has(" pearl ") && !has(" diamond ")) return V::P;
    if (version == V::HG && (has(" soulsilver ") || has(" soul silver "))) return V::SS;
    return version;
}

std::string gameTitle(const File& file, const Sav& save)
{
    if (const auto* gameCube = dynamic_cast<const pksm::SavGameCube*>(&save))
        return std::string(gameCube->title());
    const auto version = displayVersion(file, save);
    if (version == pksm::GameVersion::GN && save.language() == pksm::Language::JPN)
        return "Pokemon Green";
    return i18n::game(pksm::Language::ENG, version);
}

unsigned gameArtwork(const File& file, const Sav& save)
{
    if (const auto* gameCube = dynamic_cast<const pksm::SavGameCube*>(&save))
        return gameCube->artwork();
    const auto version = displayVersion(file, save);
    if (version == pksm::GameVersion::GN && save.language() == pksm::Language::JPN)
        return 56;
    return static_cast<unsigned>(version);
}

bool canDeposit(const Sav& save)
{
    const auto* gameCube = dynamic_cast<const pksm::SavGameCube*>(&save);
    return !gameCube || gameCube->canDeposit();
}

int boxSlotCount(const Sav& save, int box)
{
    if (box < 0 || box >= save.maxBoxes())
        return 0;
    const int width = save.generation() <= Generation::TWO ? save.maxSlot() / save.maxBoxes() :
        save.generation() == Generation::LGPE ? pksm::SavLGPE::SlotsPerBox : 30;
    return std::clamp(save.maxSlot() - box * width, 0, width);
}

namespace
{
bool compactStorage(const Sav& save)
{
    return save.generation() <= Generation::TWO || save.generation() == Generation::LGPE;
}

std::unique_ptr<Sav> copyCompactSave(const Sav& active)
{
    const size_t size = active.getEntireLengthIncludingFooter();
    auto bytes = std::make_shared_for_overwrite<u8[]>(size);
    std::copy_n(active.rawData().get(), size, bytes.get());
    auto copy = Sav::getSave(bytes, size);
    if (!copy || copy->version() != active.version())
        throw std::runtime_error("Cannot stage the save copy.");
    return copy;
}

Input serialize(Sav& active)
{
    if (compactStorage(active))
    {
        auto copy = copyCompactSave(active);
        copy->finishEditing();
        if (!copy->checksumsValid())
            throw std::runtime_error("Edited copy failed save validation.");
        return {copy->rawData(), copy->getEntireLengthIncludingFooter()};
    }
    const auto version = active.version();
    const auto size = active.getEntireLengthIncludingFooter();
    auto bytes = std::make_shared_for_overwrite<u8[]>(size);
    struct ResumeEditing
    {
        Sav& save;
        ~ResumeEditing() { save.beginEditing(); }
    } resume{active};
    active.finishEditing();
    std::copy_n(active.rawData().get(), size, bytes.get());
    const auto probe = std::make_shared_for_overwrite<u8[]>(size);
    std::copy_n(bytes.get(), size, probe.get());
    const auto checked = Sav::getSave(probe, size);
    if (!checked || checked->version() != version || !checked->checksumsValid())
        throw std::runtime_error("Edited copy failed save validation.");
    return {std::move(bytes), size};
}

std::string fileLeaf(std::string_view path)
{
    return std::string(path.substr(path.find_last_of('/') + 1));
}

std::string safeFolderName(std::string_view label)
{
    std::string name;
    for (const unsigned char ch : label)
    {
        if (name.size() == 40)
            break;
        if (std::isalnum(ch) || ch == '-' || ch == '_')
            name.push_back(ch);
        else if (ch == ' ' && !name.empty() && name.back() != '-')
            name.push_back('-');
    }
    if (name.empty())
        throw std::runtime_error("Use at least one letter or number in the snapshot name.");
    return name;
}
}

std::vector<u8> engineSnapshot(const Save& save)
{
    if (!save.data) throw std::runtime_error("Open a save first.");
    auto input = !save.writable && !save.dirty && !save.data->checksumsValid()
        ? readFile(save.file.path, 16 * 1024 * 1024) : serialize(*save.data);
    return {input.bytes.get(), input.bytes.get() + input.size};
}

void acceptEngineCopy(Save& save, std::span<const u8> data)
{
    if (!save.writable || !save.data) throw std::runtime_error("The save is read-only.");
    if (data.size()!=save.data->getEntireLengthIncludingFooter()) throw std::runtime_error("The edited copy has a different size.");
    auto bytes = std::make_shared_for_overwrite<u8[]>(data.size());
    std::copy(data.begin(), data.end(), bytes.get());
    auto checked = Sav::getSave(bytes, data.size());
    if (!checked || checked->version() != save.data->version() || !checked->checksumsValid())
        throw std::runtime_error("The edited copy failed native save validation.");
    save.data = std::move(checked);
    save.dirty = true;
}

constexpr auto ProfileDirectory = "sdmc:/switch/PKForge/trainer-profiles";
namespace
{
void recoverSetting(const std::string& path)
{
    const auto previous = path + ".old";
    if (::access(path.c_str(), F_OK) != 0 && ::access(previous.c_str(), F_OK) == 0 &&
        std::rename(previous.c_str(),path.c_str()) != 0)
        throw std::runtime_error("Cannot recover the previous setting.");
}
void replaceSetting(const std::string& path, std::span<const u8> data)
{
    recoverSetting(path);
    const auto pending = path + ".new", previous = path + ".old";
    std::remove(pending.c_str());
    writeNew(pending,data);
    const bool exists = ::access(path.c_str(),F_OK)==0;
    if (exists)
    {
        std::remove(previous.c_str());
        if (std::rename(path.c_str(),previous.c_str())!=0)
            throw std::runtime_error("Cannot replace the previous setting.");
    }
    if (std::rename(pending.c_str(),path.c_str())!=0)
    {
        recoverSetting(path);
        throw std::runtime_error("Cannot publish the new setting.");
    }
    if (exists) std::remove(previous.c_str());
}
const std::string PreferencePath = std::string(Root) + "/editing.cfg";
}
EditorPreferences readEditorPreferences()
{
    recoverSetting(PreferencePath);
    if (::access(PreferencePath.c_str(),F_OK)!=0) return {};
    const auto data=readFile(PreferencePath,3);
    if (data.size!=3) throw std::runtime_error("Invalid editing preferences.");
    return {data.bytes[0]!=0,data.bytes[1]!=0,data.bytes[2]!=0};
}
void writeEditorPreferences(EditorPreferences p)
{
    const std::array<u8,3> bytes{u8(p.hax),u8(p.hardcore),u8(p.trainerOwnership)};
    replaceSetting(PreferencePath,bytes);
}
void deleteTrainerProfile(std::string_view name)
{
    const auto path=std::string(ProfileDirectory)+"/"+safeFolderName(name)+".profile";
    if (std::remove(path.c_str())!=0) throw std::runtime_error("Cannot remove this profile.");
}
void storeTrainerProfile(std::string_view name, std::string_view value)
{
    makeDirectory(ProfileDirectory);
    replaceSetting(std::string(ProfileDirectory) + "/" + safeFolderName(name) + ".profile",
        {reinterpret_cast<const u8*>(value.data()), value.size()});
}

std::vector<std::pair<std::string,std::string>> trainerProfiles()
{
    makeDirectory(ProfileDirectory);
    std::vector<std::pair<std::string,std::string>> result;
    auto closeDirectory=[](DIR* directory){ closedir(directory); };
    std::unique_ptr<DIR, decltype(closeDirectory)> dir(opendir(ProfileDirectory), closeDirectory);
    if (!dir) throw std::runtime_error("Cannot open the trainer profile folder.");
    while (auto* entry = readdir(dir.get()))
    {
        std::string name = entry->d_name;
        if (name.ends_with(".profile.old")) {
            const auto base = name.substr(0,name.size()-4);
            recoverSetting(std::string(ProfileDirectory)+"/"+base);
            name=base;
        }
        if (!name.ends_with(".profile")) continue;
        auto data = readFile(std::string(ProfileDirectory) + "/" + name, 1024);
        result.emplace_back(name.substr(0, name.size()-8), std::string(reinterpret_cast<char*>(data.bytes.get()),data.size));
    }
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}

std::unique_ptr<Sav> stageSave(Sav& active)
{
    if (compactStorage(active))
        return copyCompactSave(active);
    auto detached = serialize(active);
    return Sav::getSave(detached.bytes, detached.size);
}

std::string exportSave(const Save& save)
{
    if (!save.writable || !save.dirty)
        throw std::runtime_error("There are no edits to export.");
    const auto bytes = serialize(*save.data);
    const auto folder = uniqueDirectory(Exports);
    const auto path = folder + "/" + fileLeaf(save.file.path);
    try
    {
        writeNew(path, {bytes.bytes.get(), bytes.size});
    }
    catch (...)
    {
        ::rmdir(folder.c_str());
        throw;
    }
    return path;
}

File snapshot(const Save& save, std::string_view label)
{
    if (!save.data)
        throw std::runtime_error("Open a save before making a snapshot.");
    const auto title = safeFolderName(label);
    const auto bytes = save.writable ? serialize(*save.data) : readFile(save.file.path, 16 * 1024 * 1024);
    const std::string folder = std::string(Snapshots) + "/" + title;
    if (::mkdir(folder.c_str(), 0777) != 0)
        throw std::runtime_error("That snapshot name already exists or cannot be created.");
    const auto leaf = fileLeaf(save.file.path);
    auto path = folder + "/" + leaf;
    try
    {
        writeNew(path, {bytes.bytes.get(), bytes.size});
    }
    catch (...)
    {
        ::rmdir(folder.c_str());
        throw;
    }
    return {title + "/" + leaf, std::move(path)};
}

std::string restoreSnapshot(Save& current, const File& selected)
{
    auto previous = openSave(selected);
    if (!previous.writable)
        throw std::runtime_error("The selected snapshot is read-only.");
    if (current.data && (current.data->version() != previous.data->version() ||
        current.data->getLength() != previous.data->getLength()))
        throw std::runtime_error("This snapshot is from a different game or save revision.");
    if (current.data && !current.writable)
        throw std::runtime_error("The open save is read-only.");
    previous.dirty = true;
    if (current.data)
        previous.file = current.file;
    const auto output = exportSave(previous);
    previous.dirty = false;
    // Treat the released copy as the base for future snapshots.
    previous.file.path = output;
    current = std::move(previous);
    return output;
}

File storeBagPreset(const Save& save, std::string_view name)
{
    if (!save.data)
        throw std::runtime_error("Open a save to make a bag preset.");
    // Record only normal pouches with an available item list. Save the game identifier
    // so the same numeric item cannot acquire an unrelated meaning in another game.
    std::vector<u8> data{'P', 'K', 'B', 'A', 'G', 1, u8(save.data->version())};
    for (const auto [pouch, capacity] : save.data->pouches())
    {
        const auto permitted = bagCatalog(*save.data, pouch);
        if (permitted.empty())
            continue;
        std::set<unsigned> seen;
        for (int slot = 0; slot < capacity; ++slot)
        {
            const auto stack = bagSlot(*save.data, pouch, slot);
            if (!stack.count || !std::ranges::binary_search(permitted, stack.id))
                continue;
            if (!seen.insert(stack.id).second)
                throw std::runtime_error("Combine duplicate item stacks before saving this preset.");
            data.push_back(static_cast<u8>(pouch));
            const auto id = LittleEndian::convertFrom<u16>(stack.id);
            const auto count = LittleEndian::convertFrom<u16>(stack.count);
            data.insert(data.end(), id.begin(), id.end());
            data.insert(data.end(), count.begin(), count.end());
        }
    }
    if (data.size() == 7)
        throw std::runtime_error("The bag has no items for a preset.");
    const auto title = safeFolderName(name);
    const auto folder = std::string(BagPresets) + "/" + title;
    if (::mkdir(folder.c_str(), 0777) != 0)
        throw std::runtime_error("That bag preset name is already in use or cannot be created.");
    const auto path = folder + "/items.bin";
    try { writeNew(path, data); }
    catch (...) { ::rmdir(folder.c_str()); throw; }
    return {title, path};
}

std::vector<BagPresetEntry> readBagPreset(const File& file, const Sav& save)
{
    const auto raw = readFile(file.path, 64 * 1024);
    const auto* bytes = raw.bytes.get();
    if (raw.size < 12 || (raw.size - 7) % 5 || std::memcmp(bytes, "PKBAG\x01", 6) != 0)
        throw std::runtime_error("Invalid bag preset.");
    if (bytes[6] != u8(save.version()))
        throw std::runtime_error("Choose a bag preset from the same game.");
    std::vector<BagPresetEntry> entries;
    for (size_t i = 7; i < raw.size; i += 5)
    {
        const auto pouch = static_cast<Sav::Pouch>(bytes[i]);
        const auto item = LittleEndian::convertTo<u16>(bytes + i + 1);
        const auto count = LittleEndian::convertTo<u16>(bytes + i + 3);
        if (item == 0 || count == 0 || count > 9999 || pouch > Sav::Pouch::Donut)
            throw std::runtime_error("The bag preset contains an invalid item stack.");
        entries.push_back({pouch, item, count});
    }
    return entries;
}

std::string exportShowdown(std::string_view text)
{
    if (text.empty())
        throw std::runtime_error("There is no Showdown set to export.");
    const auto folder = uniqueDirectory(Exports);
    const auto path = folder + "/showdown.txt";
    try
    {
        writeNew(path, {reinterpret_cast<const u8*>(text.data()), text.size()});
    }
    catch (...)
    {
        ::rmdir(folder.c_str());
        throw;
    }
    return path;
}

std::string publishSavePair(const Save& current, const Save& donor)
{
    const auto first = serialize(*current.data);
    const auto second = serialize(*donor.data);
    const auto final = uniqueDirectory(Exports);
    const auto pending = std::string(Exports) + "/." + fileLeaf(final);
    const auto targetDir = pending + "/destination";
    const auto donorDir = pending + "/source";
    const auto targetPath = targetDir + "/" + fileLeaf(current.file.path);
    const auto donorPath = donorDir + "/" + fileLeaf(donor.file.path);
    try
    {
        if (::mkdir(pending.c_str(), 0777) != 0)
            throw std::runtime_error("Cannot stage the paired export.");
        makeDirectory(targetDir);
        makeDirectory(donorDir);
        writeNew(targetPath, {first.bytes.get(), first.size});
        writeNew(donorPath, {second.bytes.get(), second.size});
        if (::rmdir(final.c_str()) != 0 || std::rename(pending.c_str(), final.c_str()) != 0)
            throw std::runtime_error("Cannot publish both save copies.");
    }
    catch (...)
    {
        std::remove(targetPath.c_str());
        std::remove(donorPath.c_str());
        ::rmdir(targetDir.c_str());
        ::rmdir(donorDir.c_str());
        ::rmdir(pending.c_str());
        ::rmdir(final.c_str());
        throw;
    }
    return final;
}

std::string readShowdown()
{
    const auto input = readFile(std::string(Imports) + "/showdown.txt", 8192);
    return std::string(reinterpret_cast<const char*>(input.bytes.get()), input.size);
}

static std::string depositTo(const PKX& pokemon, const std::string& destination)
{
    if (!isOccupied(pokemon))
        throw std::runtime_error("Choose an occupied slot.");
    auto copy = pokemon.clone();
    copy->refreshChecksum();
    const std::string base = uniqueDirectory(destination);
    const auto species = copy->species();
    auto name = species.localize(pksm::Language::ENG);
    std::ranges::replace_if(name, [](unsigned char c)
        { return !std::isalnum(c) && c != '-'; }, '_');
    if (name.empty())
        name = std::to_string(static_cast<unsigned>(species));
    const auto path = base + "/" + name + std::string(copy->extension());
    try
    {
        writeNew(path, copy->rawData());
    }
    catch (...)
    {
        ::rmdir(base.c_str());
        throw;
    }
    return path;
}

void depositBatch(std::span<const PKX* const> entries)
{
    if (entries.empty())
        throw std::runtime_error("Mark at least one Pokemon.");
    const auto reserved = uniqueDirectory(Bank);
    const auto temporary = std::string(Bank) + "/." + fileLeaf(reserved);
    if (::mkdir(temporary.c_str(), 0777) != 0)
    {
        ::rmdir(reserved.c_str());
        throw std::runtime_error("Cannot stage a Bank batch.");
    }
    std::vector<std::string> paths;
    try
    {
        for (const auto* original : entries)
        {
            auto copy = original->clone();
            copy->refreshChecksum();
            auto path = temporary + "/" + std::to_string(paths.size() + 1) + "-" +
                std::to_string(static_cast<unsigned>(copy->species())) + std::string(copy->extension());
            writeNew(path, copy->rawData());
            paths.push_back(std::move(path));
        }
        if (::rmdir(reserved.c_str()) != 0 || std::rename(temporary.c_str(), reserved.c_str()) != 0)
            throw std::runtime_error("Cannot publish the Bank batch.");
    }
    catch (...)
    {
        for (const auto& path : paths)
            std::remove(path.c_str());
        ::rmdir(temporary.c_str());
        ::rmdir(reserved.c_str());
        throw;
    }
}

std::string deposit(const PKX& pokemon)
{
    return depositTo(pokemon, Bank);
}

void prepareExtras()
{
    makeDirectory(ItemBank);
    makeDirectory(Park);
}

void storeItem(StoredItem item)
{
    if (!item.id || item.id > 65535 || !item.count || item.count > 9999)
        throw std::runtime_error("Invalid Item Bank quantity.");
    const auto folder = uniqueDirectory(ItemBank);
    const std::array<u8, 4> data{u8(item.id), u8(item.id >> 8), u8(item.count), u8(item.count >> 8)};
    writeNew(folder + "/item.bin", data);
}

std::vector<StoredItem> loadItems()
{
    std::vector<StoredItem> result;
    for (const auto& file : listFiles(ItemBank, false, true))
    {
        if (!file.name.ends_with("/item.bin"))
            continue;
        try
        {
            const auto item = readFile(file.path, 4);
            if (item.size != 4)
                continue;
            const unsigned id = item.bytes[0] | (item.bytes[1] << 8);
            const unsigned count = item.bytes[2] | (item.bytes[3] << 8);
            if (id && count && count <= 9999)
                result.push_back({id, count});
        }
        catch (const std::runtime_error&) {}
    }
    return result;
}

void moveCopyToPark(const PKX& pokemon)
{
    if (pokemon.egg())
        throw std::runtime_error("Hatch the egg before adding it to the park.");
    if (listFiles(Park, true, true).size() >= 30)
        throw std::runtime_error("The park has reached its 30-resident limit.");
    depositTo(pokemon, Park);
}

std::vector<ParkEvent> parkJournal(const Resident& resident)
{
    const auto base = resident.pokemon.path.substr(0, resident.pokemon.path.find_last_of('/'));
    std::vector<ParkEvent> result;
    for (const auto& event : listFiles(base, false, true))
    {
        if (!event.name.ends_with("/care.bin"))
            continue;
        try
        {
            const auto input = readFile(event.path, 4);
            struct stat modified{};
            if (input.size == 4 && ::stat(event.path.c_str(), &modified) == 0)
                result.push_back({modified.st_mtime, input.bytes[0], input.bytes[1],
                    static_cast<unsigned>(input.bytes[2] | (input.bytes[3] << 8))});
        }
        catch (const std::runtime_error&) {}
    }
    std::ranges::stable_sort(result, std::greater{}, &ParkEvent::when);
    return result;
}

std::vector<Resident> parkResidents()
{
    std::vector<Resident> result;
    for (auto& file : listFiles(Park, true, true))
    {
        Resident resident{std::move(file)};
        for (const auto& event : parkJournal(resident))
        {
            resident.feeds += event.feeds;
            resident.plays += event.plays;
            resident.steps += event.steps;
        }
        result.push_back(std::move(resident));
    }
    return result;
}

void logParkActivity(const Resident& resident, unsigned feeds, unsigned plays, unsigned steps)
{
    if (feeds > 255 || plays > 255 || steps > 65535)
        throw std::runtime_error("This park session is too long to store.");
    const auto base = resident.pokemon.path.substr(0, resident.pokemon.path.find_last_of('/'));
    const auto folder = uniqueDirectory(base);
    const std::array<u8, 4> data{u8(feeds), u8(plays), u8(steps), u8(steps >> 8)};
    writeNew(folder + "/care.bin", data);
}

std::unique_ptr<PKX> bankPokemon(const File& file)
{
    const auto format = formatFor(extension(file.path));
    if (format == formats.end())
        throw std::runtime_error("Unsupported Pokemon format.");
    auto input = readFile(file.path, 1024);
    std::unique_ptr<PKX> pokemon;
    if (format->extension == ".pa9")
        pokemon = PKX::getPKM<pksm::PA9>(input.bytes.get(), input.size);
    else if (format->extension == ".pb8")
        pokemon = PKX::getPKM<pksm::PB8>(input.bytes.get(), input.size);
    else
        pokemon = PKX::getPKM(format->generation, input.bytes.get(), input.size);
    if (!pokemon || !isOccupied(*pokemon) || pokemon->extension() != format->extension)
        throw std::runtime_error("Invalid Pokemon file.");
    const auto checksum = pokemon->checksum();
    pokemon->refreshChecksum();
    if (pokemon->checksum() != checksum)
        throw std::runtime_error("Pokemon checksum mismatch.");
    return pokemon;
}

void putPokemon(Save& save, int box, int slot, PKX& pokemon)
{
    if (!save.writable || !save.data)
        throw std::runtime_error("Open an editable save first.");
    if (slot < 0 || slot >= boxSlotCount(*save.data, box))
        throw std::runtime_error("That box slot is unavailable.");
    const auto target = save.data->pkm(box, slot);
    if (isOccupied(*target))
        throw std::runtime_error("Choose an empty box slot.");
    if (target->extension() != pokemon.extension() ||
        save.data->invalidTransferReason(pokemon) != Sav::BadTransferReason::OKAY)
        throw std::runtime_error("This Pokemon is incompatible with the destination game.");
    save.data->pkm(pokemon, box, slot, false);
    save.dirty = true;
}
// A Bank move publishes the edited save immediately. Keep the SD input available throughout.
void moveToBank(Save& save, int box, int slot)
{
    if (!save.writable || !save.data || !canDeposit(*save.data)) throw std::runtime_error("Open an editable save.");
    Save draft{save.file,stageSave(*save.data),true,true};
    std::unique_ptr<PKX> pokemon;
    if (box < 0)
    {
        if (slot<0 || slot>=draft.data->partyCount() || draft.data->partyCount()<=1 ||
            draft.data->generation()==Generation::LGPE)
            throw std::runtime_error("Choose a boxed Pokemon or keep at least one party member.");
        pokemon=draft.data->pkm(slot);
        draft.data->pkm(*draft.data->emptyPkm(),slot);
        draft.data->fixParty();
    }
    else
    {
        pokemon=draft.data->pkm(box,slot);
        const std::array selection{slot};
        organizeBox(draft,box,selection,BoxAction::Release);
    }
    auto path=exportSave(draft);
    try { deposit(*pokemon); }
    catch (...) { std::remove(path.c_str()); ::rmdir(path.substr(0,path.find_last_of('/')).c_str()); throw; }
    draft.file.path=std::move(path); draft.dirty=false;
    save=std::move(draft);
}

void moveFromBank(Save& save, const File& bank, int box, int slot, const PKX& expected)
{
    if (!save.writable) throw std::runtime_error("Open an editable save.");
    auto original=bankPokemon(bank);
    if (!std::ranges::equal(expected.rawData(),original->rawData()))
        throw std::runtime_error("The Bank selection changed.");
    Save draft{save.file,stageSave(*save.data),true,true};
    auto placed=previewTransfer(draft,box,slot,*original);
    putPokemon(draft,box,slot,*placed);
    auto path=exportSave(draft);
    // Archive the consumed entry alongside the output. It can be recovered after an interrupted move.
    const auto folder=path.substr(0,path.find_last_of('/'));
    const auto receipt=folder+"/withdrawn-"+fileLeaf(bank.path)+".bak";
    if (std::rename(bank.path.c_str(),receipt.c_str())!=0)
    {
        std::remove(path.c_str()); ::rmdir(folder.c_str());
        throw std::runtime_error("The Bank entry could not be moved.");
    }
    draft.file.path=std::move(path); draft.dirty=false;
    save=std::move(draft);
}

}
