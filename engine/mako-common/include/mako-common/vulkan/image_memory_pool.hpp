/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <vulkan/vulkan_core.h>

namespace vk {
    class Vulkan;

    namespace detail {
        /// Allocation-free linear cursor used by one non-aliasing memory block.
        class ImageMemoryBlockCursor {
        public:
            explicit ImageMemoryBlockCursor(VkDeviceSize capacity) noexcept;

            [[nodiscard]] std::optional<VkDeviceSize> allocate(
                VkDeviceSize size, VkDeviceSize alignment
            ) noexcept;
            [[nodiscard]] VkDeviceSize used() const noexcept { return this->nextOffset; }
            [[nodiscard]] VkDeviceSize capacity() const noexcept { return this->blockCapacity; }

        private:
            VkDeviceSize blockCapacity{};
            VkDeviceSize nextOffset{};
        };
    }

    /// Context-local non-aliasing suballocator for internal optimal images.
    /// External images and host-visible buffers deliberately remain outside it.
    class ImageMemoryPool {
    public:
        explicit ImageMemoryPool(const Vulkan& vk,
            VkDeviceSize minimumBlockSize = 4ULL * 1024ULL * 1024ULL);
        ~ImageMemoryPool();

        ImageMemoryPool(const ImageMemoryPool&) = delete;
        ImageMemoryPool& operator=(const ImageMemoryPool&) = delete;
        ImageMemoryPool(ImageMemoryPool&&) = delete;
        ImageMemoryPool& operator=(ImageMemoryPool&&) = delete;

        /// Bind an image to a unique range and return an owner for that range.
        [[nodiscard]] std::shared_ptr<void> bind(VkImage image);

    private:
        class Impl;
        std::unique_ptr<Impl> impl;
    };
}
