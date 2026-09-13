/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "dll_reader.hpp"
#include "mako-backend/ls1.hpp"

#include <cstdint>
#include <optional>

namespace mako::backend {

    /// One coherent resource table in an immutable DLL archive. An offset
    /// preserves the supported table's stage/variant ordering; it never mixes
    /// individually plausible shaders from different tables or files.
    struct ModelResourceSelection {
        int64_t idOffset{0};

        [[nodiscard]] uint32_t resourceId(uint32_t canonicalId) const;
        [[nodiscard]] const std::vector<uint8_t>& resource(
            const DllResourceArchive& archive, uint32_t canonicalId) const;
    };

    /// Prefer the selected canonical model, then discover a unique relocated
    /// complete table. Cache successes and failures on the immutable archive.
    /// With no mode, resolve both modes together for the runtime registry so
    /// shared stages cannot come from a different table. Call only at setup.
    [[nodiscard]] ModelResourceSelection resolveLsfgModelResources(
        const DllResourceArchive& archive, bool fp16,
        std::optional<bool> performance = std::nullopt);

    /// A variant selects only that LS1 graph on the canonical path. With no
    /// variant, inspect all five graphs of the requested mode. Discovery needs
    /// the complete reflected LS1 table to identify stage and variant order.
    [[nodiscard]] ModelResourceSelection resolveLs1ModelResources(
        const DllResourceArchive& archive, Ls1Mode mode,
        std::optional<uint32_t> variant = std::nullopt);

}
