/* SPDX-License-Identifier: GPL-3.0-or-later */
// Generated from engine/mako-common/launcher_exclusions.json; do not edit.
// Regenerate: python3 scripts/generate-launcher-exclusions.py
#pragma once

#include <array>
#include <string_view>

namespace ls::detail {
    constexpr std::array<std::string_view, 11> excludedWindowsLauncherExecutables{
        std::string_view{"ubisoftconnect.exe"},
        std::string_view{"upc.exe"},
        std::string_view{"uplaywebcore.exe"},
        std::string_view{"redprelauncher.exe"},
        std::string_view{"redlauncher.exe"},
        std::string_view{"socialclubhelper.exe"},
        std::string_view{"rockstarservice.exe"},
        std::string_view{"rockstarerrorhandler.exe"},
        std::string_view{"eadesktop.exe"},
        std::string_view{"ealauncher.exe"},
        std::string_view{"eabackgroundservice.exe"},
    };
}
