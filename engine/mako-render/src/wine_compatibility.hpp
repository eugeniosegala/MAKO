/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mako::layer {

    /// How MAKO handles a game window that Wine renders offscreen. Proton 9
    /// renders a Vulkan window that has Win32 child windows into an offscreen
    /// client window and copies it to the screen once per game frame, so
    /// generated frames are never shown. Its Wine draws a game that uses its
    /// main window directly when WINE_DISABLE_VK_CHILD_WINDOW_RENDERING_HACK=1.
    /// Proton 7 and 8 redirect the window in place, which MAKO cannot detect.
    enum class WineChildWindowFix {
        Unneeded,
        RestartRequired,
        Ineffective,
        NotEligible,
        Disabled,
        Manual,
    };

    /// The launch contract shared with the MAKO Decky wrapper: a launcher that
    /// exports this profile's name and setting 1, and finds the profile's
    /// marker without the ineffective one, exports the Wine switch. The
    /// ineffective marker names the Wine build it failed with.
    inline constexpr std::string_view wineChildWindowSwitchEnvironment =
        "WINE_DISABLE_VK_CHILD_WINDOW_RENDERING_HACK";
    inline constexpr std::string_view wineCompatibilitySettingEnvironment =
        "MAKO_WINE_COMPATIBILITY_FIX";
    inline constexpr std::string_view wineCompatibilityDirectory = "wine-compatibility";
    inline constexpr std::string_view wineChildWindowMarkerSuffix = ".vk-child-window";
    inline constexpr std::string_view wineChildWindowIneffectiveSuffix =
        ".vk-child-window-ineffective";

    [[nodiscard]] constexpr std::string_view wineChildWindowFixName(
            const WineChildWindowFix fix) noexcept {
        switch (fix) {
            case WineChildWindowFix::Unneeded: return "unneeded";
            case WineChildWindowFix::RestartRequired: return "restart-required";
            case WineChildWindowFix::Ineffective: return "ineffective";
            case WineChildWindowFix::NotEligible: return "not-eligible";
            case WineChildWindowFix::Disabled: return "disabled";
            case WineChildWindowFix::Manual: return "manual";
        }
        return "unneeded";
    }

    /// Decide for a window that Wine renders offscreen. A switch that is
    /// already exported, or that once failed with this Wine build, cannot
    /// help; a window that is not the game's main window could be drawn in the
    /// wrong place without Wine's offscreen copy; and a launcher that did not
    /// identify the profile with the setting on cannot apply the marker.
    [[nodiscard]] constexpr WineChildWindowFix wineChildWindowFixFor(
            const bool settingDisabled, const bool launcherApplies,
            const bool switchExported, const bool previouslyIneffective,
            const std::optional<bool> drawsMainWindow) noexcept {
        if (settingDisabled)
            return WineChildWindowFix::Disabled;
        if (switchExported || previouslyIneffective)
            return WineChildWindowFix::Ineffective;
        if (drawsMainWindow != true)
            return WineChildWindowFix::NotEligible;
        if (!launcherApplies)
            return WineChildWindowFix::Manual;
        return WineChildWindowFix::RestartRequired;
    }

    /// Lowercase hexadecimal of the profile name's bytes, so every name maps to
    /// one safe file name that a shell launcher can derive with od.
    [[nodiscard]] std::string wineCompatibilityMarkerStem(std::string_view profileName);

    /// Read this process's switch, setting and launcher identity, and the
    /// profile's earlier failure with this Wine build, then decide and record
    /// the result for the next launch.
    [[nodiscard]] WineChildWindowFix resolveWineChildWindowFix(
        std::string_view profileName, const std::filesystem::path& configDirectory,
        std::optional<bool> drawsMainWindow) noexcept;

}
