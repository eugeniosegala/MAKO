/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <filesystem>

namespace ls {

    /// Process-start compatibility settings consumed by standalone mako-launch.
    struct LaunchConf {
        bool enable_zink{false};
        bool force_alsa_audio{false};
    };

    /// Strict standalone launcher configuration kept separate from Renderer TOML.
    class LaunchConfigFile {
    public:
        static constexpr int64_t formatVersion = 1;

        LaunchConfigFile() = default;
        explicit LaunchConfigFile(const std::filesystem::path& path);

        [[nodiscard]] auto& settings() { return this->launchConf; }
        [[nodiscard]] const auto& settings() const { return this->launchConf; }

        /// Write the canonical, shell-safe key/value format.
        void write(const std::filesystem::path& path) const;

    private:
        LaunchConf launchConf;
    };

    /// Find the standalone launcher configuration without reusing MAKO_CONFIG.
    [[nodiscard]] std::filesystem::path findLaunchConfigurationFile();

}
