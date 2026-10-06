/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ls {

    struct VkBasaltConf {
        bool enabled{false};
        std::string sharpening{"cas"};
        float sharpness{0.5F};
        float dls_denoise{0.2F};
        std::string antialiasing{"none"};
        std::string shader{"none"};
        bool manage_custom_shaders{false};
    };

    struct VkBasaltCustomShader {
        std::string name;
        std::string path;
    };

    inline constexpr float vkBasaltStrengthMinimum = 0.0F;
    inline constexpr float vkBasaltStrengthMaximum = 1.0F;

    [[nodiscard]] bool isVkBasaltSharpening(std::string_view value) noexcept;
    [[nodiscard]] bool isVkBasaltAntialiasing(std::string_view value) noexcept;
    [[nodiscard]] bool isVkBasaltShader(std::string_view value);
    [[nodiscard]] std::vector<VkBasaltCustomShader> customVkBasaltShaders(std::string_view content);
    [[nodiscard]] std::string vkBasaltShaderSelection(std::string_view content, const VkBasaltConf& settings);
    [[nodiscard]] std::string readVkBasaltConfiguration(const std::filesystem::path& path);
    [[nodiscard]] VkBasaltCustomShader addVkBasaltCustomShader(const std::filesystem::path& configPath, const std::filesystem::path& shaderPath);
    [[nodiscard]] std::string removeVkBasaltCustomShaders(std::string_view content, const std::vector<std::string>& shaderIds);

    // Atomic replacement for explicit profile edits and their rollback.
    void writeVkBasaltConfiguration(const std::filesystem::path& path, std::string_view content);

    [[nodiscard]] std::filesystem::path findVkBasaltConfigurationFile();

    [[nodiscard]] std::string mergeVkBasaltConfiguration(
        std::string_view existing,
        const VkBasaltConf& settings,
        const std::filesystem::path& shaderDirectory,
        std::string_view previousSelection = "none"
    );

    void writeVkBasaltConfiguration(
        const std::filesystem::path& path,
        const VkBasaltConf& settings,
        const std::filesystem::path& shaderDirectory,
        std::string_view previousSelection = "none"
    );

}
