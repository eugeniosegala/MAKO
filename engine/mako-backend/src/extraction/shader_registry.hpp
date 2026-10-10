/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/vulkan/shader.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace mako::backend {

    struct DllResourceArchive;

    /// shader collection struct
    struct Shaders {
        std::array<vk::Shader, 4> alpha;
        std::array<vk::Shader, 5> beta;
        std::array<vk::Shader, 5> gamma;
        std::array<vk::Shader, 10> delta;
    };

    /// shader registry struct
    struct ShaderRegistry {
        vk::Shader mipmaps;
        vk::Shader generate, generate_hdr;
        std::vector<uint8_t> generate_pq_packed_source;
        vk::Shader hdr10_pq_to_scrgb;
        vk::Shader scrgb_to_hdr10_pq;
        std::optional<vk::Shader> scrgb_to_hdr10_pq_packed;
        Shaders quality;
        Shaders performance;

        [[nodiscard]] vk::Shader packedPqShader(const vk::Vulkan& vk) const;

        bool is_fp16; //!< whether the fp16 shader variants were loaded
    };

    /// build a shader registry from resources
    /// @param vk Vulkan instance
    /// @param fp16 whether to load fp16 variants
    /// @param archive immutable model DLL archive
    /// @return constructed shader registry
    /// @throws ls::error if shaders are missing
    /// @throws vk::vulkan_error on Vulkan errors
    ShaderRegistry buildShaderRegistry(const vk::Vulkan& vk, bool fp16,
        const DllResourceArchive& archive, const std::filesystem::path& dll);

}
