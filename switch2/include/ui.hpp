// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "portrait_model.hpp"
#include "webp_portrait.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace forge::ui
{
using Pixel = std::uint16_t;
constexpr int Width = 1280;
constexpr int Height = 720;
constexpr Pixel rgb(unsigned hex)
{
    return ((hex >> 8) & 0xf800) | ((hex >> 5) & 0x07e0) | ((hex >> 3) & 0x001f);
}

constexpr Pixel Deep = rgb(0x171b32);
constexpr Pixel Deck = rgb(0x1b2447);
constexpr Pixel Grid = rgb(0x243964);
constexpr Pixel Edge = rgb(0x355795);
constexpr Pixel Blue = rgb(0x2789cd);
constexpr Pixel Cyan = rgb(0x42bfe8);
constexpr Pixel Ink = rgb(0xf4f8ff);
constexpr Pixel Soft = rgb(0xa8badc);
constexpr Pixel Well = rgb(0x041f46);
constexpr Pixel Band = rgb(0x091328);
constexpr Pixel Gold = rgb(0xf6d778);

enum class Font { Small, Body, Title, Hero, Album, Caption };
enum class Art { Species, Shiny, Chrome, Home, Game, Idle };

struct PortraitLook
{
    unsigned species = 0, form = 0, sweet = 0;
    bool shiny = false, female = false, cosplay = false, gigantamax = false;

    std::uint32_t key() const
    {
        return species << 15 | form << 7 | sweet << 4 | std::uint32_t(gigantamax) << 3 |
            std::uint32_t(cosplay) << 2 | std::uint32_t(shiny) << 1 | std::uint32_t(female);
    }
};

class Assets
{
    struct Glyph
    {
        int advance = 0, x = 0, y = 0, width = 0, height = 0;
        std::vector<unsigned char> coverage;
    };
    struct Atlas
    {
        std::unique_ptr<FILE, decltype(&std::fclose)> file{nullptr, std::fclose};
        std::vector<std::uint32_t> offsets;
        long base = 0;
        int size = 48;
    };
    struct Record
    {
        Art art;
        unsigned id;
        std::vector<Pixel> pixels;
    };

    std::array<std::array<Glyph, 95>, 4> fonts;
    std::array<std::unordered_map<unsigned,Glyph>,2> albumFonts;
    struct Portrait { std::uint32_t look; std::uint32_t id; };
    std::array<Atlas, 6> atlases;
    std::vector<Portrait> portraits;
    std::vector<Portrait> loops;
    std::vector<Record> cache;
    struct IdleClip
    {
        int duration = 0, count = 0, resolution = 0;
        std::array<Pixel, 256> palette{};
        std::vector<unsigned char> frames;
    };
    std::shared_ptr<IdleClip> clip;
    unsigned clipId = ~0u, modelKey = ~0u;
    std::string modelsDirectory, portraitsDirectory;
    unsigned webpKey = ~0u;
    std::shared_ptr<WebpPortrait> activeWebp;
    std::shared_ptr<WebpPortrait> webp(const PortraitLook& look);
    std::shared_ptr<PortraitModel> activeModel;

    const Glyph& glyph(Font font, unsigned c) const;
    const Record& picture(Art art, unsigned id);
    std::vector<unsigned char> encodedPicture(Art art, unsigned id, size_t limit);
    std::shared_ptr<const IdleClip> idleClip(unsigned id);
    std::shared_ptr<PortraitModel> model(const PortraitLook& look);
    void loadIndex(const std::string& path, std::vector<Portrait>& entries, Art art);
    friend class Canvas;

public:
    explicit Assets(const std::string& root, std::string models = {}, std::string portraits = {},
        const std::string& media = "sdmc:/switch/PKForge/media/ui");
};

class Canvas
{
    Assets& assets;
    std::vector<Pixel> surface = std::vector<Pixel>(Width * Height);
    struct IdlePortrait
    {
        int x = 0, y = 0, size = 0;
        std::uint32_t key = 0;
        std::shared_ptr<const Assets::IdleClip> clip;
        std::shared_ptr<PortraitModel> model;
        std::shared_ptr<WebpPortrait> webp;
        std::vector<Pixel> background;
    } idle;
    void drawIdle(Pixel* output, int stride, float seconds, bool advance = true) const;

public:
    explicit Canvas(Assets& assets) : assets(assets) {}
    const std::vector<Pixel>& pixels() const { return surface; }
    void rect(int x, int y, int w, int h, Pixel color);
    void round(int x, int y, int w, int h, int radius, Pixel color);
    void panel(int x, int y, int w, int h, Pixel fill = Deck, Pixel edge = Edge, int radius = 12);
    void gradient(int x, int y, int w, int h, Pixel top, Pixel bottom);
    int measure(std::string_view text, Font font = Font::Body) const;
    int text(int x, int y, std::string_view text, Font font = Font::Body,
        Pixel color = Ink, int maxWidth = 1200);
    bool art(Art art, unsigned id, int centerX, int centerY, int size);
    bool portrait(PortraitLook look, int centerX, int centerY, int size);
    bool animates() const { return idle.size > 0 && (!idle.webp || idle.webp->playing()); }
    std::uint32_t animationKey() const { return animates() ? idle.key : 0; }
    int animationStepMs() const { return idle.webp ? idle.webp->nextUpdateMs() : idle.model ? 34 : 52; }
    void animatePortrait(Pixel* output, int stride, float seconds) const;
    void background();
    void focus(int x, int y, int w, int h);
};

Pixel mix(Pixel from, Pixel to, unsigned weight);
void present(Pixel* output, int stride, const std::vector<Pixel>& next,
    const std::vector<Pixel>& previous, float progress, bool slide);
}
