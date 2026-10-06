/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "model_resources.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace mako::backend {

    using TranslatedLsfgShaders = std::map<uint32_t, std::vector<uint8_t>>;

    struct LsfgShaderSet {
        ModelResourceSelection selection;
        bool fp16{false};
        std::shared_ptr<const TranslatedLsfgShaders> translated;
        size_t convertedFp16Stages{0};

        [[nodiscard]] const std::vector<uint8_t>& resource(
            const DllResourceArchive& archive, uint32_t logicalId,
            bool performance) const;
    };

    /// Require the selected precision. Experimental builds may convert a
    /// supported FP32 graph when native FP16 is absent. Both runtime and
    /// preflight use this setup-only owner; resources never leave memory.
    [[nodiscard]] LsfgShaderSet loadLsfgShaderSet(
        const DllResourceArchive& archive, const std::filesystem::path& dll,
        bool fp16, std::optional<bool> performance = std::nullopt);

}
