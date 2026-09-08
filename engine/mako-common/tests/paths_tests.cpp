/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/helpers/paths.hpp"
#include "mako-common/helpers/errors.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

int main() {
    const auto temporary = std::filesystem::temp_directory_path() /
        ("mako-dll-paths-" + std::to_string(::getpid()));
    const auto originalDirectory = std::filesystem::current_path();
    try {
        const auto home = temporary / "home";
        const auto xdg = temporary / "custom data";
        const auto current = temporary / "game";
        std::filesystem::create_directories(current);
        std::filesystem::current_path(current);
        ::setenv("HOME", home.c_str(), 1);
        ::setenv("XDG_DATA_HOME", xdg.c_str(), 1);
        const auto xdgDll = xdg /
            "Steam/steamapps/common/Lossless Scaling/Lossless.dll";
        const auto homeDll = home /
            ".local/share/Steam/steamapps/common/Lossless Scaling/Lossless.dll";
        const auto localDll = current / "Lossless.dll";
        const auto write = [](const std::filesystem::path& path) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path) << "synthetic discovery fixture, not a model";
        };
        const auto expect = [](const std::filesystem::path& path) {
            if (ls::findShaderDll() != path)
                throw std::runtime_error("DLL discovery chose the wrong candidate");
        };
        write(localDll);
        write(homeDll);
        write(xdgDll);
        expect(xdgDll);
        std::filesystem::remove(xdgDll);
        std::filesystem::create_directory(xdgDll);
        expect(homeDll); // A directory named Lossless.dll is not an input.
        std::filesystem::remove(homeDll);
        std::filesystem::create_symlink(temporary / "missing", homeDll);
        expect(localDll); // A stale installation does not suppress later paths.
        std::filesystem::remove(localDll);
        bool failed = false;
        try { static_cast<void>(ls::findShaderDll()); }
        catch (const ls::error&) { failed = true; }
        if (!failed) throw std::runtime_error("missing DLL was accepted");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::filesystem::current_path(originalDirectory);
        std::filesystem::remove_all(temporary);
        return 1;
    }
    std::filesystem::current_path(originalDirectory);
    std::filesystem::remove_all(temporary);
    return 0;
}
