/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/vulkan/image_memory_pool.hpp"

#include "mako-common/helpers/errors.hpp"
#include "mako-common/vulkan/device_memory_accounting.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

using namespace vk;

namespace {
    constexpr VkDeviceSize maximumGrowthBlockSize = 32ULL * 1024ULL * 1024ULL;

    [[nodiscard]] std::optional<VkDeviceSize> alignedOffset(
            const VkDeviceSize value, const VkDeviceSize alignment) noexcept {
        if (alignment == 0 || (alignment & (alignment - 1)) != 0)
            return std::nullopt;
        const auto mask = alignment - 1;
        if (value > std::numeric_limits<VkDeviceSize>::max() - mask)
            return std::nullopt;
        return (value + mask) & ~mask;
    }

    [[nodiscard]] VkDeviceSize blockSizeFor(
            const VkDeviceSize minimumBlockSize,
            const VkMemoryRequirements& requirements) {
        VkDeviceSize allocationSize = minimumBlockSize;
        if (requirements.size > maximumGrowthBlockSize) {
            allocationSize = requirements.size;
        } else if (requirements.size > maximumGrowthBlockSize / 4) {
            allocationSize = maximumGrowthBlockSize;
        } else {
            allocationSize = std::max(allocationSize, requirements.size * 4);
        }
        const auto aligned = alignedOffset(allocationSize, requirements.alignment);
        if (!aligned)
            throw ls::vulkan_error("internal image pool block size overflow");
        return *aligned;
    }
}

detail::ImageMemoryBlockCursor::ImageMemoryBlockCursor(
        const VkDeviceSize capacity) noexcept
    : blockCapacity(capacity) {}

std::optional<VkDeviceSize> detail::ImageMemoryBlockCursor::allocate(
        const VkDeviceSize size, const VkDeviceSize alignment) noexcept {
    const auto offset = alignedOffset(this->nextOffset, alignment);
    if (!offset || *offset > this->blockCapacity ||
            size > this->blockCapacity - *offset)
        return std::nullopt;
    this->nextOffset = *offset + size;
    return offset;
}

class ImageMemoryPool::Impl {
public:
    explicit Impl(const Vulkan& vk, const VkDeviceSize minimumBlockSize)
        : vk(vk), minimumBlockSize(minimumBlockSize) {
        if (minimumBlockSize == 0)
            throw ls::vulkan_error("internal image pool block size must be positive");
    }

    struct Block {
        Block(const Vulkan& vk, const uint32_t memoryTypeIndex,
                const VkDeviceSize size)
            : vk(vk), memoryTypeIndex(memoryTypeIndex), cursor(size), size(size) {
            const VkMemoryAllocateInfo allocationInfo{
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize = size,
                .memoryTypeIndex = memoryTypeIndex,
            };
            const auto result = vk.df().AllocateMemory(
                vk.dev(), &allocationInfo, VK_NULL_HANDLE, &this->memory
            );
            if (result != VK_SUCCESS)
                throw ls::vulkan_error(result, "internal image pool allocation failed");
            if (this->memory == VK_NULL_HANDLE)
                throw ls::vulkan_error(
                    VK_ERROR_OUT_OF_DEVICE_MEMORY,
                    "internal image pool allocation returned a null handle"
                );
            this->accounting = vk.deviceMemoryAccounting();
            this->accounting->recordAllocation(DeviceMemoryKind::Internal, size);
        }

        ~Block() {
            if (this->memory != VK_NULL_HANDLE) {
                this->vk.df().FreeMemory(
                    this->vk.dev(), this->memory, VK_NULL_HANDLE
                );
                this->accounting->recordFree(DeviceMemoryKind::Internal, this->size);
            }
        }

        const Vulkan& vk;
        uint32_t memoryTypeIndex{};
        detail::ImageMemoryBlockCursor cursor;
        VkDeviceSize size{};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        std::shared_ptr<DeviceMemoryAccounting> accounting;
    };

    std::shared_ptr<void> bind(const VkImage image) {
        VkMemoryRequirements requirements{};
        this->vk.df().GetImageMemoryRequirements(
            this->vk.dev(), image, &requirements
        );
        const auto memoryTypeIndex = this->vk.findMemoryTypeIndex(
            requirements.memoryTypeBits, false
        );
        if (!memoryTypeIndex)
            throw ls::vulkan_error("no suitable memory type found for pooled image");

        std::shared_ptr<Block> selected;
        VkDeviceSize offset{};
        for (const auto& block : this->blocks) {
            if (block->memoryTypeIndex != *memoryTypeIndex)
                continue;
            const auto candidate = block->cursor.allocate(
                requirements.size, requirements.alignment
            );
            if (candidate) {
                selected = block;
                offset = *candidate;
                break;
            }
        }
        if (!selected) {
            selected = std::make_shared<Block>(
                this->vk,
                *memoryTypeIndex,
                blockSizeFor(this->minimumBlockSize, requirements)
            );
            const auto candidate = selected->cursor.allocate(
                requirements.size, requirements.alignment
            );
            if (!candidate)
                throw ls::vulkan_error("new internal image pool block is too small");
            offset = *candidate;
            this->blocks.push_back(selected);
        }

        const auto result = this->vk.df().BindImageMemory(
            this->vk.dev(), image, selected->memory, offset
        );
        if (result != VK_SUCCESS)
            throw ls::vulkan_error(result, "pooled vkBindImageMemory() failed");
        auto* const owner = selected.get();
        return std::shared_ptr<void>{std::move(selected), owner};
    }

private:
    const Vulkan& vk;
    VkDeviceSize minimumBlockSize{};
    std::vector<std::shared_ptr<Block>> blocks;
};

ImageMemoryPool::ImageMemoryPool(
        const Vulkan& vk, const VkDeviceSize minimumBlockSize)
    : impl(std::make_unique<Impl>(vk, minimumBlockSize)) {}

ImageMemoryPool::~ImageMemoryPool() = default;

std::shared_ptr<void> ImageMemoryPool::bind(const VkImage image) {
    return this->impl->bind(image);
}
