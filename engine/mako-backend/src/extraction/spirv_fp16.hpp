/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "model_resource_validation.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mako::backend::detail {

    [[nodiscard]] bool experimentalLsfgFp16Available();

    struct ConvertedFp16Shader {
        std::vector<uint8_t> bytes;
        bool hasHalfArithmetic{false};
    };

    /// Experimental setup-only conversion. Descriptor interfaces and image
    /// formats stay intact; float operations eligible for relaxation become
    /// explicit half arithmetic. Neither inputs nor outputs leave memory.
    [[nodiscard]] ConvertedFp16Shader convertLsfgShaderToFp16(
        std::span<const uint8_t> source, const ShaderResourceContract& contract,
        const std::string& sourceName);

}
