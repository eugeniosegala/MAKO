/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "wine_compatibility.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

using namespace mako::layer;

namespace {
    [[nodiscard]] std::optional<std::string_view> environmentValue(
            const std::string_view name) {
        const char* const value = std::getenv(std::string(name).c_str());
        return value ? std::optional<std::string_view>{value} : std::nullopt;
    }

    // Wine's own loader; a different Proton has a different path.
    [[nodiscard]] std::string wineBuildIdentity() {
        std::error_code error;
        const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
        return error ? std::string() : path.string();
    }

    [[nodiscard]] std::optional<std::string> readMarker(
            const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return std::nullopt;
        std::string content{std::istreambuf_iterator<char>(input), {}};
        while (!content.empty() && content.back() == '\n')
            content.pop_back();
        return content;
    }

    // Write through a temporary file so a launcher never reads a partial
    // marker. Failures leave the game running without generation.
    [[nodiscard]] bool writeMarker(const std::filesystem::path& path,
            const std::string_view content) noexcept {
        try {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error)
                return false;
            auto temporary = path;
            temporary += ".tmp";
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output)
                    return false;
                output << content << '\n';
                if (!output)
                    return false;
            }
            std::filesystem::rename(temporary, path, error);
            if (error)
                std::filesystem::remove(temporary, error);
            return !error;
        } catch (...) {
            return false;
        }
    }
}

std::string mako::layer::wineCompatibilityMarkerStem(const std::string_view profileName) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string stem;
    stem.reserve(profileName.size() * 2);
    for (const char character : profileName) {
        const auto byte = static_cast<unsigned char>(character);
        stem += digits[byte >> 4];
        stem += digits[byte & 0x0f];
    }
    return stem;
}

WineChildWindowFix mako::layer::resolveWineChildWindowFix(
        const std::string_view profileName,
        const std::filesystem::path& configDirectory,
        const std::optional<bool> drawsMainWindow) noexcept {
    try {
        const auto base = configDirectory / std::string(wineCompatibilityDirectory) /
            wineCompatibilityMarkerStem(profileName);
        auto marker = base;
        marker += std::string(wineChildWindowMarkerSuffix);
        auto ineffective = base;
        ineffective += std::string(wineChildWindowIneffectiveSuffix);

        // A failure recorded with another Wine build says nothing about this
        // one. Remove it so the launcher may apply the switch again.
        const auto build = wineBuildIdentity();
        bool previouslyIneffective = false;
        if (const auto failedBuild = readMarker(ineffective)) {
            previouslyIneffective = *failedBuild == build;
            std::error_code error;
            if (!previouslyIneffective)
                std::filesystem::remove(ineffective, error);
        }
        const auto setting = environmentValue(wineCompatibilitySettingEnvironment);
        const auto launcherProfile = environmentValue("MAKO_PROFILE");
        const auto fix = wineChildWindowFixFor(
            setting == "0",
            !profileName.empty() && setting == "1" && launcherProfile == profileName,
            environmentValue(wineChildWindowSwitchEnvironment) == "1",
            previouslyIneffective,
            drawsMainWindow
        );
        if (fix == WineChildWindowFix::RestartRequired && !writeMarker(marker, "1"))
            return WineChildWindowFix::Manual;
        if (fix == WineChildWindowFix::Ineffective && !previouslyIneffective &&
                !profileName.empty())
            static_cast<void>(writeMarker(ineffective, build));
        return fix;
    } catch (...) {
        return WineChildWindowFix::NotEligible;
    }
}
