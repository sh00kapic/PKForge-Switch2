// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui.hpp"
#include "lz4.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace forge::ui
{
namespace
{
void read(FILE* f, void* dest, size_t size)
{
    if (!f || std::fread(dest, 1, size, f) != size)
        throw std::runtime_error("Cannot read PKForge artwork or fonts.");
}
}

Assets::Assets(const std::string& root, std::string models, std::string portraitsPath, const std::string& media)
    : modelsDirectory(std::move(models)), portraitsDirectory(std::move(portraitsPath))
{
    constexpr std::array faces{"small", "body", "title", "hero"};
    for (size_t i = 0; i < faces.size(); ++i)
    {
        const auto path = root + "/" + faces[i] + ".font";
        const std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path.c_str(), "rb"), std::fclose);
        for (auto& glyph : fonts[i])
        {
            unsigned char info[5];
            read(file.get(), info, sizeof(info));
            glyph.advance = info[0];
            glyph.x = static_cast<std::int8_t>(info[1]);
            glyph.y = static_cast<std::int8_t>(info[2]);
            glyph.width = info[3];
            glyph.height = info[4];
            glyph.coverage.resize(glyph.width * glyph.height);
            if (!glyph.coverage.empty())
                read(file.get(), glyph.coverage.data(), glyph.coverage.size());
        }
    }

    for (unsigned face=0;face<albumFonts.size();++face) {
        auto path=root+(face==0?"/album.font":"/caption.font");
        const std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path.c_str(),"rb"),std::fclose);
        uint32_t count;
        read(file.get(),&count,4);
        if (count>65536) throw std::runtime_error("Invalid album font.");
        for (unsigned i=0;i<count;++i) {
            uint32_t code;
            unsigned char data[5];
            read(file.get(),&code,4); read(file.get(),data,5);
            auto& g=albumFonts[face][code];
            g.advance=data[0]; g.x=static_cast<int8_t>(data[1]); g.y=static_cast<int8_t>(data[2]);
            g.width=data[3]; g.height=data[4]; g.coverage.resize(g.width*g.height);
            if (!g.coverage.empty()) read(file.get(),g.coverage.data(),g.coverage.size());
        }
    }
    constexpr std::array names{"species", "shiny", "chrome", "home", "games", "idle"};
    constexpr std::array sizes{48, 48, 96, 128, 128, 0};
    std::array<bool, names.size()> externalAtlas{};
    for (size_t i = 0; i < names.size(); ++i)
    {
        auto& atlas = atlases[i];
        const auto path = media + "/" + names[i] + ".art";
        atlas.file.reset(std::fopen(path.c_str(), "rb"));
        externalAtlas[i] = static_cast<bool>(atlas.file);
        if (!atlas.file)
            atlas.file.reset(std::fopen((root + "/" + names[i] + ".art").c_str(), "rb"));
        if (!atlas.file) continue;
        // A partial SD copy disables only this optional atlas.
        try
        {
            std::uint32_t count = 0;
            read(atlas.file.get(), &count, sizeof(count));
            if (count > 8192)
                throw std::runtime_error("Invalid PKForge artwork index.");
            atlas.size = sizes[i];
            atlas.offsets.resize(count + 1);
            read(atlas.file.get(), atlas.offsets.data(), atlas.offsets.size() * sizeof(std::uint32_t));
            atlas.base = std::ftell(atlas.file.get());
            if (atlas.base < 0 || atlas.offsets.front() || !std::ranges::is_sorted(atlas.offsets))
                throw std::runtime_error("Invalid PKForge artwork offsets.");
        }
        catch (const std::runtime_error&)
        {
            atlas.file.reset();
            atlas.offsets.clear();
        }
    }
    loadIndex((externalAtlas[static_cast<size_t>(Art::Home)] ? media : root) + "/home.idx",
        portraits, Art::Home);
    loadIndex((externalAtlas[static_cast<size_t>(Art::Idle)] ? media : root) + "/idle.idx",
        loops, Art::Idle);
    cache.reserve(64);
}

void Assets::loadIndex(const std::string& path, std::vector<Portrait>& entries, Art art)
{
    const std::unique_ptr<FILE, decltype(&std::fclose)> index(
        std::fopen(path.c_str(), "rb"), std::fclose);
    if (!index || !atlases[static_cast<size_t>(art)].file) return;
    try
    {
        if (std::fseek(index.get(), 0, SEEK_END) != 0)
            throw std::runtime_error("Cannot open the portrait index.");
        const long length = std::ftell(index.get());
        if (length <= 0 || length % sizeof(Portrait) != 0 ||
            static_cast<unsigned long>(length) > 16384 * sizeof(Portrait))
            throw std::runtime_error("Invalid PKForge portrait index.");
        entries.resize(length / sizeof(Portrait));
        std::rewind(index.get());
        read(index.get(), entries.data(), length);
        const auto count = atlases[static_cast<size_t>(art)].offsets.size() - 1;
        if (!std::ranges::is_sorted(entries, {}, &Portrait::look) ||
            std::ranges::any_of(entries, [=](const auto& entry) { return entry.id >= count; }))
            throw std::runtime_error("Invalid PKForge portrait entries.");
    }
    catch (const std::runtime_error&)
    {
        entries.clear();
    }
}

std::vector<unsigned char> Assets::encodedPicture(Art art, unsigned id, size_t limit)
{
    const auto& atlas = atlases[static_cast<size_t>(art)];
    if (!atlas.file || atlas.offsets.empty() || id >= atlas.offsets.size() - 1)
        return {};
    const auto start = atlas.offsets[id];
    const auto bytes = atlas.offsets[id + 1] - start;
    if (!bytes || bytes > limit || std::fseek(atlas.file.get(), atlas.base + start, SEEK_SET) != 0)
        return {};
    std::vector<unsigned char> encoded(bytes);
    if (std::fread(encoded.data(), 1, bytes, atlas.file.get()) != bytes)
        return {};
    return encoded;
}

std::shared_ptr<const Assets::IdleClip> Assets::idleClip(unsigned id)
{
    if (clipId == id)
        return clip;
    clipId = id;
    clip.reset();
    constexpr size_t HeaderSize = 4 + 256 * sizeof(Pixel);
    const auto encoded = encodedPicture(Art::Idle, id, 600000);
    if (encoded.size() <= HeaderSize)
        return {};
    const int count = encoded[2];
    const int side = encoded[3];
    const int duration = encoded[0] | (encoded[1] << 8);
    if (count < 2 || count > 30 || side < 8 || side > 128 || duration < 40)
        return {};
    auto decoded = std::make_shared<IdleClip>();
    decoded->count = count;
    decoded->resolution = side;
    decoded->duration = duration;
    decoded->frames.resize(count * side * side);
    std::memcpy(decoded->palette.data(), encoded.data() + 4, 256 * sizeof(Pixel));
    if (LZ4_decompress_safe(reinterpret_cast<const char*>(encoded.data() + HeaderSize),
        reinterpret_cast<char*>(decoded->frames.data()), encoded.size() - HeaderSize,
        decoded->frames.size()) != static_cast<int>(decoded->frames.size()))
        return {};
    const auto area = side * side;
    for (size_t i = area; i < decoded->frames.size(); ++i)
        decoded->frames[i] ^= decoded->frames[i - area];
    clip = std::move(decoded);
    return clip;
}

namespace
{
template<class Loader>
auto loadPortraitFile(const std::string& directory, const PortraitLook& look, const char* extension, Loader load)
{
    auto path = directory + "/" + std::to_string(look.species) + "-" + std::to_string(look.form);
    if (look.species == 869 && !look.gigantamax)
        path += "-" + std::to_string(look.sweet);
    if (look.cosplay) path += "c";
    if (look.gigantamax) path += "g";
    const std::string suffix = (look.shiny ? "-s" : "") + std::string(extension);
    if (look.female)
        if (auto result = load(path + "-f" + suffix))
            return result;
    return load(path + suffix);
}
}

std::shared_ptr<WebpPortrait> Assets::webp(const PortraitLook& look)
{
    if (!look.species || portraitsDirectory.empty())
        return {};
    if (webpKey != look.key())
    {
        webpKey = look.key();
        activeWebp.reset();
        activeWebp = loadPortraitFile(portraitsDirectory, look, ".webp", WebpPortrait::load);
    }
    return activeWebp;
}

std::shared_ptr<PortraitModel> Assets::model(const PortraitLook& look)
{
    if (!look.species || modelsDirectory.empty())
        return {};
    if (modelKey == look.key())
        return activeModel;
    modelKey = look.key();
    activeModel = loadPortraitFile(modelsDirectory, look, ".pfm", PortraitModel::load);
    return activeModel;
}

const Assets::Glyph& Assets::glyph(Font font, unsigned c) const
{
    if (font==Font::Album || font==Font::Caption) {
        const auto& face=albumFonts[font==Font::Caption];
        const auto it=face.find(c);
        return it==face.end()?face.at('?'):it->second;
    }
    return fonts[static_cast<size_t>(font)][(c >= 32 && c < 127 ? c : '?') - 32];
}

const Assets::Record& Assets::picture(Art art, unsigned id)
{
    if (auto found = std::ranges::find_if(cache, [=](const Record& item)
        { return item.art == art && item.id == id; }); found != cache.end())
    {
        std::rotate(found, found + 1, cache.end());
        return cache.back();
    }

    auto& atlas = atlases[static_cast<size_t>(art)];
    Record item{art, id, {}};
    const size_t area = atlas.size * atlas.size;
    const auto encoded = encodedPicture(art, id, area * 3);
    const bool indexed = art == Art::Home;
    constexpr size_t paletteBytes = 128 * sizeof(Pixel);
    if (!encoded.empty() && (!indexed || encoded.size() >= paletteBytes))
    {
        std::array<Pixel, 128> palette{};
        if (indexed)
            std::memcpy(palette.data(), encoded.data(), paletteBytes);
        item.pixels.resize(area);
        size_t pos = 0;
        for (size_t in = indexed ? paletteBytes : 0; in < encoded.size(); )
        {
            const int header = encoded[in++];
            const size_t count = (header & 127) + 1;
            if (pos + count > area)
                break;
            if ((header & 128) == 0)
            {
                const auto bytes = count * (indexed ? 1 : sizeof(Pixel));
                if (in + bytes > encoded.size())
                    break;
                if (indexed)
                    for (size_t j = 0; j < count; ++j)
                        item.pixels[pos + j] = palette[encoded[in + j] & 127];
                else
                    std::memcpy(item.pixels.data() + pos, encoded.data() + in, bytes);
                in += bytes;
            }
            pos += count;
        }
        if (pos != area || std::ranges::none_of(item.pixels, [](Pixel pixel) { return pixel != 0; }))
            item.pixels.clear();
    }
    if (cache.size() == 64)
        cache.erase(cache.begin());
    cache.push_back(std::move(item));
    return cache.back();
}

Pixel mix(Pixel a, Pixel b, unsigned phase)
{
    const unsigned inverse = 32 - phase;
    const unsigned rb = ((a & 0xf81f) * inverse + (b & 0xf81f) * phase) >> 5;
    const unsigned g = ((a & 0x07e0) * inverse + (b & 0x07e0) * phase) >> 5;
    return (rb & 0xf81f) | (g & 0x07e0);
}

void Canvas::rect(int x, int y, int w, int h, Pixel color)
{
    const int left = std::clamp(x, 0, Width);
    const int right = std::clamp(x + w, 0, Width);
    if (left >= right)
        return;
    for (int row = std::max(0, y); row < std::min(Height, y + h); ++row)
        std::fill(surface.data() + row * Width + left, surface.data() + row * Width + right, color);
}

void Canvas::round(int x, int y, int w, int h, int r, Pixel color)
{
    r = std::min({r, w / 2, h / 2});
    rect(x, y + r, w, h - 2 * r, color);
    for (int n = 0; n < r; ++n)
    {
        const int dy = r - n;
        const int inset = r - static_cast<int>(std::sqrt(r * r - dy * dy));
        rect(x + inset, y + n, w - 2 * inset, 1, color);
        rect(x + inset, y + h - 1 - n, w - 2 * inset, 1, color);
    }
}

void Canvas::panel(int x, int y, int w, int h, Pixel fill, Pixel edge, int radius)
{
    round(x, y, w, h, radius, edge);
    round(x + 2, y + 2, w - 4, h - 4, std::max(0, radius - 2), fill);
}

void Canvas::gradient(int x, int y, int w, int h, Pixel top, Pixel bottom)
{
    for (int row = 0; row < h; ++row)
        rect(x, y + row, w, 1, mix(top, bottom, row * 32 / std::max(1, h - 1)));
}

static unsigned nextGlyph(std::string_view& value)
{
    unsigned c=static_cast<unsigned char>(value.front());
    value.remove_prefix(1);
    if (c<128) return c;
    int remaining=c<0xe0?1:c<0xf0?2:3;
    c &= 0x7f >> (remaining+1);
    while (remaining-->0) {
        if (value.empty() || (value.front() & 0xc0)!=0x80) return '?';
        c=(c<<6)|(value.front()&63);
        value.remove_prefix(1);
    }
    return c;
}

int Canvas::measure(std::string_view value, Font font) const
{
    int width = 0;
    while (!value.empty())
        width += assets.glyph(font, nextGlyph(value)).advance;
    return width;
}

int Canvas::text(int x, int y, std::string_view value, Font font, Pixel color, int maxWidth)
{
    const int left = x;
    const bool truncate = measure(value, font) > maxWidth;
    const int ellipsis = measure("...", font);
    const auto paint = [&](unsigned c)
    {
        const auto& g = assets.glyph(font, c);
        for (int row = 0; row < g.height; ++row)
        {
            const int py = y + g.y + row;
            if (py < 0 || py >= Height)
                continue;
            for (int col = 0; col < g.width; ++col)
            {
                const int px = x + g.x + col;
                const auto opacity = g.coverage[row * g.width + col];
                if (px >= 0 && px < Width && opacity)
                {
                    auto& pixel = surface[py * Width + px];
                    pixel = mix(pixel, color, (opacity + 4) >> 3);
                }
            }
        }
        x += g.advance;
    };
    while (!value.empty())
    {
        const unsigned c=nextGlyph(value);
        if (truncate && x - left + assets.glyph(font, c).advance + ellipsis > maxWidth)
        {
            for (int dot = 0; dot < 3; ++dot)
                paint('.');
            break;
        }
        paint(c);
    }
    return x - left;
}

bool Canvas::art(Art art, unsigned id, int centerX, int centerY, int size)
{
    const auto& picture = assets.picture(art, id).pixels;
    const int sourceSize = assets.atlases[static_cast<size_t>(art)].size;
    if (picture.empty())
    {
        if (art != Art::Species && art != Art::Shiny && art != Art::Chrome)
            return false;
        const bool chrome = art == Art::Chrome;
        static constexpr std::array icons{"SD", "BK", "SD", "*", "+", "o"};
        std::string value = chrome ? (id < icons.size() ? icons[id] : "o") :
            (id ? std::to_string(id) : "Egg");
        const auto font = size >= 90 ? Font::Title : Font::Small;
        const int w = std::max(measure(value,font)+12, size*2/3);
        const int h = size >= 90 ? 47 : 28;
        panel(centerX-w/2,centerY-h/2,w,h,Deck,art==Art::Shiny?Gold:Edge,6);
        text(centerX-measure(value,font)/2,centerY-h/2+1,value,font,art==Art::Shiny?Gold:Ink);
        return true;
    }
    const int x = centerX - size / 2;
    const int y = centerY - size / 2;
    for (int py = std::max(0, y); py < std::min(Height, y + size); ++py)
        for (int px = std::max(0, x); px < std::min(Width, x + size); ++px)
        {
            const auto color = picture[((py - y) * sourceSize / size) * sourceSize +
                (px - x) * sourceSize / size];
            if (color)
                surface[py * Width + px] = color;
        }
    return true;
}

bool Canvas::portrait(PortraitLook look, int centerX, int centerY, int size)
{
    idle.size = 0;
    idle.clip.reset();
    idle.model.reset();
    idle.webp.reset();
    if (look.species > 65535 || look.form > 255 || look.sweet > 6 || size <= 0)
        return false;

    const int x = centerX - size / 2;
    const int y = centerY - size / 2;
    const bool inBounds = x >= 0 && y >= 0 && x + size <= Width && y + size <= Height;
    const auto find = [](const auto& entries, std::uint32_t key) -> const Assets::Portrait*
    {
        const auto match = std::ranges::lower_bound(entries, key, {}, &Assets::Portrait::look);
        return match != entries.end() && match->look == key ? &*match : nullptr;
    };
    const auto showMotion = [&]()
    {
        idle.x = x;
        idle.y = y;
        idle.size = size;
        idle.key = look.key();
        idle.background.resize(size * size);
        for (int row = 0; row < size; ++row)
            std::copy_n(surface.data() + (y + row) * Width + x, size,
                idle.background.data() + row * size);
        drawIdle(surface.data(), Width, 0, false);
        if (idle.model && !idle.model->animated())
            idle.size = 0;
        return true;
    };
    const auto showArt = [&](const PortraitLook& variant)
    {
        if (inBounds)
            if (const auto* loop = find(assets.loops, variant.key()))
                if ((idle.clip = assets.idleClip(loop->id)))
                    return showMotion();
        const auto* match = find(assets.portraits, variant.key());
        return match && art(Art::Home, match->id, centerX, centerY, size);
    };

    if (inBounds && (idle.model = assets.model(look)))
        return showMotion();

    if (inBounds && (idle.webp = assets.webp(look)))
        return showMotion();

    if (showArt(look))
        return true;
    const bool shiny = look.shiny;
    const bool female = look.female;
    look.female = false;
    if (female && showArt(look))
        return true;
    look.shiny = false;
    look.female = female;
    if (shiny && showArt(look))
        return true;
    look.female = false;
    if (shiny && female && showArt(look))
        return true;
    if (look.form || look.sweet || look.gigantamax || look.cosplay)
    {
        look.form = look.sweet = 0;
        look.cosplay = look.gigantamax = false;
        look.shiny = shiny;
        if (showArt(look))
            return true;
        look.shiny = false;
        if (shiny && showArt(look))
            return true;
    }
    return art(shiny ? Art::Shiny : Art::Species, look.species, centerX, centerY, size);
}

void Canvas::drawIdle(Pixel* output, int stride, float seconds, bool advance) const
{
    auto* region = output + idle.y * stride + idle.x;
    for (int row = 0; row < idle.size; ++row)
        std::copy_n(idle.background.data() + row * idle.size, idle.size, region + row * stride);

    if (idle.model)
    {
        idle.model->draw(region, stride, idle.size, seconds);
        return;
    }
    if (idle.webp)
    {
        if (advance)
            idle.webp->advance(seconds);
        idle.webp->draw(region, stride, idle.size);
        return;
    }
    const auto& clip = *idle.clip;
    const auto elapsed = std::fmod(seconds * 1000.0f, static_cast<float>(clip.duration));
    const int frame = std::min(clip.count - 1, static_cast<int>(elapsed * clip.count / clip.duration));
    const auto* source = clip.frames.data() + frame * clip.resolution * clip.resolution;
    for (int row = 0; row < idle.size; ++row)
    {
        const auto* sourceRow = source + row * clip.resolution / idle.size * clip.resolution;
        for (int column = 0; column < idle.size; ++column)
        {
            const auto color = clip.palette[sourceRow[column * clip.resolution / idle.size]];
            if (color)
                region[row * stride + column] = color;
        }
    }
}

void Canvas::animatePortrait(Pixel* output, int stride, float seconds) const
{
    if (animates() && output && stride >= Width)
        drawIdle(output, stride, std::max(0.0f, seconds));
}

void Canvas::background()
{
    idle.size = 0;
    idle.clip.reset();
    idle.model.reset();
    idle.webp.reset();
    gradient(0, 0, Width, Height, Deck, Deep);
    for (int x = 0; x < Width; x += 24)
        rect(x, 0, 1, Height, Grid);
    for (int y = 0; y < Height; y += 24)
        rect(0, y, Width, 1, Grid);
}

void Canvas::focus(int x, int y, int w, int h)
{
    constexpr int a = 13;
    constexpr int thickness = 3;
    for (const int ty : {y, y + h - thickness})
    {
        rect(x, ty, a, thickness, Cyan);
        rect(x + w - a, ty, a, thickness, Cyan);
    }
    for (const int tx : {x, x + w - thickness})
    {
        rect(tx, y, thickness, a, Cyan);
        rect(tx, y + h - a, thickness, a, Cyan);
    }
}

void present(Pixel* output, int stride, const std::vector<Pixel>& next,
    const std::vector<Pixel>& previous, float progress, bool slide)
{
    const bool moving = progress < 1.0f && previous.size() == next.size();
    const float eased = 1.0f - (1.0f - progress) * (1.0f - progress);
    const unsigned weight = static_cast<unsigned>(eased * 32);
    const int shift = static_cast<int>((1.0f - eased) * 22);
    for (int y = 0; y < Height; ++y)
    {
        const auto* target = next.data() + y * Width;
        auto* row = output + y * stride;
        if (!moving || y < 96 || y >= 616)
        {
            std::copy_n(target, Width, row);
            continue;
        }
        const auto* old = previous.data() + y * Width;
        const auto* incoming = slide ? next.data() + std::max(96, y - shift) * Width : target;
        for (int x = 0; x < Width; ++x)
            row[x] = mix(old[x], incoming[x], weight);
    }
}
}
