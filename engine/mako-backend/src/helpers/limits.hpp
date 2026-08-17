/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/vulkan/descriptor_pool.hpp"

#include <cstddef>

namespace mako::backend {
    /// calculate limits for descriptor pools
    /// @param count number of images
    /// @param perf whether performance mode is enabled
    /// @return calculated limits
    vk::Limits calculateDescriptorPoolLimits(
        size_t count, bool perf, bool colorConversion = false
    );
}
