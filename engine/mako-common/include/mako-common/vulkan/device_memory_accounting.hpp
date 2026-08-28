/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace vk {

    enum class DeviceMemoryKind : size_t {
        Internal,
        Imported,
        Exported,
        Count
    };

    struct DeviceMemoryTotals {
        uint64_t bytes{};
        uint64_t allocations{};
    };

    struct DeviceMemorySnapshot {
        DeviceMemoryTotals internal;
        DeviceMemoryTotals imported;
        DeviceMemoryTotals exported;
        DeviceMemoryTotals peakInternal;
        DeviceMemoryTotals peakImported;
        DeviceMemoryTotals peakExported;
    };

    /// Process-local accounting for VkDeviceMemory handles created through
    /// MAKO's Vulkan wrappers. Imported mappings are intentionally tracked
    /// separately because their allocationSize is not additional physical
    /// memory owned by MAKO.
    class DeviceMemoryAccounting {
    public:
        void recordAllocation(DeviceMemoryKind kind, uint64_t bytes) noexcept;
        void recordFree(DeviceMemoryKind kind, uint64_t bytes) noexcept;
        [[nodiscard]] DeviceMemorySnapshot snapshot() const noexcept;

    private:
        struct Bucket {
            std::atomic<uint64_t> bytes{};
            std::atomic<uint64_t> allocations{};
            std::atomic<uint64_t> peakBytes{};
            std::atomic<uint64_t> peakAllocations{};
        };

        static constexpr size_t bucketCount =
            static_cast<size_t>(DeviceMemoryKind::Count);
        std::array<Bucket, bucketCount> buckets;
    };

}
