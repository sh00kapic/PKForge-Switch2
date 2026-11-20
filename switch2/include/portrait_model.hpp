// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace forge::ui
{
class PortraitModel
{
    struct Vertex
    {
        std::array<float, 3> position;
        std::array<std::uint8_t, 4> joints;
        std::array<std::uint8_t, 4> weights;
        std::array<std::int16_t, 2> uv;
    };

    struct Triangle
    {
        std::array<std::uint16_t, 3> vertices;
        std::uint16_t material;
        std::uint16_t tint;
    };

    struct Texture
    {
        std::uint16_t width = 0, height = 0;
        std::vector<std::uint16_t> pixels;
    };

    struct Projected
    {
        float x, y, depth, u, v;
    };

    using Transform = std::array<float, 12>;
    int jointCount = 0, frameCount = 0, fps = 0;
    std::vector<Vertex> vertices;
    std::vector<Triangle> triangles;
    std::vector<Texture> textures;
    std::vector<Transform> poses;
    std::vector<Transform> interpolated;
    std::vector<Projected> projected;
    std::vector<float> depth;

public:
    static std::unique_ptr<PortraitModel> load(const std::string& path);
    void draw(std::uint16_t* surface, int stride, int size, float seconds);
    bool animated() const { return frameCount > 1; }
};
}
