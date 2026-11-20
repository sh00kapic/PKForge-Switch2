// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <memory>
#include <string>
#include <vector>

namespace forge
{
struct EngineResult
{
    std::string text;
    std::vector<std::uint8_t> changed;
};
EngineResult runEngine(const std::string& resourceRoot, std::span<const std::uint8_t> input,
    const std::vector<std::string>& arguments);
}

namespace forge
{
class EngineJob
{
    struct State;
    std::unique_ptr<State> state;
public:
    EngineJob(std::shared_ptr<const std::vector<std::uint8_t>> input, std::vector<std::string> arguments);
    ~EngineJob();
    bool ready() const;
    EngineResult take();
};
}
