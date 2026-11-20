// SPDX-License-Identifier: GPL-3.0-or-later
#include "webp_portrait.hpp"
#include "src/webp/decode.h"

#include <algorithm>
#include <cstdio>

namespace forge::ui
{
std::shared_ptr<WebpPortrait> WebpPortrait::load(const std::string& path)
{
    const std::unique_ptr<FILE, decltype(&std::fclose)> input(std::fopen(path.c_str(), "rb"), std::fclose);
    if (!input || std::fseek(input.get(), 0, SEEK_END))
        return {};
    const long length = std::ftell(input.get());
    if (length < 32 || length > 32 * 1024 * 1024 || std::fseek(input.get(), 0, SEEK_SET))
        return {};
    auto result = std::make_shared<WebpPortrait>();
    result->file.resize(length);
    if (std::fread(result->file.data(), 1, length, input.get()) != static_cast<size_t>(length))
        return {};
    const WebPData bytes{result->file.data(), result->file.size()};
    WebPBitstreamFeatures features{};
    if (WebPGetFeatures(bytes.bytes, bytes.size, &features) != VP8_STATUS_OK || !features.has_animation ||
        features.width < 1 || features.height < 1 || features.width > 1024 || features.height > 1024)
        return {};
    result->decoder.reset(WebPAnimDecoderNew(&bytes, nullptr));
    WebPAnimInfo info{};
    if (!result->decoder || !WebPAnimDecoderGetInfo(result->decoder.get(), &info) ||
        info.frame_count < 2 || info.frame_count > 768)
        return {};
    result->width = features.width;
    result->height = features.height;
    return result->nextFrame() ? result : nullptr;
}

bool WebpPortrait::nextFrame()
{
    if (!WebPAnimDecoderHasMoreFrames(decoder.get()))
    {
        WebPAnimDecoderReset(decoder.get());
        timestamp = 0;
    }
    int end = 0;
    std::uint8_t* next = nullptr;
    if (!WebPAnimDecoderGetNext(decoder.get(), &next, &end))
        return false;
    rgba = next;
    remainingMs += std::max(10, end - timestamp);
    timestamp = end;
    return true;
}

void WebpPortrait::advance(double seconds)
{
    if (stopped)
        return;
    const double now = seconds * 1000.0;
    // Resume from the visible pose after a suspend or a long screen transition.
    fractionalMs += std::clamp(now - lastMs, 0.0, 100.0);
    lastMs = now;
    const int elapsed = static_cast<int>(fractionalMs);
    fractionalMs -= elapsed;
    remainingMs -= elapsed;
    while (remainingMs <= 0)
        if (!nextFrame())
        {
            stopped = true;
            // Restore the valid first pose if a later frame is damaged.
            WebPAnimDecoderReset(decoder.get());
            timestamp = 0;
            nextFrame();
            return;
        }
}

int WebpPortrait::nextUpdateMs() const
{
    return std::clamp(remainingMs, 8, 100);
}

void WebpPortrait::draw(std::uint16_t* dest, int stride, int size) const
{
    const int longest = std::max(width, height);
    const int w = std::max(1, width * size / longest);
    const int h = std::max(1, height * size / longest);
    dest += ((size - h) / 2) * stride + (size - w) / 2;
    for (int y = 0; y < h; ++y)
    {
        const auto* row = rgba + (y * height / h) * width * 4;
        for (int x = 0; x < w; ++x)
        {
            const auto* p = row + (x * width / w) * 4;
            const unsigned a = p[3];
            if (a == 0)
                continue;
            unsigned r = p[0] >> 3, g = p[1] >> 2, b = p[2] >> 3;
            if (a != 255)
            {
                const auto bg = dest[x];
                r = (r * a + (bg >> 11) * (255 - a) + 127) / 255;
                g = (g * a + ((bg >> 5) & 63) * (255 - a) + 127) / 255;
                b = (b * a + (bg & 31) * (255 - a) + 127) / 255;
            }
            dest[x] = (r << 11) | (g << 5) | b;
        }
        dest += stride;
    }
}
}
