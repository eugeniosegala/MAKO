/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/vulkan/device_memory_accounting.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

using namespace vk;

namespace {
    void raisePeak(std::atomic<uint64_t>& peak, const uint64_t current) noexcept {
        auto observed = peak.load(std::memory_order_relaxed);
        while (observed < current && !peak.compare_exchange_weak(
                observed, current,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
        }
    }
}

void DeviceMemoryAccounting::recordAllocation(
        const DeviceMemoryKind kind, const uint64_t bytes) noexcept {
    auto& bucket = this->buckets[static_cast<size_t>(kind)];
    const auto currentBytes = bucket.bytes.fetch_add(
        bytes, std::memory_order_relaxed) + bytes;
    const auto currentAllocations = bucket.allocations.fetch_add(
        1, std::memory_order_relaxed) + 1;
    raisePeak(bucket.peakBytes, currentBytes);
    raisePeak(bucket.peakAllocations, currentAllocations);
}

void DeviceMemoryAccounting::recordFree(
        const DeviceMemoryKind kind, const uint64_t bytes) noexcept {
    auto& bucket = this->buckets[static_cast<size_t>(kind)];
    static_cast<void>(bucket.bytes.fetch_sub(bytes, std::memory_order_relaxed));
    static_cast<void>(bucket.allocations.fetch_sub(1, std::memory_order_relaxed));
}

DeviceMemorySnapshot DeviceMemoryAccounting::snapshot() const noexcept {
    const auto& internal = this->buckets[
        static_cast<size_t>(DeviceMemoryKind::Internal)];
    const auto& imported = this->buckets[
        static_cast<size_t>(DeviceMemoryKind::Imported)];
    const auto& exported = this->buckets[
        static_cast<size_t>(DeviceMemoryKind::Exported)];
    const auto currentTotals = [](const Bucket& bucket) {
        return DeviceMemoryTotals{
            .bytes = bucket.bytes.load(std::memory_order_relaxed),
            .allocations = bucket.allocations.load(std::memory_order_relaxed),
        };
    };
    const auto peakTotals = [](const Bucket& bucket) {
        return DeviceMemoryTotals{
            .bytes = bucket.peakBytes.load(std::memory_order_relaxed),
            .allocations = bucket.peakAllocations.load(std::memory_order_relaxed),
        };
    };
    return {
        .internal = currentTotals(internal),
        .imported = currentTotals(imported),
        .exported = currentTotals(exported),
        .peakInternal = peakTotals(internal),
        .peakImported = peakTotals(imported),
        .peakExported = peakTotals(exported),
    };
}
