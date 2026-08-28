/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mako::backend {
    /// Return an exact, non-owning prefix or fail before descriptor creation.
    template<typename T>
    std::span<const T> requiredPrefix(const std::vector<T>& values,
            const size_t count, const std::string_view label) {
        if (values.size() < count)
            throw std::invalid_argument(
                std::string{label} + " image set is too small"
            );
        return std::span<const T>{values}.first(count);
    }
}
