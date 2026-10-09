/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/vulkan/descriptor_pool.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <array>
#include <algorithm>
#include <cstdint>

#include <vulkan/vulkan_core.h>

using namespace vk;

namespace {
    /// create a descriptor pool
    ls::owned_ptr<VkDescriptorPool> createDescriptorPool(const vk::Vulkan& vk,
            const Limits& limits) {
        VkDescriptorPool handle{};

        std::array<VkDescriptorPoolSize, 4> poolCounts{{
            {
                .type = VK_DESCRIPTOR_TYPE_SAMPLER,
                .descriptorCount = limits.samplers
            },
            {
                .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .descriptorCount = limits.sampled_images
            },
            {
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = limits.storage_images
            },
            {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = limits.uniform_buffers
            }
        }};
        // Vulkan rejects zero-sized entries. Conversion-only pipelines do not
        // need uniform buffers; keep absent descriptor types out of the pool.
        const auto end = std::remove_if(poolCounts.begin(), poolCounts.end(),
            [](const auto& entry) { return entry.descriptorCount == 0; });
        const VkDescriptorPoolCreateInfo descpoolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = limits.sets,
            .poolSizeCount = static_cast<uint32_t>(end - poolCounts.begin()),
            .pPoolSizes = poolCounts.data()
        };
        auto res = vk.df().CreateDescriptorPool(vk.dev(), &descpoolInfo, VK_NULL_HANDLE, &handle);
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkCreateDescriptorPool() failed");

        return ls::owned_ptr<VkDescriptorPool>(
            new VkDescriptorPool(handle),
            [dev = vk.dev(), defunc = vk.df().DestroyDescriptorPool](VkDescriptorPool& pool) {
                defunc(dev, pool, VK_NULL_HANDLE);
            }
        );
    }
}

DescriptorPool::DescriptorPool(const vk::Vulkan& vk, const Limits& limits)
    : descriptor_pool(createDescriptorPool(vk, limits)) {}
