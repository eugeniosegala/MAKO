/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "limits.hpp"

#include "mako-common/vulkan/descriptor_pool.hpp"

#include <cstddef>
#include <cstdint>

using namespace mako;
using namespace mako::backend;

namespace {
    const vk::Limits BASE_LIMITS{
        .sets = 51,
        .uniform_buffers = 3,
        .samplers = 51,
        .sampled_images = 165,
        .storage_images = 172
    };
    const vk::Limits BASE_LIMITS_PERF{
        .sampled_images = 91,
        .storage_images = 102
    };
    const vk::Limits GEN_LIMITS{
        .sets = 93,
        .uniform_buffers = 54,
        .samplers = 147,
        .sampled_images = 567,
        .storage_images = 261
    };
    const vk::Limits GEN_LIMITS_PERF{
        .sampled_images = 339,
        .storage_images = 183
    };
}

vk::Limits backend::calculateDescriptorPoolLimits(
        size_t count, bool perf, bool colorConversion) {
    const auto m = static_cast<uint16_t>(count);

    vk::Limits a{BASE_LIMITS};
    vk::Limits b{GEN_LIMITS};
    if (perf) {
        a.sampled_images = BASE_LIMITS_PERF.sampled_images;
        b.sampled_images = GEN_LIMITS_PERF.sampled_images;
        a.storage_images = BASE_LIMITS_PERF.storage_images;
        b.storage_images = GEN_LIMITS_PERF.storage_images;
    }

    a.sets += b.sets * m;
    a.uniform_buffers += b.uniform_buffers * m;
    a.samplers += b.samplers * m;
    a.sampled_images += b.sampled_images * m;
    a.storage_images += b.storage_images * m;

    if (colorConversion) {
        // Two source decoders plus one encoder per generated-frame image.
        const auto conversionSets = static_cast<uint16_t>(2 + count);
        a.sets += conversionSets;
        a.samplers += conversionSets;
        a.sampled_images += conversionSets;
        a.storage_images += conversionSets;
    }
    return a;
}
