/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/helpers/paths.hpp"
#include "mako-common/helpers/errors.hpp"

#include <cstdlib>
#include <filesystem>
#include <vector>

std::filesystem::path ls::makoFgPackagePath() {
    std::filesystem::path path;
    if (const char* value = std::getenv("MAKO_FG_PACKAGE"); value && *value)
        path = value;
    else if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data)
        path = std::filesystem::path(data) / "mako-render/models/mako-fg";
    else if (const char* home = std::getenv("HOME"); home && *home)
        path = std::filesystem::path(home) / ".local/share/mako-render/models/mako-fg";
    if (!path.is_absolute())
        throw ls::error("MAKO FG requires an absolute model package path");
    return path;
}

std::filesystem::path ls::findShaderDll() {
    const std::vector<std::filesystem::path> FRAGMENTS{{
        ".local/share/Steam/steamapps/common",
        ".steam/steam/steamapps/common",
        ".steam/debian-installation/steamapps/common",
        ".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common",
        "snap/steam/common/.local/share/Steam/steamapps/common"
    }};

    // check XDG overridden location
    const char* xdgPath = std::getenv("XDG_DATA_HOME");
    if (xdgPath && *xdgPath != '\0') {
        auto base = std::filesystem::path(xdgPath);

        auto full = base / "Steam/steamapps/common" / "Lossless Scaling" / "Lossless.dll";
        std::error_code error;
        if (std::filesystem::is_regular_file(full, error))
            return full;
    }

    // check home directory
    const char* homePath = std::getenv("HOME");
    if (homePath && *homePath != '\0') {
        auto base = std::filesystem::path(homePath);

        for (const auto& frag : FRAGMENTS) {
            auto full = base / frag / "Lossless Scaling" / "Lossless.dll";
            std::error_code error;
            if (std::filesystem::is_regular_file(full, error))
                return full;
        }
    }

    // fallback to same directory
    auto local = std::filesystem::current_path() / "Lossless.dll";
    std::error_code error;
    if (std::filesystem::is_regular_file(local, error))
        return local;

    throw ls::error("unable to locate Lossless.dll, please set the path in the configuration");
}

std::optional<std::filesystem::path> ls::makoFgPackageOverride() {
    if (const char* value = std::getenv("MAKO_FG_PACKAGE"); value && *value)
        return makoFgPackagePath();
    return std::nullopt;
}
