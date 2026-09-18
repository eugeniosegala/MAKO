/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-backend/ls1.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

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

    struct Ls1ShaderSpec {
        uint32_t resourceId;
        ShaderResourceContract contract;
        uint32_t storageImageFormat;
    };

    struct Ls1ModelSpec {
        Ls1ShaderSpec reconstruction;
        Ls1ShaderSpec stage1;
        std::optional<Ls1ShaderSpec> stage2;
        std::optional<Ls1ShaderSpec> stage3;
    };

    [[nodiscard]] Ls1ModelSpec ls1ModelSpec(Ls1Mode mode, uint32_t variant);

    struct SpirvShaderInfo {
        bool declaresFloat16{false};
        bool exactBindings{false};
    };

    /// Validate only invariants required to consume a DXBC compute container.
    /// Unknown chunks and trailing vendor metadata are accepted.
    void validateDxbcComputeShader(
        std::span<const uint8_t> data, const std::string& sourceName
    );

    /// Check known SM5.0 reflection when available. Discovery requires it and
    /// an exact interface; canonical loading permits missing/unknown metadata
    /// and additional bindings, but never a contradictory required binding.
    void validateDxbcResourceBindings(
        std::span<const uint8_t> data,
        const ShaderResourceContract& required,
        const std::string& sourceName, bool discovery = true
    );

    /// Validate SPIR-V structure, a compute `main`, and the descriptor bindings
    /// MAKO will actually bind. Additional declarations are accepted and remain
    /// subject to Vulkan pipeline validation, avoiding brittle allowlisting.
    SpirvShaderInfo validateSpirvComputeShader(
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

}
