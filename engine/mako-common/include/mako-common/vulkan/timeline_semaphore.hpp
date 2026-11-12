/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "../helpers/pointers.hpp"
#include "vulkan.hpp"

#include <functional>
#include <cstdint>
#include <optional>

#include <vulkan/vulkan_core.h>

namespace vk {
    /// vulkan timeline semaphore
    class TimelineSemaphore {
    public:
        /// create a timeline semaphore
        /// @param vk the vulkan instance
        /// @param initial initial value of the timeline semaphore
        /// Import consumes the descriptor on success and failure.
        /// @param importFd optional file descriptor to import from
        /// @param exportFd optional file descriptor to export to
        /// @throws ls::vulkan_error on failure
        TimelineSemaphore(const vk::Vulkan& vk, uint32_t initial,
            std::optional<int> importFd = std::nullopt,
            std::optional<int*> exportFd = std::nullopt);

        /// signal the timeline semaphore
        /// @param vk the vulkan instance
        /// @param value the value to signal to
        /// @throws ls::vulkan_error on failure
        void signal(const vk::Vulkan& vk, uint64_t value) const;

        /// Build a callable that releases every wait on this semaphore, from
        /// any thread, for work whose real signal will never arrive. The
        /// semaphore must outlive the callable, and it must run at most once.
        /// @param vk the vulkan instance
        /// @returns the callable
        [[nodiscard]] std::function<void()> releaser(const vk::Vulkan& vk) const;

        /// wait for the timeline semaphore
        /// @param vk the vulkan instance
        /// @param value the value to wait for
        /// @param timeout the timeout in nanoseconds, or UINT64_MAX for no timeout
        /// @returns true if the semaphore reached the value, false if it timed out
        /// @throws ls::vulkan_error on failure
        [[nodiscard]] bool wait(const vk::Vulkan& vk,
            uint64_t value, uint64_t timeout = UINT64_MAX) const;

        /// get the underlying VkSemaphore handle
        /// @return the VkSemaphore handle
        [[nodiscard]] const auto& handle() const { return *this->semaphore; }
    private:
        ls::owned_ptr<VkSemaphore> semaphore;
    };
}
