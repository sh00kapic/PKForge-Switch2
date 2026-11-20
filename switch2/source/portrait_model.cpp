// SPDX-License-Identifier: GPL-3.0-or-later
#include "portrait_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace forge::ui
{
namespace
{
constexpr std::uint16_t NoTexture = 0xffff;
constexpr float UvScale = 4096.0f;

template<class T>
bool read(FILE* file, T* output, size_t count)
{
    return std::fread(output, sizeof(T), count, file) == count;
}

bool validCoordinates(const auto& values, float limit)
{
    return std::ranges::all_of(values, [limit](float value)
        { return std::isfinite(value) && std::abs(value) <= limit; });
}

std::uint16_t lit(std::uint16_t color, std::uint16_t tint, float brightness)
{
    const auto red = static_cast<unsigned>((color >> 11) * (tint >> 11) * brightness / 31);
    const auto green = static_cast<unsigned>(((color >> 5) & 63) * ((tint >> 5) & 63) * brightness / 63);
    const auto blue = static_cast<unsigned>((color & 31) * (tint & 31) * brightness / 31);
    return (red << 11) | (green << 5) | blue;
}

float wrap(float coordinate)
{
    return coordinate - std::floor(coordinate);
}
}

std::unique_ptr<PortraitModel> PortraitModel::load(const std::string& path)
{
    static_assert(sizeof(Vertex) == 24 && sizeof(Triangle) == 10);
    const auto closeFile = [](FILE* file) { std::fclose(file); };
    const std::unique_ptr<FILE, decltype(closeFile)> file(std::fopen(path.c_str(), "rb"), closeFile);
    if (!file)
        return {};
    std::array<char, 4> magic{};
    std::array<std::uint16_t, 6> counts{};
    if (!read(file.get(), magic.data(), magic.size()) || magic != std::array{'P', 'F', 'M', '1'} ||
        !read(file.get(), counts.data(), counts.size()))
        return {};
    const auto [vertexCount, triangleCount, joints, frames, textureCount, fps] = counts;
    if (!vertexCount || vertexCount > 16384 || !triangleCount || triangleCount > 32768 ||
        !joints || joints > 256 || !frames || frames > 240 || textureCount > 16 || !fps || fps > 60)
        return {};

    auto model = std::make_unique<PortraitModel>();
    model->jointCount = joints;
    model->frameCount = frames;
    model->fps = fps;
    model->vertices.resize(vertexCount);
    model->triangles.resize(triangleCount);
    if (!read(file.get(), model->vertices.data(), vertexCount) ||
        !read(file.get(), model->triangles.data(), triangleCount))
        return {};
    for (const auto& vertex : model->vertices)
    {
        unsigned weight = 0;
        for (size_t i = 0; i < vertex.joints.size(); ++i)
        {
            if (vertex.weights[i] && vertex.joints[i] >= joints)
                return {};
            weight += vertex.weights[i];
        }
        if (weight != 255 || !validCoordinates(vertex.position, 4))
            return {};
    }
    for (const auto& tri : model->triangles)
        if ((tri.material != NoTexture && tri.material >= textureCount) ||
            std::ranges::any_of(tri.vertices, [vertexCount](auto index) { return index >= vertexCount; }))
            return {};

    model->textures.resize(textureCount);
    for (auto& texture : model->textures)
    {
        if (!read(file.get(), &texture.width, 1) || !read(file.get(), &texture.height, 1) ||
            !texture.width || texture.width > 256 || !texture.height || texture.height > 256)
            return {};
        texture.pixels.resize(texture.width * texture.height);
        if (!read(file.get(), texture.pixels.data(), texture.pixels.size()))
            return {};
    }
    model->poses.resize(static_cast<size_t>(frames) * joints);
    if (!read(file.get(), model->poses.data(), model->poses.size()) ||
        !std::ranges::all_of(model->poses, [](const auto& pose) { return validCoordinates(pose, 64); }))
        return {};
    model->interpolated.resize(joints);
    model->projected.resize(vertexCount);
    return model;
}

void PortraitModel::draw(std::uint16_t* surface, int stride, int size, float seconds)
{
    if (!surface || size <= 0)
        return;
    depth.assign(size * size, std::numeric_limits<float>::max());
    const float frame = std::fmod(seconds * fps, static_cast<float>(frameCount));
    const int before = static_cast<int>(frame);
    const int after = (before + 1) % frameCount;
    const float fraction = frame - before;
    for (int joint = 0; joint < jointCount; ++joint)
        for (size_t component = 0; component < interpolated[joint].size(); ++component)
            interpolated[joint][component] = std::lerp(poses[before * jointCount + joint][component],
                poses[after * jointCount + joint][component], fraction);

    constexpr float yaw = 0.35f;
    const float sine = std::sin(yaw), cosine = std::cos(yaw);
    const float scale = size * 0.46f;
    const float middle = size * 0.5f;
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        const auto& vertex = vertices[i];
        const auto [vx, vy, vz] = vertex.position;
        float x = 0, y = 0, z = 0;
        for (size_t k = 0; k < vertex.joints.size(); ++k)
        {
            if (!vertex.weights[k])
                continue;
            const auto& matrix = interpolated[vertex.joints[k]];
            const float weight = vertex.weights[k] / 255.0f;
            x += weight * (matrix[0] * vx + matrix[1] * vy + matrix[2] * vz + matrix[3]);
            y += weight * (matrix[4] * vx + matrix[5] * vy + matrix[6] * vz + matrix[7]);
            z += weight * (matrix[8] * vx + matrix[9] * vy + matrix[10] * vz + matrix[11]);
        }
        const float distance = sine * x + cosine * y;
        projected[i] = {middle + scale * (cosine * x - sine * y),
            middle - scale * (0.99f * z + 0.14f * distance), 0.99f * distance - 0.14f * z,
            vertex.uv[0] / UvScale, vertex.uv[1] / UvScale};
    }

    for (const auto& triangle : triangles)
    {
        const auto& a = projected[triangle.vertices[0]];
        const auto& b = projected[triangle.vertices[1]];
        const auto& c = projected[triangle.vertices[2]];
        const float bx = b.x - a.x, by = b.y - a.y, bz = (b.depth - a.depth) * scale;
        const float cx = c.x - a.x, cy = c.y - a.y, cz = (c.depth - a.depth) * scale;
        const float area = bx * cy - by * cx;
        if (std::abs(area) < 0.01f)
            continue;
        const float nx = by * cz - bz * cy, ny = bz * cx - bx * cz;
        const float light = std::abs(0.3f * nx - 0.45f * ny + 0.84f * area) /
            std::sqrt(nx * nx + ny * ny + area * area);
        const float brightness = 0.65f + 0.35f * std::min(1.0f, light);
        const int left = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
        const int right = std::min(size - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
        const int top = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
        const int bottom = std::min(size - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
        const float stepB = cy / area, stepC = -by / area;
        const auto* texture = triangle.material == NoTexture ? nullptr : &textures[triangle.material];
        const auto flatColor = lit(0xffff, triangle.tint, brightness);
        for (int py = top; py <= bottom; ++py)
        {
            const float dy = py + 0.5f - a.y;
            const float dx = left + 0.5f - a.x;
            float wb = (dx * cy - dy * cx) / area;
            float wc = (dy * bx - dx * by) / area;
            for (int px = left; px <= right; ++px, wb += stepB, wc += stepC)
            {
                const float wa = 1.0f - wb - wc;
                if (wa < -0.0001f || wb < -0.0001f || wc < -0.0001f)
                    continue;
                const float distance = wa * a.depth + wb * b.depth + wc * c.depth;
                auto& z = depth[py * size + px];
                if (distance >= z)
                    continue;
                auto color = flatColor;
                if (texture)
                {
                    const int u = std::min<int>(texture->width - 1,
                        wrap(wa * a.u + wb * b.u + wc * c.u) * texture->width);
                    const int v = std::min<int>(texture->height - 1,
                        wrap(wa * a.v + wb * b.v + wc * c.v) * texture->height);
                    const auto sample = texture->pixels[v * texture->width + u];
                    if (!sample)
                        continue;
                    color = lit(sample, triangle.tint, brightness);
                }
                z = distance;
                surface[py * stride + px] = color;
            }
        }
    }
}
}
