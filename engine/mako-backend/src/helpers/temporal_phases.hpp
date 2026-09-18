/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <array>
#include <cstddef>
#include <numeric>

namespace mako::backend {
    // These counts construct Alpha1's histories and determine command reuse.
    inline constexpr std::array<size_t, 7> alphaHistoryCounts{3, 2, 2, 2, 2, 2, 2};
    inline constexpr size_t commandPhaseCount = [] {
        size_t period = 2; // Imported source images alternate between two slots.
        for (const size_t count : alphaHistoryCounts)
            period = std::lcm(period, count);
        return period;
    }();
}
