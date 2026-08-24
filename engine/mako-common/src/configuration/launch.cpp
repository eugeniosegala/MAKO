/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/configuration/launch.hpp"
#include "mako-common/helpers/errors.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using namespace ls;

namespace {

    std::string_view trim(std::string_view value) {
        constexpr std::string_view whitespace{" \t\r\n"};
        const auto first = value.find_first_not_of(whitespace);
        if (first == std::string_view::npos)
            return {};
        const auto last = value.find_last_not_of(whitespace);
        return value.substr(first, last - first + 1);
    }

    bool parseBoolean(const std::string_view key, const std::string_view value) {
        if (value == "0")
            return false;
        if (value == "1")
            return true;
        throw ls::error(
            "launcher setting " + std::string(key) + " must be 0 or 1"
        );
    }

}

LaunchConfigFile::LaunchConfigFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open())
        throw ls::error("unable to open launcher configuration");

    bool versionSeen = false;
    bool enableZinkSeen = false;
    bool forceAlsaAudioSeen = false;
    std::string line;
    while (std::getline(input, line)) {
        const auto content = trim(line);
        if (content.empty() || content.starts_with('#'))
            continue;

        const auto separator = content.find('=');
        if (separator == std::string_view::npos ||
                content.find('=', separator + 1) != std::string_view::npos) {
            throw ls::error("invalid launcher configuration line");
        }

        const auto key = trim(content.substr(0, separator));
        const auto value = trim(content.substr(separator + 1));
        if (key == "version") {
            if (versionSeen || value != "1")
                throw ls::error("unsupported launcher configuration version");
            versionSeen = true;
        } else if (key == "enable_zink") {
            if (enableZinkSeen)
                throw ls::error("duplicate enable_zink launcher setting");
            this->launchConf.enable_zink = parseBoolean(key, value);
            enableZinkSeen = true;
        } else if (key == "force_alsa_audio") {
            if (forceAlsaAudioSeen)
                throw ls::error("duplicate force_alsa_audio launcher setting");
            this->launchConf.force_alsa_audio = parseBoolean(key, value);
            forceAlsaAudioSeen = true;
        } else {
            throw ls::error("unknown launcher setting: " + std::string(key));
        }
    }

    if (!versionSeen)
        throw ls::error("launcher configuration version is missing");
}

void LaunchConfigFile::write(const std::filesystem::path& path) const {
    try {
        std::filesystem::create_directories(path.parent_path());
        if (!std::filesystem::exists(path.parent_path()))
            throw ls::error("unable to create launcher configuration directory");

        std::ofstream output(path, std::ios::trunc);
        if (!output.is_open())
            throw ls::error("unable to open launcher configuration for writing");

        output << "version=" << LaunchConfigFile::formatVersion << '\n'
            << "enable_zink="
            << static_cast<int>(this->launchConf.enable_zink) << '\n'
            << "force_alsa_audio="
            << static_cast<int>(this->launchConf.force_alsa_audio) << '\n';
        output.close();
        if (!output)
            throw ls::error("unable to write launcher configuration");
    } catch (const std::filesystem::filesystem_error& error) {
        throw ls::error("unable to write launcher configuration", error);
    }
}

std::filesystem::path ls::findLaunchConfigurationFile() {
    const char* explicitPath = std::getenv("MAKO_LAUNCH_CONFIG");
    if (explicitPath && *explicitPath != '\0')
        return {explicitPath};

    const char* xdgPath = std::getenv("XDG_CONFIG_HOME");
    if (xdgPath && *xdgPath != '\0')
        return std::filesystem::path(xdgPath)
            / "mako-render" / "launcher.conf";

    const char* homePath = std::getenv("HOME");
    if (homePath && *homePath != '\0')
        return std::filesystem::path(homePath)
            / ".config" / "mako-render" / "launcher.conf";

    return "/etc/mako-render/launcher.conf";
}
