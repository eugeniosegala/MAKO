/* SPDX-License-Identifier: GPL-3.0-or-later */
// Generated from engine/mako-common/launcher_exclusions.json; do not edit.
// Regenerate: python3 scripts/generate-launcher-exclusions.py
#pragma once

#include <array>
#include <string_view>

namespace ls::detail {
    constexpr std::array<std::string_view, 3> excludedWindowsLauncherExecutables{
        std::string_view{"ubisoftconnect.exe"},
        std::string_view{"upc.exe"},
        std::string_view{"uplaywebcore.exe"},
    };
}
