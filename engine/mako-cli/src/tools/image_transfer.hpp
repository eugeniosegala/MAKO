/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "mako-common/vulkan/image.hpp"
#include "mako-common/vulkan/vulkan.hpp"
#include <cstdint>
#include <span>
namespace mako::cli::images {
    [[nodiscard]] VkImageMemoryBarrier imageBarrier(VkImage image,
        VkAccessFlags sourceAccess, VkAccessFlags destinationAccess,
        VkImageLayout oldLayout, VkImageLayout newLayout);
    // The caller must complete earlier users of this image before replacing it.
    void uploadImage(const vk::Vulkan& vk, const vk::Image& image,
        std::span<const uint8_t> rgba);
}
