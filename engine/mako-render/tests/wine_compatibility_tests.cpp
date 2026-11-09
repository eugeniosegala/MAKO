/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "wine_compatibility.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

using namespace mako::layer;

namespace {
    int failures = 0;

    void expect(const bool condition, const char* message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }

    void testDecision() {
        expect(wineChildWindowFixFor(false, true, false, false, true) ==
                WineChildWindowFix::RestartRequired,
            "A main window drawn offscreen must ask for the Wine switch");
        expect(wineChildWindowFixFor(true, true, false, false, true) ==
                WineChildWindowFix::Disabled,
            "A profile that turned the fix off must not record it");
        expect(wineChildWindowFixFor(false, true, true, false, true) ==
                WineChildWindowFix::Ineffective,
            "A window still offscreen with the switch exported must report it ineffective");
        expect(wineChildWindowFixFor(false, true, false, true, true) ==
                WineChildWindowFix::Ineffective,
            "A profile whose switch failed with this Wine build must not ask again");
        expect(wineChildWindowFixFor(false, true, false, false, false) ==
                WineChildWindowFix::NotEligible &&
                wineChildWindowFixFor(false, true, false, false, std::nullopt) ==
                    WineChildWindowFix::NotEligible,
            "A child or unknown window must not receive the switch");
        expect(wineChildWindowFixFor(false, false, false, false, true) ==
                WineChildWindowFix::Manual,
            "A launcher that cannot apply the marker must not promise a restart");
        expect(wineChildWindowFixName(WineChildWindowFix::RestartRequired) ==
                "restart-required" &&
                wineChildWindowFixName(WineChildWindowFix::Manual) == "manual" &&
                wineChildWindowFixName(WineChildWindowFix::NotEligible) == "not-eligible",
            "State names are part of the runtime status contract");
    }

    void testMarkerStem() {
        expect(wineCompatibilityMarkerStem("Half-Life-2") == "48616c662d4c6966652d32",
            "Profile names must map to lowercase hexadecimal bytes");
        expect(wineCompatibilityMarkerStem("a/b") == "612f62",
            "A path separator in a profile name must not reach the file system");
        expect(wineCompatibilityMarkerStem("").empty(),
            "An empty profile name has an empty stem");
    }

    void testMarkers() {
        const auto directory = std::filesystem::temp_directory_path() /
            ("mako-wine-compatibility-tests-" + std::to_string(::getpid()));
        std::filesystem::remove_all(directory);
        const auto base = directory / "wine-compatibility" /
            wineCompatibilityMarkerStem("Half-Life-2");
        const auto marker = std::filesystem::path(base.string() + ".vk-child-window");
        const auto ineffective =
            std::filesystem::path(base.string() + ".vk-child-window-ineffective");
        ::unsetenv("WINE_DISABLE_VK_CHILD_WINDOW_RENDERING_HACK");
        ::unsetenv("MAKO_WINE_COMPATIBILITY_FIX");
        ::unsetenv("MAKO_PROFILE");

        expect(resolveWineChildWindowFix("Half-Life-2", directory, true) ==
                    WineChildWindowFix::Manual && !std::filesystem::exists(marker),
            "Without the launcher's setting and profile the fix must stay manual");
        ::setenv("MAKO_WINE_COMPATIBILITY_FIX", "1", 1);
        ::setenv("MAKO_PROFILE", "Other", 1);
        expect(resolveWineChildWindowFix("Half-Life-2", directory, true) ==
                    WineChildWindowFix::Manual && !std::filesystem::exists(marker),
            "A launcher that identified another profile cannot apply this marker");
        ::setenv("MAKO_PROFILE", "Half-Life-2", 1);
        expect(resolveWineChildWindowFix("Half-Life-2", directory, false) ==
                    WineChildWindowFix::NotEligible && !std::filesystem::exists(marker),
            "A child window must not leave a marker");
        ::setenv("MAKO_WINE_COMPATIBILITY_FIX", "0", 1);
        expect(resolveWineChildWindowFix("Half-Life-2", directory, true) ==
                    WineChildWindowFix::Disabled && !std::filesystem::exists(marker),
            "A disabled profile must not leave a marker");
        ::setenv("MAKO_WINE_COMPATIBILITY_FIX", "1", 1);
        expect(resolveWineChildWindowFix("Half-Life-2", directory, true) ==
                    WineChildWindowFix::RestartRequired &&
                std::filesystem::exists(marker) && !std::filesystem::exists(ineffective),
            "A main window must leave the marker the launcher reads");
        ::setenv("WINE_DISABLE_VK_CHILD_WINDOW_RENDERING_HACK", "1", 1);
        expect(resolveWineChildWindowFix("Half-Life-2", directory, true) ==
                    WineChildWindowFix::Ineffective &&
                std::filesystem::exists(ineffective),
            "A switch that did not help must leave the ineffective marker");
        ::unsetenv("WINE_DISABLE_VK_CHILD_WINDOW_RENDERING_HACK");
        expect(resolveWineChildWindowFix("Half-Life-2", directory, true) ==
                WineChildWindowFix::Ineffective,
            "A later launch with the same Wine build must remember the failure");

        { std::ofstream(ineffective, std::ios::trunc) << "/another/proton/wine-preloader\n"; }
        expect(resolveWineChildWindowFix("Half-Life-2", directory, true) ==
                    WineChildWindowFix::RestartRequired &&
                !std::filesystem::exists(ineffective),
            "A failure with another Wine build must be retried and forgotten");

        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        { std::ofstream(directory / "wine-compatibility") << "not a directory\n"; }
        expect(resolveWineChildWindowFix("Half-Life-2", directory, true) ==
                WineChildWindowFix::Manual,
            "A marker that cannot be written must not promise a restart");
        expect(resolveWineChildWindowFix("", directory, true) ==
                WineChildWindowFix::Manual,
            "A process without a profile name cannot be fixed by its launcher");
        ::unsetenv("MAKO_WINE_COMPATIBILITY_FIX");
        ::unsetenv("MAKO_PROFILE");
        std::filesystem::remove_all(directory);
    }
}

int main() {
    testDecision();
    testMarkerStem();
    testMarkers();
    if (failures == 0)
        std::cout << "Wine compatibility tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
