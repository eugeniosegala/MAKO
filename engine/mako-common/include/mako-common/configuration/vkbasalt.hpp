/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ls {

    struct VkBasaltConf {
        bool enabled{false};
        std::string sharpening{"cas"};
        float sharpness{0.5F};
        float dls_denoise{0.2F};
        std::string antialiasing{"none"};
        std::string shader{"none"};
    };

    inline constexpr float vkBasaltStrengthMinimum = 0.0F;
    inline constexpr float vkBasaltStrengthMaximum = 1.0F;

    [[nodiscard]] bool isVkBasaltSharpening(std::string_view value) noexcept;
    [[nodiscard]] bool isVkBasaltAntialiasing(std::string_view value) noexcept;
    [[nodiscard]] bool isVkBasaltShader(std::string_view value) noexcept;

    [[nodiscard]] std::filesystem::path findVkBasaltConfigurationFile();

    [[nodiscard]] std::string mergeVkBasaltConfiguration(
        std::string_view existing,
        const VkBasaltConf& settings,
        const std::filesystem::path& shaderDirectory
    );

    void writeVkBasaltConfiguration(
        const std::filesystem::path& path,
        const VkBasaltConf& settings,
        const std::filesystem::path& shaderDirectory
    );

}
