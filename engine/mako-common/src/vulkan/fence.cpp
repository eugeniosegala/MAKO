/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/vulkan/fence.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <cstdint>

#include <vulkan/vulkan_core.h>

using namespace vk;

namespace {
    /// create a fence
    ls::owned_ptr<VkFence> createFence(const vk::Vulkan& vk) {
        VkFence handle{};

        const VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
        };
        auto res = vk.df().CreateFence(vk.dev(), &fenceInfo, VK_NULL_HANDLE, &handle);
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkCreateFence() failed");

        return ls::owned_ptr<VkFence>(
            new VkFence(handle),
            [dev = vk.dev(), defunc = vk.df().DestroyFence](VkFence& fence) {
                defunc(dev, fence, VK_NULL_HANDLE);
            }
        );
    }
}

Fence::Fence(const vk::Vulkan& vk)
    : fence(createFence(vk)) {}

void Fence::reset(const vk::Vulkan& vk) const {
    auto res = vk.df().ResetFences(vk.dev(), 1, &*this->fence);
    if (res != VK_SUCCESS)
        throw ls::vulkan_error(res, "vkResetFences() failed");
}

bool Fence::wait(const vk::Vulkan& vk, uint64_t timeout) const {
    auto res = vk.df().WaitForFences(vk.dev(), 1, &*this->fence, VK_TRUE, timeout);
    if (res != VK_SUCCESS && res != VK_TIMEOUT)
        throw ls::vulkan_error(res, "vkWaitForFences() failed");

    return res == VK_SUCCESS;
}
