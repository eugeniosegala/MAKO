/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mako::backend {

    /// Lossless Scaling LS1 network shape selected by the user.
    enum class Ls1Mode : uint8_t {
        Quality,
        Performance,
    };

    /// Vulkan-ready LS1 shader payloads translated from the licensed DLL at
    /// runtime. Quality uses all four stages; Performance omits stage2/stage3.
    struct [[gnu::visibility("default")]] Ls1ShaderSet {
        Ls1Mode mode{Ls1Mode::Quality};
        uint32_t modelVariant{0};
        std::vector<uint8_t> stage1;
        std::vector<uint8_t> stage2;
        std::vector<uint8_t> stage3;
        std::vector<uint8_t> reconstruction;
        std::string translator;
    };

    /// Extract one LS1 model from a user-supplied Lossless.dll and translate
    /// its Direct3D 11 compute bytecode to Vulkan SPIR-V. No licensed payload
    /// is persisted or packaged by MAKO.
    [[nodiscard]] Ls1ShaderSet loadLs1ShaderSet(
        const std::filesystem::path& shaderDllPath,
        Ls1Mode mode,
        float sharpness
    );

}
