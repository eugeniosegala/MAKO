/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace vk::detail {
    template<typename T, size_t inlineCapacity>
    class InlineSubmitArray {
    public:
        explicit InlineSubmitArray(const size_t size,
                const T& value = T{}) : valueCount(size) {
            if (size > inlineCapacity) {
                this->overflowValues.assign(size, value);
            } else {
                std::fill_n(this->inlineValues.begin(), size, value);
            }
        }

        [[nodiscard]] std::span<T> values() {
            return {this->data(), this->valueCount};
        }
        [[nodiscard]] std::span<const T> values() const {
            return {this->data(), this->valueCount};
        }
        [[nodiscard]] T& back() { return this->data()[this->valueCount - 1]; }
    private:
        [[nodiscard]] T* data() {
            return this->valueCount > inlineCapacity
                ? this->overflowValues.data()
                : this->inlineValues.data();
        }
        [[nodiscard]] const T* data() const {
            return this->valueCount > inlineCapacity
                ? this->overflowValues.data()
                : this->inlineValues.data();
        }

        std::array<T, inlineCapacity> inlineValues{};
        std::vector<T> overflowValues;
        size_t valueCount;
    };

    /// Own the arrays referenced by one VkSubmitInfo. The normal Renderer
    /// shape stays inline; unusually large application wait lists retain an
    /// unbounded vector fallback.
    class CommandBufferSubmitStorage {
    public:
        CommandBufferSubmitStorage(
                const std::span<const VkSemaphore> waitSemaphores,
                const VkSemaphore waitTimelineSemaphore,
                const uint64_t waitValue,
                const std::span<const VkSemaphore> signalSemaphores,
                const VkSemaphore signalTimelineSemaphore,
                const uint64_t signalValue) :
            combinedWaitSemaphores(combinedSize(
                waitSemaphores.size(), waitTimelineSemaphore
            )),
            combinedSignalSemaphores(combinedSize(
                signalSemaphores.size(), signalTimelineSemaphore
            )),
            waitTimelineValues(this->combinedWaitSemaphores.values().size()),
            signalTimelineValues(
                this->combinedSignalSemaphores.values().size()
            ),
            waitStages(this->combinedWaitSemaphores.values().size(),
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT) {
            std::copy(
                waitSemaphores.begin(), waitSemaphores.end(),
                this->combinedWaitSemaphores.values().begin()
            );
            if (waitTimelineSemaphore) {
                this->combinedWaitSemaphores.back() = waitTimelineSemaphore;
                this->waitTimelineValues.back() = waitValue;
            }

            std::copy(
                signalSemaphores.begin(), signalSemaphores.end(),
                this->combinedSignalSemaphores.values().begin()
            );
            if (signalTimelineSemaphore) {
                this->combinedSignalSemaphores.back() =
                    signalTimelineSemaphore;
                this->signalTimelineValues.back() = signalValue;
            }
        }

        [[nodiscard]] auto waits() const {
            return this->combinedWaitSemaphores.values();
        }
        [[nodiscard]] auto signals() const {
            return this->combinedSignalSemaphores.values();
        }
        [[nodiscard]] auto waitValues() const {
            return this->waitTimelineValues.values();
        }
        [[nodiscard]] auto signalValues() const {
            return this->signalTimelineValues.values();
        }
        [[nodiscard]] auto stages() const {
            return this->waitStages.values();
        }
    private:
        static constexpr size_t inlineSemaphoreCount = 4;

        static size_t combinedSize(const size_t binaryCount,
                const VkSemaphore timelineSemaphore) {
            return binaryCount + static_cast<size_t>(
                timelineSemaphore != VK_NULL_HANDLE
            );
        }

        InlineSubmitArray<VkSemaphore, inlineSemaphoreCount>
            combinedWaitSemaphores;
        InlineSubmitArray<VkSemaphore, inlineSemaphoreCount>
            combinedSignalSemaphores;
        InlineSubmitArray<uint64_t, inlineSemaphoreCount> waitTimelineValues;
        InlineSubmitArray<uint64_t, inlineSemaphoreCount> signalTimelineValues;
        InlineSubmitArray<VkPipelineStageFlags, inlineSemaphoreCount>
            waitStages;
    };
}
