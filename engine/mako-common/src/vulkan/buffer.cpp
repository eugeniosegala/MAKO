/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/vulkan/buffer.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <vulkan/vulkan_core.h>

using namespace vk;

namespace {
    /// create a buffer
    ls::owned_ptr<VkBuffer> createBuffer(const vk::Vulkan& vk, size_t size,
            VkBufferUsageFlags usage) {
        VkBuffer handle{};

        const VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        auto res = vk.df().CreateBuffer(vk.dev(), &bufferInfo, VK_NULL_HANDLE, &handle);
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkCreateBuffer() failed");

        return ls::owned_ptr<VkBuffer>(
            new VkBuffer(handle),
            [dev = vk.dev(), defunc = vk.df().DestroyBuffer](VkBuffer& buffer) {
                defunc(dev, buffer, VK_NULL_HANDLE);
            }
        );
    }
    /// allocate memory for a buffer
    ls::owned_ptr<VkDeviceMemory> allocateMemory(const vk::Vulkan& vk, VkBuffer buffer) {
        VkDeviceMemory handle{};

        VkMemoryRequirements reqs{};
        vk.df().GetBufferMemoryRequirements(vk.dev(), buffer, &reqs);

        auto mti = vk.findMemoryTypeIndex(
            reqs.memoryTypeBits,
            true
        );
        if (!mti.has_value())
            throw ls::vulkan_error("no suitable memory type found for buffer");

        const VkMemoryAllocateInfo memoryInfo{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = reqs.size,
            .memoryTypeIndex = *mti
        };
        auto res = vk.df().AllocateMemory(vk.dev(), &memoryInfo, VK_NULL_HANDLE, &handle);
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkAllocateMemory() failed");
        if (handle == VK_NULL_HANDLE)
            throw ls::vulkan_error(VK_ERROR_OUT_OF_DEVICE_MEMORY,
                "vkAllocateMemory() succeeded but returned a null handle");

        const auto memoryAccounting = vk.deviceMemoryAccounting();
        memoryAccounting->recordAllocation(DeviceMemoryKind::Internal, reqs.size);
        auto memory = ls::owned_ptr<VkDeviceMemory>(
            new VkDeviceMemory(handle),
            [dev = vk.dev(), defunc = vk.df().FreeMemory,
             memoryAccounting,
             allocationSize = reqs.size](VkDeviceMemory& value) {
                defunc(dev, value, VK_NULL_HANDLE);
                memoryAccounting->recordFree(
                    DeviceMemoryKind::Internal, allocationSize);
            }
        );

        res = vk.df().BindBufferMemory(vk.dev(), buffer, handle, 0);
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkBindBufferMemory() failed");

        return memory;
    }
    /// copy data to a buffer
    void copyDataToBuffer(const vk::Vulkan& vk,
            VkDeviceMemory memory, const void* data, size_t size) {
        void* buf{};

        auto res = vk.df().MapMemory(vk.dev(), memory, 0, size, 0, &buf);
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkMapMemory() failed");

        std::copy_n(
            reinterpret_cast<const uint8_t*>(data),
            size,
            reinterpret_cast<uint8_t*>(buf)
        );

        vk.df().UnmapMemory(vk.dev(), memory);
    }
}

Buffer::Buffer(const vk::Vulkan& vk, const void* data, size_t size, VkBufferUsageFlags usage) :
        buffer(createBuffer(vk, size, usage)),
        size(size) {
    this->memory = allocateMemory(vk, *this->buffer);
    copyDataToBuffer(vk, *this->memory, data, size);
}

void Buffer::write(const vk::Vulkan& vk, const void* data, size_t length) {
    if (length > this->size)
        throw ls::vulkan_error("buffer write exceeds allocation size");
    copyDataToBuffer(vk, *this->memory, data, length);
}

std::vector<uint8_t> Buffer::read(const vk::Vulkan& vk) const {
    std::vector<uint8_t> result(this->size);
    void* mapped{};
    const auto res = vk.df().MapMemory(
        vk.dev(), *this->memory, 0, this->size, 0, &mapped
    );
    if (res != VK_SUCCESS)
        throw ls::vulkan_error(res, "vkMapMemory() failed");

    std::copy_n(
        reinterpret_cast<const uint8_t*>(mapped),
        this->size,
        result.data()
    );
    vk.df().UnmapMemory(vk.dev(), *this->memory);
    return result;
}
