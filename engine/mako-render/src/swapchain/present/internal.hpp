/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "present_diagnostics.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>

#include <vulkan/vulkan_core.h>

namespace mako::layer::present_detail {
    using DiagnosticsClock = present_diagnostics::Clock;
    using DiagnosticsContextScope = present_diagnostics::ContextScope;
    using present_diagnostics::logHistoryWarmup;
    using present_diagnostics::logPresentFallback;

    inline bool presentDiagnosticsEnabled() {
        return present_diagnostics::enabled();
    }

    inline DiagnosticsClock::time_point startPresentDiagnostic() {
        return present_diagnostics::start();
    }

    inline DiagnosticsClock::duration finishPresentDiagnostic(
            const DiagnosticsClock::time_point started) {
        if (!presentDiagnosticsEnabled())
            return {};
        return DiagnosticsClock::now() - started;
    }

    struct PresentPhaseDurations {
        DiagnosticsClock::duration renderFence{};
        DiagnosticsClock::duration schedule{};
        DiagnosticsClock::duration sourceCopy{};
        DiagnosticsClock::duration acquire{};
        DiagnosticsClock::duration generatedSubmit{};
        DiagnosticsClock::duration generatedPresent{};
        DiagnosticsClock::duration originalPresent{};
    };

    inline void logSlowPresentBreakdown(const uint64_t contextId,
            const size_t frameIndex, const size_t sequenceIndex,
            const DiagnosticsClock::duration totalDuration,
            const PresentPhaseDurations& phases) {
        if (!presentDiagnosticsEnabled())
            return;

        const auto milliseconds = [](const auto duration) {
            return std::chrono::duration<double, std::milli>(
                duration
            ).count();
        };
        const double totalMs = milliseconds(totalDuration);

        const auto attributedDuration =
            phases.renderFence + phases.schedule + phases.sourceCopy +
            phases.acquire + phases.generatedSubmit +
            phases.generatedPresent + phases.originalPresent;
        const double unattributedMs = std::max(
            0.0, totalMs - milliseconds(attributedDuration)
        );
        present_diagnostics::recordPresentPhaseTiming(contextId, frameIndex,
            sequenceIndex, DiagnosticsClock::now(), {
                totalMs, milliseconds(phases.renderFence), milliseconds(phases.schedule),
                milliseconds(phases.sourceCopy), milliseconds(phases.acquire),
                milliseconds(phases.generatedSubmit), milliseconds(phases.generatedPresent),
                milliseconds(phases.originalPresent), unattributedMs});
        if (totalMs < present_diagnostics::thresholdMilliseconds())
            return;
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=present-breakdown"
                  << " context=" << contextId
                  << " total_ms=" << totalMs
                  << " render_fence_ms="
                  << milliseconds(phases.renderFence)
                  << " schedule_ms=" << milliseconds(phases.schedule)
                  << " source_copy_ms=" << milliseconds(phases.sourceCopy)
                  << " acquire_ms=" << milliseconds(phases.acquire)
                  << " generated_submit_ms="
                  << milliseconds(phases.generatedSubmit)
                  << " generated_present_ms="
                  << milliseconds(phases.generatedPresent)
                  << " original_present_ms="
                  << milliseconds(phases.originalPresent)
                  << " unattributed_ms=" << unattributedMs
                  << " frame=" << frameIndex
                  << " sequence=" << sequenceIndex
                  << '\n';
    }

    inline void logSlowPresentOperation(const std::string_view operation,
            const size_t frameIndex, const size_t sequenceIndex,
            const DiagnosticsClock::time_point started,
            const std::optional<VkResult> result = std::nullopt,
            const std::optional<size_t> passIndex = std::nullopt,
            const std::optional<uint32_t> imageIndex = std::nullopt) {
        present_diagnostics::logSlowOperation(
            operation, frameIndex, sequenceIndex, started,
            result, passIndex, imageIndex
        );
    }

    inline VkImageMemoryBarrier barrierHelper(const VkImage handle,
            const VkAccessFlags srcAccessMask,
            const VkAccessFlags dstAccessMask,
            const VkImageLayout oldLayout,
            const VkImageLayout newLayout) {
        return VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = srcAccessMask,
            .dstAccessMask = dstAccessMask,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = handle,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
    }
}
