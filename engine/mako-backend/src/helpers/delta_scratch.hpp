/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "image_prefix.hpp"

namespace mako::backend {
    template<typename T>
    struct DeltaScratch {
        std::span<const T> primary;
        std::span<const T> spare;
    };

    // Gamma0 is dead after Delta1's first dispatch. Gamma1's first m images
    // remain live until Delta1's second half, so only its tail can be reused.
    template<typename T>
    DeltaScratch<T> deltaScratch(const std::vector<T>& gamma0,
            const std::vector<T>& gamma1, const size_t multiplier) {
        if ((multiplier != 1 && multiplier != 2) || &gamma0 == &gamma1)
            throw std::invalid_argument("invalid Delta1 scratch inputs");
        const size_t primaryCount = multiplier == 1 ? 2 : 3;
        const size_t spareCount = 2 * multiplier - primaryCount;
        return {
            requiredPrefix(gamma0, primaryCount, "Delta1 Gamma0 scratch"),
            requiredPrefix(gamma1, multiplier + spareCount,
                "Delta1 Gamma1 scratch").subspan(multiplier),
        };
    }
}
