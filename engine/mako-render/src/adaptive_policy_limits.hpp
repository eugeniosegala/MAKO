/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace mako::layer {

    /// Lowest real-frame cadence for Adaptive policy and its automatic cap.
    inline constexpr double adaptiveMinimumBaseFps = 10.0;

    /// Smooth Cadence may hand pacing to ordered FIFO only when the confirmed
    /// display rate represents the configured output target. The one-Hz floor
    /// tolerates integer refresh reporting around rates such as 59/60 Hz.
    [[nodiscard]] inline bool adaptiveTargetMatchesRefresh(
            const uint32_t targetFps,
            const std::optional<uint32_t> refreshHz) {
        if (!refreshHz || *refreshHz == 0)
            return false;
        const uint32_t difference = *refreshHz > targetFps
            ? *refreshHz - targetFps
            : targetFps - *refreshHz;
        const uint32_t tolerance = std::max<uint32_t>(
            1, (targetFps + 49) / 50
        );
        return difference <= tolerance;
    }

}
