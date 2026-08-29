/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-backend/ls1.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace mako::backend::detail {

    struct ShaderResourceContract {
        size_t sampledImages{0};
        size_t storageImages{0};
        size_t uniformBuffers{0};
        size_t samplers{0};

        [[nodiscard]] bool operator==(
            const ShaderResourceContract&) const = default;
    };

    struct LsfgShaderSpec {
        uint32_t logicalId{0};
        ShaderResourceContract contract;
    };

    /// Validate only invariants required to consume a DXBC compute container.
    /// Unknown chunks and trailing vendor metadata are accepted.
    void validateDxbcComputeShader(
        std::span<const uint8_t> data, const std::string& sourceName
    );

    /// Validate SPIR-V structure, a compute `main`, and the descriptor bindings
    /// MAKO will actually bind. Additional declarations are accepted and remain
    /// subject to Vulkan pipeline validation, avoiding brittle allowlisting.
    void validateSpirvComputeShader(
        std::span<const uint8_t> data,
        const ShaderResourceContract& required,
        const std::string& sourceName
    );

    [[nodiscard]] uint32_t lsfgResourceId(
        uint32_t logicalId, bool fp16, bool performance
    );

    [[nodiscard]] std::span<const LsfgShaderSpec> lsfgShaderSpecs(
        bool performance
    );

    [[nodiscard]] const std::vector<uint8_t>& validatedLsfgResource(
        const std::unordered_map<uint32_t, std::vector<uint8_t>>& resources,
        uint32_t logicalId, bool fp16, bool performance
    );

    void validateLsfgModelResources(
        const std::unordered_map<uint32_t, std::vector<uint8_t>>& resources,
        bool fp16, bool performance
    );

    void validateLs1ModelResources(
        const std::unordered_map<uint32_t, std::vector<uint8_t>>& resources,
        Ls1Mode mode
    );

}
