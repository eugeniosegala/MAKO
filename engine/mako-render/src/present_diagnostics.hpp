/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "adaptive_scheduler.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include <vulkan/vulkan_core.h>

namespace mako::layer::present_diagnostics {

    using Clock = std::chrono::steady_clock;

    [[nodiscard]] uint64_t allocateContextId();
    [[nodiscard]] bool enabled();
    [[nodiscard]] double thresholdMilliseconds();
    [[nodiscard]] Clock::time_point start();

    class ContextScope {
    public:
        explicit ContextScope(uint64_t contextId);
        ~ContextScope();

        ContextScope(const ContextScope&) = delete;
        ContextScope& operator=(const ContextScope&) = delete;
    private:
        uint64_t previousContextId;
    };

    void logSlowOperation(std::string_view operation,
        size_t frameIndex, size_t sequenceIndex,
        Clock::time_point started,
        std::optional<VkResult> result = std::nullopt,
        std::optional<size_t> passIndex = std::nullopt,
        std::optional<uint32_t> imageIndex = std::nullopt);

    void logPresentFallback(size_t frameIndex, size_t sequenceIndex,
        size_t passIndex, size_t skippedFrames, uint64_t timelineValue,
        std::string_view acquireMode, std::string_view backendWork);

    void logHistoryWarmup(size_t frameIndex, size_t sequenceIndex,
        size_t remainingFrames, bool recovery,
        std::optional<uint32_t> acquiredImage);

    [[nodiscard]] AdaptiveSchedulerDiagnostics& adaptiveScheduler();

}
