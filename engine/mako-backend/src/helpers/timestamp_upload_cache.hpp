/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace mako::backend {
    /// Upload a per-output timestamp only when its value changed. The cache is
    /// committed after the writer succeeds so a failed upload remains
    /// retryable.
    template<typename Writer>
    bool uploadChangedTimestamp(std::vector<float>& cachedTimestamps,
            const size_t index, const float timestamp, Writer&& writer) {
        if (cachedTimestamps.at(index) == timestamp)
            return false;

        std::invoke(std::forward<Writer>(writer), timestamp);
        cachedTimestamps.at(index) = timestamp;
        return true;
    }
}
