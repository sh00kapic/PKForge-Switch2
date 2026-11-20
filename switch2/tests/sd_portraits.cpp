// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv)
{
    using namespace forge::ui;
    if (argc != 3)
        return 2;
    Assets assets("romfs/ui", {}, argv[1]);
    Canvas canvas(assets);
    std::ifstream manifest(argv[2]);
    if (!manifest)
        return 2;
    std::string row;
    unsigned count = 0;
    while (std::getline(manifest, row))
    {
        if (row.empty() || row.front() == '#')
            continue;
        auto stem = row.substr(0, row.find('\t'));
        std::ranges::replace(stem, '-', ' ');
        std::istringstream fields(stem);
        PortraitLook look;
        if (!(fields >> look.species >> look.form))
            return 2;
        fields >> look.sweet;
        for (const bool shiny : {false, true})
        {
            look.shiny = shiny;
            canvas.background();
            if (!canvas.portrait(look, 96, 96, 160) || !canvas.animates())
                return 1;
            auto pixels = canvas.pixels();
            for (int frame = 1; frame <= 80 && pixels == canvas.pixels(); ++frame)
                canvas.animatePortrait(pixels.data(), Width, frame * 0.04f);
            if (pixels == canvas.pixels())
            {
                std::cerr << "No visible motion for " << look.key() << '\n';
                return 1;
            }
            ++count;
        }
    }
    for (unsigned id = 1; id <= 1025; ++id)
        for (const bool shiny : {false, true})
            if (!canvas.portrait({id, 0, 0, shiny}, 96, 96, 160) || !canvas.animates())
                return 1;
    std::cout << count << " installed looks moved. All 2,050 base portraits have playback.\n";
}
