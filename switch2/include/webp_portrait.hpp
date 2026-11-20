// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "src/webp/demux.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace forge::ui
{
class WebpPortrait
{
    // The decoder borrows the file bytes and owns the current composited frame.
    std::vector<std::uint8_t> file;
    std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> decoder{nullptr, WebPAnimDecoderDelete};
    std::uint8_t* rgba = nullptr;
    int width = 0, height = 0, timestamp = 0, remainingMs = 0;
    double lastMs = 0, fractionalMs = 0;
    bool stopped = false;
    bool nextFrame();

public:
    static std::shared_ptr<WebpPortrait> load(const std::string& path);
    void advance(double seconds);
    void draw(std::uint16_t* dest, int stride, int size) const;
    int nextUpdateMs() const;
    bool playing() const { return !stopped; }
};
}
