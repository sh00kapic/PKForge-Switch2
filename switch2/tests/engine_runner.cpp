// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine_host.hpp"
#include "workspace.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cerr << "Usage: engine-runner INPUT OUTPUT BOX,SLOT:COMMAND [VALUE [MODE]]\n";
        return 2;
    }
    try
    {
        namespace fs = std::filesystem;
        const fs::path input = argv[1], output = argv[2];
        if (fs::exists(output)) throw std::runtime_error("Output already exists.");
        auto save = forge::openSave({input.filename().string(),input.string()});
        auto result = forge::runEngine("romfs/engine",forge::engineSnapshot(save),
            {"ForgeTools",argc>5?argv[5]:"0",argv[3],argc>4?argv[4]:"","owner",input.filename().string()});
        std::cout << result.text;
        if (!result.changed.empty())
        {
            forge::acceptEngineCopy(save,result.changed);
            auto bytes=forge::engineSnapshot(save);
            std::ofstream file(output,std::ios::binary);
            file.exceptions(std::ios::badbit|std::ios::failbit);
            file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
            file.close();
            auto reopened=forge::openSave({output.filename().string(),output.string()});
            if (!reopened.writable || reopened.data->version()!=save.data->version())
                throw std::runtime_error("Export failed to reopen.");
        }
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
