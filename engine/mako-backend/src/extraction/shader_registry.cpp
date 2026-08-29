/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "shader_registry.hpp"
#include "../shaders/color_conversion_spirv.hpp"
#include "model_resource_validation.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/vulkan/shader.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

using namespace mako;
using namespace mako::backend;

namespace {
    /// get the source code for a shader
    const std::vector<uint8_t>& getShaderSource(uint32_t id, bool fp16, bool perf,
            const std::unordered_map<uint32_t, std::vector<uint8_t>>& resources) {
        return mako::backend::detail::validatedLsfgResource(
            resources, id, fp16, perf
        );
    }

    [[nodiscard]] const mako::backend::detail::LsfgShaderSpec& shaderSpec(
            const uint32_t id, const bool perf) {
        const auto specs = mako::backend::detail::lsfgShaderSpecs(perf);
        const auto found = std::ranges::find(
            specs, id, &mako::backend::detail::LsfgShaderSpec::logicalId
        );
        if (found == specs.end())
            throw ls::error("unknown LSFG shader contract: " + std::to_string(id));
        return *found;
    }

    [[nodiscard]] vk::Shader makeShader(
            const vk::Vulkan& vk, const uint32_t id,
            const bool fp16, const bool perf,
            const std::unordered_map<uint32_t, std::vector<uint8_t>>& resources) {
        const auto& contract = shaderSpec(id, perf).contract;
        return vk::Shader(
            vk, getShaderSource(id, fp16, perf, resources),
            contract.sampledImages, contract.storageImages,
            contract.uniformBuffers, contract.samplers
        );
    }
    /// patch the generate shader
    void patchGenerateShader(std::vector<uint8_t>& data, bool hdr) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
        auto* _ptr = data.data();
        const std::span<uint32_t> words(
            reinterpret_cast<uint32_t*>(_ptr),
            data.size() / sizeof(uint32_t)
        );
#pragma clang diagnostic pop

        const uint16_t SpvOpCapability = 17;
        const uint16_t SpvOpTypeImage = 25;
        const uint32_t SpvCapabilityStorageImageWriteWithoutFormat = 56;
        const uint32_t SpvCapabilityShader = 1;
        const uint32_t SpvImageFormatRgba16f = 2;
        const uint32_t SpvImageFormatRgba8 = 4;

        for (size_t i = 5; i < words.size();) {
            const uint32_t& word = words[i]; // NOLINT ([]-usage)
            const uint16_t wc = (word >> 16);
            const uint16_t op = word & 0xFFFF;

            // remove write without format capability
            if (op == SpvOpCapability && wc >= 2) {
                uint32_t& cap = words[i + 1]; // NOLINT ([]-usage)
                if (cap == SpvCapabilityStorageImageWriteWithoutFormat)
                    cap = SpvCapabilityShader;
            }

            // patch format in image instructions
            if (op == SpvOpTypeImage && wc >= 9) {
                const uint32_t sampled = words[i + 7]; // NOLINT ([]-usage)
                if (sampled == 2)
                    words[i + 8] = // NOLINT ([]-usage)
                        hdr ? SpvImageFormatRgba16f : SpvImageFormatRgba8;
            }

            i += wc ? wc : 1;
        }
    }
}

ShaderRegistry backend::buildShaderRegistry(const vk::Vulkan& vk, bool fp16,
        const std::unordered_map<uint32_t, std::vector<uint8_t>>& resources) {
    // patch the generate shader
    std::vector<uint8_t> generate_data = getShaderSource(256, fp16, false, resources);
    std::vector<uint8_t> generate_data_hdr = generate_data;
    patchGenerateShader(generate_data, false);
    patchGenerateShader(generate_data_hdr, true);

    // load all other shaders
#define SHADER(id) makeShader(vk, id, fp16, PERF, resources)

    return {
#define PERF false
        .mipmaps = SHADER(255),
        .generate = vk::Shader(vk, generate_data,
            shaderSpec(256, PERF).contract.sampledImages,
            shaderSpec(256, PERF).contract.storageImages,
            shaderSpec(256, PERF).contract.uniformBuffers,
            shaderSpec(256, PERF).contract.samplers),
        .generate_hdr = vk::Shader(vk, generate_data_hdr,
            shaderSpec(256, PERF).contract.sampledImages,
            shaderSpec(256, PERF).contract.storageImages,
            shaderSpec(256, PERF).contract.uniformBuffers,
            shaderSpec(256, PERF).contract.samplers),
        .hdr10_pq_to_scrgb = vk::Shader(
            vk, embedded::hdr10PqToScRgbSpirv, 1, 1, 0, 1
        ),
        .scrgb_to_hdr10_pq = vk::Shader(
            vk, embedded::scRgbToHdr10PqSpirv, 1, 1, 0, 1
        ),
        .scrgb_to_hdr10_pq_packed =
            vk.supportsStorageImageExtendedFormats()
                ? std::optional<vk::Shader>(std::in_place,
                    vk, embedded::scRgbToHdr10PqPackedSpirv, 1, 1, 0, 1)
                : std::nullopt,
        .quality = {
            .alpha = {
                SHADER(267), SHADER(268), SHADER(269), SHADER(270)
            },
            .beta = {
                SHADER(275), SHADER(276), SHADER(277), SHADER(278),
                SHADER(279)
            },
            .gamma = {
                SHADER(257), SHADER(259), SHADER(260), SHADER(261),
                SHADER(262)
            },
            .delta = {
                SHADER(257), SHADER(263), SHADER(264), SHADER(265),
                SHADER(266), SHADER(258), SHADER(271), SHADER(272),
                SHADER(273), SHADER(274)
            }
        },
#undef PERF
#define PERF true
        .performance = {
            .alpha = {
                SHADER(267), SHADER(268), SHADER(269), SHADER(270)
            },
            .beta = {
                SHADER(275), SHADER(276), SHADER(277), SHADER(278),
                SHADER(279)
            },
            .gamma = {
                SHADER(257), SHADER(259), SHADER(260), SHADER(261),
                SHADER(262)
            },
            .delta = {
                SHADER(257), SHADER(263), SHADER(264), SHADER(265),
                SHADER(266), SHADER(258), SHADER(271), SHADER(272),
                SHADER(273), SHADER(274)
            }
        },
#undef PERF
        .is_fp16 = fp16
    };

#undef SHADER
}
