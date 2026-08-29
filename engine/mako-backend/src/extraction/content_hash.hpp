/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace mako::backend::detail {

    /// Return a stable lowercase SHA-256 digest without retaining the input.
    [[nodiscard]] std::string sha256Hex(std::span<const uint8_t> data);

}
