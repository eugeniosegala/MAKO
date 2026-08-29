/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace mako::backend {

    struct ModelCompatibility {
        bool compatible{false};
        std::string reason;
    };

    /// Static, GPU-independent inspection of the model resources MAKO knows
    /// how to consume. Fingerprints identify content but never decide support.
    struct LosslessDllInspection {
        std::string fileSha256;
        std::string resourceLayoutSha256;
        uint64_t fileSize{0};
        size_t resourceCount{0};
        ModelCompatibility lsfgFp32Quality;
        ModelCompatibility lsfgFp32Performance;
        ModelCompatibility lsfgFp16Quality;
        ModelCompatibility lsfgFp16Performance;
        ModelCompatibility ls1Quality;
        ModelCompatibility ls1Performance;
    };

    [[nodiscard]] LosslessDllInspection inspectLosslessDll(
        const std::filesystem::path& dll
    );

}
