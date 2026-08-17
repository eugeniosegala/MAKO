/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "swapchain.hpp"
#include "adaptive_scheduler.hpp"
#include "mako-backend/mako.hpp"
#include "mako-common/configuration/config.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/command_buffer.hpp"
#include "mako-common/vulkan/image.hpp"
#include "mako-common/vulkan/semaphore.hpp"
#include "mako-common/vulkan/vulkan.hpp"
#include "pnext_chain.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <unistd.h>

using namespace mako;
using namespace mako::layer;

namespace {
    using DiagnosticsClock = std::chrono::steady_clock;

    std::atomic<uint32_t> nextDiagnosticsContextSequence{1};
    thread_local uint64_t activeDiagnosticsContextId{0};

    uint64_t allocateDiagnosticsContextId() {
        const auto processId = static_cast<uint64_t>(
            static_cast<uint32_t>(::getpid())
        );
        const auto sequence = static_cast<uint64_t>(
            nextDiagnosticsContextSequence.fetch_add(1, std::memory_order_relaxed)
        );
        return (processId << 32) | sequence;
    }

    class DiagnosticsContextScope {
    public:
        explicit DiagnosticsContextScope(const uint64_t contextId) :
                previousContextId(activeDiagnosticsContextId) {
            activeDiagnosticsContextId = contextId;
        }

        ~DiagnosticsContextScope() {
            activeDiagnosticsContextId = this->previousContextId;
        }

        DiagnosticsContextScope(const DiagnosticsContextScope&) = delete;
        DiagnosticsContextScope& operator=(const DiagnosticsContextScope&) = delete;
    private:
        uint64_t previousContextId;
    };

    size_t generatedFrameCapacity(const ls::GameConf& profile) {
        return generatedFrameCapacityForProfile(profile);
    }

    std::vector<float> buildFixedFrameTimestamps(
            const size_t multiplier, const size_t generatedFrameCapacity) {
        const size_t count = fixedGeneratedFrameCount(
            multiplier, generatedFrameCapacity
        );
        std::vector<float> timestamps(count);
        for (size_t i = 0; i < count; ++i)
            timestamps[i] = fixedFrameTimestamp(i, multiplier);
        return timestamps;
    }

    SwapchainColorPipeline initialColorPipeline(
            const VkFormat format, const VkColorSpaceKHR colorSpace,
            const std::optional<bool> gamescopeHdrActive,
            const bool gamescopeManaged,
            const bool hdrExposureDisabled) {
        auto pipeline = classifySwapchainColor(
            format, colorSpace, gamescopeHdrActive.value_or(false)
        );
        if (hdrExposureDisabled && pipeline.hdr) {
            pipeline.generationSupported = false;
            pipeline.name = "hdr-exposure-disabled";
            pipeline.reason =
                "experimental HDR frame generation is disabled for this process";
            return pipeline;
        }
        if (gamescopeManaged && !gamescopeHdrActive &&
                pipeline.encoding == backend::FrameEncoding::SdrHighPrecision) {
            pipeline.generationSupported = false;
            pipeline.name = "gamescope-hdr-pending";
            pipeline.reason =
                "Gamescope HDR state is not confirmed; real-frame passthrough retained";
        }
        return pipeline;
    }

    void selectPackedHdr10Transport(const vk::Vulkan& vk,
            backend::Instance& backendInstance, SwapchainColorPipeline& pipeline,
            bool& applicationSupported, bool& backendSupported) {
        if (pipeline.encoding != backend::FrameEncoding::Hdr10Pq)
            return;

        applicationSupported =
            vk.supportsExternalImageFormat(
                VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT
            ) &&
            vk.supportsOptimalTilingFormatFeatures(
                VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                VK_FORMAT_FEATURE_BLIT_DST_BIT
            ) &&
            vk.supportsExternalImageFormat(
                VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT
            ) &&
            vk.supportsOptimalTilingFormatFeatures(
                VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                VK_FORMAT_FEATURE_BLIT_SRC_BIT
            );
        backendSupported = backendInstance.supportsPackedHdr10Transport();
        static_cast<void>(enablePackedHdr10Transport(
            pipeline, applicationSupported, backendSupported
        ));
    }

    bool presentDiagnosticsEnabled() {
        static const bool enabled = [] {
            const char* value = std::getenv("MAKO_PRESENT_DIAGNOSTICS");
            return value && std::string_view(value) != "0";
        }();
        return enabled;
    }

    double presentDiagnosticsThresholdMs() {
        static const double threshold = [] {
            constexpr double defaultThresholdMs = 20.0;
            const char* value = std::getenv("MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS");
            if (!value)
                return defaultThresholdMs;

            char* end{};
            const double parsed = std::strtod(value, &end);
            if (end == value || *end != '\0' || parsed < 0.0)
                return defaultThresholdMs;
            return parsed;
        }();
        return threshold;
    }

    std::optional<uint64_t> generatedImageAcquireTimeoutNs() {
        static const std::optional<uint64_t> timeout = []() -> std::optional<uint64_t> {
            const char* value = std::getenv("MAKO_PRESENT_ACQUIRE_TIMEOUT_MS");
            if (!value)
                return std::nullopt;

            char* end{};
            const double parsed = std::strtod(value, &end);
            if (end == value || *end != '\0' || parsed <= 0.0)
                return std::nullopt;

            constexpr uint64_t nanosecondsPerMillisecond = 1'000'000;
            const double maximumMilliseconds = static_cast<double>(
                (std::numeric_limits<uint64_t>::max() - 1) / nanosecondsPerMillisecond
            );
            const double clampedMilliseconds = std::min(parsed, maximumMilliseconds);
            return static_cast<uint64_t>(
                clampedMilliseconds * static_cast<double>(nanosecondsPerMillisecond)
            );
        }();
        return timeout;
    }

    double elapsedMilliseconds(const DiagnosticsClock::time_point start) {
        return std::chrono::duration<double, std::milli>(
            DiagnosticsClock::now() - start
        ).count();
    }

    DiagnosticsClock::time_point startPresentDiagnostic() {
        if (!presentDiagnosticsEnabled())
            return {};
        return DiagnosticsClock::now();
    }

    void logSlowPresentOperation(std::string_view operation,
            size_t frameIndex, size_t sequenceIndex,
            const DiagnosticsClock::time_point start,
            std::optional<VkResult> result = std::nullopt,
            std::optional<size_t> passIndex = std::nullopt,
            std::optional<uint32_t> imageIndex = std::nullopt) {
        if (!presentDiagnosticsEnabled())
            return;

        const double durationMs = elapsedMilliseconds(start);
        const bool resultFailed = result &&
            *result != VK_SUCCESS && *result != VK_SUBOPTIMAL_KHR;
        if (durationMs < presentDiagnosticsThresholdMs() && !resultFailed)
            return;

        std::ostringstream message;
        message << "mako: present diagnostics: operation=" << operation
                << " context=" << activeDiagnosticsContextId
                << " duration_ms=" << durationMs
                << " frame=" << frameIndex
                << " sequence=" << sequenceIndex;
        if (result)
            message << " result=" << static_cast<int>(*result);
        if (passIndex)
            message << " pass=" << *passIndex;
        if (imageIndex)
            message << " image=" << *imageIndex;
        std::cerr << message.str() << '\n';
    }

    void logPresentFallback(size_t frameIndex, size_t sequenceIndex,
            size_t passIndex, size_t skippedFrames, uint64_t timelineValue,
            std::string_view acquireMode, std::string_view backendWork) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation=skip-generated-frames"
                  << " context=" << activeDiagnosticsContextId
                  << " frame=" << frameIndex
                  << " sequence=" << sequenceIndex
                  << " pass=" << passIndex
                  << " skipped=" << skippedFrames
                  << " wait_timeline=" << timelineValue
                  << " acquire_mode=" << acquireMode
                  << " backend_work=" << backendWork
                  << '\n';
    }

    void logPresentRecovery(size_t frameIndex, size_t sequenceIndex,
            size_t passIndex, uint32_t imageIndex, size_t bypassedFrames,
            std::string_view acquireMode, size_t warmupFrames) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << (warmupFrames
                      ? "generated-image-recovered"
                      : "resume-generated-frames")
                  << " context=" << activeDiagnosticsContextId
                  << " frame=" << frameIndex
                  << " sequence=" << sequenceIndex
                  << " pass=" << passIndex
                  << " image=" << imageIndex
                  << " acquire_mode=" << acquireMode
                  << " bypassed_frames=" << bypassedFrames;
        if (warmupFrames)
            std::cerr << " recovery_warmup_frames=" << warmupFrames;
        std::cerr << " recovery_action=in-place";
        std::cerr << '\n';
    }

    void logHistoryWarmup(size_t frameIndex, size_t sequenceIndex,
            size_t remainingFrames, bool recovery,
            std::optional<uint32_t> acquiredImage) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation=history-warmup"
                  << " context=" << activeDiagnosticsContextId
                  << " frame=" << frameIndex
                  << " sequence=" << sequenceIndex
                  << " reason=" << (recovery ? "recovery" : "startup")
                  << " remaining_frames=" << remainingFrames;
        if (acquiredImage)
            std::cerr << " released_image=" << *acquiredImage;
        std::cerr << '\n';
    }

    void logAdaptiveStabilization(std::string_view reason,
            const std::chrono::steady_clock::duration duration) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-stabilization"
                  << " context=" << activeDiagnosticsContextId
                  << " reason=" << reason
                  << " duration_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         duration
                     ).count()
                  << '\n';
    }

    void logAdaptiveRamp(size_t previousLimit, size_t newLimit, double baseFps) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-ramp"
                  << " context=" << activeDiagnosticsContextId
                  << " previous_generated_limit=" << previousLimit
                  << " generated_limit=" << newLimit
                  << " base_fps=" << baseFps << '\n';
    }

    void logAdaptiveRampResult(bool accepted, size_t previousLimit,
            size_t testedLimit, double previousBaseFps, double currentBaseFps,
            double previousOutputFps, double currentOutputFps) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << (accepted ? "adaptive-ramp-accepted" : "adaptive-load-shed")
                  << " context=" << activeDiagnosticsContextId
                  << " previous_generated_limit=" << previousLimit
                  << " tested_generated_limit=" << testedLimit
                  << " previous_base_fps=" << previousBaseFps
                  << " current_base_fps=" << currentBaseFps
                  << " previous_output_fps=" << previousOutputFps
                  << " current_output_fps=" << currentOutputFps << '\n';
    }

    void logAdaptiveBridge(size_t previousLimit, size_t testedLimit,
            size_t bridgeLimit, double previousBaseFps, double currentBaseFps,
            double previousOutputFps, double currentOutputFps) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-bridge"
                  << " context=" << activeDiagnosticsContextId
                  << " previous_generated_limit=" << previousLimit
                  << " tested_generated_limit=" << testedLimit
                  << " bridge_generated_limit=" << bridgeLimit
                  << " previous_base_fps=" << previousBaseFps
                  << " current_base_fps=" << currentBaseFps
                  << " previous_output_fps=" << previousOutputFps
                  << " current_output_fps=" << currentOutputFps << '\n';
    }

    void logAdaptiveBridgeResult(bool accepted, size_t baselineLimit,
            size_t testedLimit, double baselineBaseFps, double currentBaseFps,
            double baselineOutputFps, double currentOutputFps) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << (accepted ? "adaptive-bridge-accepted" : "adaptive-bridge-rejected")
                  << " context=" << activeDiagnosticsContextId
                  << " baseline_generated_limit=" << baselineLimit
                  << " tested_generated_limit=" << testedLimit
                  << " baseline_base_fps=" << baselineBaseFps
                  << " current_base_fps=" << currentBaseFps
                  << " baseline_output_fps=" << baselineOutputFps
                  << " current_output_fps=" << currentOutputFps << '\n';
    }

    void logAdaptiveProbeAborted(std::string_view reason, size_t testedLimit) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-probe-aborted"
                  << " context=" << activeDiagnosticsContextId
                  << " reason=" << reason
                  << " tested_generated_limit=" << testedLimit << '\n';
    }

    void logAdaptiveRearm(std::string_view operation, std::string_view reason,
            size_t failures, size_t fallbackLimit,
            const DiagnosticsClock::duration cooldown,
            double baselineBaseFps, double currentBaseFps = 0.0,
            std::string_view decision = {}) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation=" << operation
                  << " context=" << activeDiagnosticsContextId
                  << " reason=" << reason
                  << " consecutive_failures=" << failures;
        if (operation == "adaptive-rearm-scheduled") {
            std::cerr << " cooldown_ms="
                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                             cooldown
                         ).count()
                      << " stable_required_ms="
                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                             AdaptiveScheduler::stableRearmDuration()
                         ).count()
                      << " fallback_generated_limit=" << fallbackLimit
                      << " baseline_base_fps=" << baselineBaseFps;
        } else {
            std::cerr << " fallback_generated_limit=" << fallbackLimit
                      << " baseline_base_fps=" << baselineBaseFps
                      << " current_base_fps=" << currentBaseFps;
            if (!decision.empty())
                std::cerr << " decision=" << decision;
        }
        std::cerr << '\n';
    }

    void logAdaptiveFastCadenceBurst(double baselineBaseFps,
            double instantaneousBaseFps, double thresholdFps,
            size_t ignoredFrames, size_t totalIgnoredFrames,
            const DiagnosticsClock::duration duration) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-fast-cadence-burst"
                  << " context=" << activeDiagnosticsContextId
                  << " baseline_base_fps=" << baselineBaseFps
                  << " instantaneous_base_fps=" << instantaneousBaseFps
                  << " threshold_fps=" << thresholdFps
                  << " ignored_frames=" << ignoredFrames
                  << " total_ignored_frames=" << totalIgnoredFrames
                  << " duration_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         duration
                     ).count()
                  << '\n';
    }

    void logAdaptiveFastCadenceBurstComplete(size_t totalIgnoredFrames,
            const DiagnosticsClock::duration duration) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-fast-cadence-burst-complete"
                  << " context=" << activeDiagnosticsContextId
                  << " total_ignored_frames=" << totalIgnoredFrames
                  << " duration_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         duration
                     ).count()
                  << '\n';
    }

    void logAdaptiveRampBackoff(size_t testedLimit, size_t failures,
            double baselineBaseFps,
            const std::chrono::steady_clock::duration delay) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-ramp-backoff"
                  << " context=" << activeDiagnosticsContextId
                  << " tested_generated_limit=" << testedLimit
                  << " consecutive_failures=" << failures
                  << " baseline_base_fps=" << baselineBaseFps
                  << " retry_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         delay
                     ).count()
                  << '\n';
    }

    void logAdaptiveRampEarlyRetry(size_t testedLimit, double failedBaseFps,
            double currentBaseFps) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-ramp-early-retry"
                  << " context=" << activeDiagnosticsContextId
                  << " tested_generated_limit=" << testedLimit
                  << " failed_base_fps=" << failedBaseFps
                  << " current_base_fps=" << currentBaseFps << '\n';
    }

    void logAdaptiveRecoveryResume(size_t generationLimit,
            const std::chrono::steady_clock::duration higherProbeDelay,
            std::string_view reason) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-recovery-resume-scheduled"
                  << " context=" << activeDiagnosticsContextId
                  << " generated_limit=" << generationLimit
                  << " higher_probe_delay_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         higherProbeDelay
                     ).count()
                  << " reason=" << reason << '\n';
    }

    void logAdaptiveStableCadence(std::string_view operation, size_t generatedLimit,
            double baselineBaseFps, double currentBaseFps,
            std::string_view reason = {}) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation=" << operation
                  << " context=" << activeDiagnosticsContextId
                  << " generated_limit=" << generatedLimit
                  << " baseline_base_fps=" << baselineBaseFps
                  << " current_base_fps=" << currentBaseFps
                  << " projected_output_fps="
                  << currentBaseFps * static_cast<double>(generatedLimit + 1);
        if (!reason.empty())
            std::cerr << " reason=" << reason;
        std::cerr << '\n';
    }

    void logAdaptiveRescueStart(size_t generatedLimit,
            double baselineBaseFps, double currentBaseFps,
            double projectedOutputFps,
            std::string_view reason = "stable-cadence-collapse") {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-rescue-start"
                  << " context=" << activeDiagnosticsContextId
                  << " generated_limit=" << generatedLimit
                  << " baseline_base_fps=" << baselineBaseFps
                  << " current_base_fps=" << currentBaseFps
                  << " projected_output_fps=" << projectedOutputFps
                  << " reason=" << reason
                  << " measurement_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         AdaptiveScheduler::rescueMeasurementDuration()
                     ).count()
                  << '\n';
    }

    void logAdaptiveRescueComplete(size_t previousLimit, size_t resumedLimit,
            size_t requestedLimit, size_t configuredLimit,
            double baselineBaseFps, double measuredBaseFps,
            std::string_view decision) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-rescue-complete"
                  << " context=" << activeDiagnosticsContextId
                  << " previous_generated_limit=" << previousLimit
                  << " resumed_generated_limit=" << resumedLimit
                  << " requested_generated_limit=" << requestedLimit
                  << " configured_generated_limit=" << configuredLimit
                  << " baseline_base_fps=" << baselineBaseFps
                  << " measured_base_fps=" << measuredBaseFps
                  << " decision=" << decision
                  << " cooldown_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         AdaptiveScheduler::rescueCooldown()
                     ).count()
                  << '\n';
    }

    void logAdaptiveDiscontinuityRecoveryStart(size_t generationLimit,
            double baselineBaseFps, std::string_view reason,
            std::chrono::steady_clock::duration maximumDuration) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-discontinuity-recovery-start"
                  << " context=" << activeDiagnosticsContextId
                  << " generated_limit=" << generationLimit
                  << " baseline_base_fps=" << baselineBaseFps
                  << " reason=" << reason
                  << " maximum_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         maximumDuration
                     ).count()
                  << '\n';
    }

    void logAdaptiveDiscontinuityRecoveryComplete(size_t generationLimit,
            double baselineBaseFps, double measuredBaseFps,
            std::string_view decision) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-discontinuity-recovery-complete"
                  << " context=" << activeDiagnosticsContextId
                  << " generated_limit=" << generationLimit
                  << " baseline_base_fps=" << baselineBaseFps
                  << " measured_base_fps=" << measuredBaseFps
                  << " decision=" << decision << '\n';
    }

    void logAdaptiveDiscontinuitySoftRecovery(size_t generationLimit) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-discontinuity-soft-recovery"
                  << " context=" << activeDiagnosticsContextId
                  << " generated_limit=" << generationLimit
                  << " action=history-warmup"
                  << '\n';
    }

    void logAdaptiveTwoXGameplayHitchRecovery(size_t generationLimit,
            double baselineBaseFps,
            const std::chrono::steady_clock::duration rawInterval) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-gameplay-hitch-recovery"
                  << " context=" << activeDiagnosticsContextId
                  << " generated_limit=" << generationLimit
                  << " baseline_base_fps=" << baselineBaseFps
                  << " raw_interval_ms="
                  << std::chrono::duration<double, std::milli>(
                         rawInterval
                     ).count()
                  << " history_warmup_frames="
                  << AdaptiveScheduler::historyWarmupFrameCount()
                  << '\n';
    }

    void logAdaptiveCadenceRefresh(std::string_view reason,
            size_t retainedGenerationLimit, size_t historyFrames) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-cadence-refresh"
                  << " context=" << activeDiagnosticsContextId
                  << " reason=" << reason
                  << " retained_generated_limit=" << retainedGenerationLimit
                  << " history_warmup_frames=" << historyFrames
                  << " policy=sdr-fast-resume\n";
    }

    void logAdaptiveLoadShed(size_t previousLimit, size_t resumedLimit,
            double baselineBaseFps, double currentBaseFps,
            std::string_view reason) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-load-shed"
                  << " context=" << activeDiagnosticsContextId
                  << " previous_generated_limit=" << previousLimit
                  << " resumed_generated_limit=" << resumedLimit
                  << " baseline_base_fps=" << baselineBaseFps
                  << " current_base_fps=" << currentBaseFps
                  << " reason=" << reason
                  << " action=retain-generated-output\n";
    }

    class SwapchainAdaptiveSchedulerDiagnostics final :
            public AdaptiveSchedulerDiagnostics {
    public:
        [[nodiscard]] bool enabled() const override {
            return presentDiagnosticsEnabled();
        }

        void plan(const AdaptivePlanDiagnostic& plan) override {
            if (!presentDiagnosticsEnabled())
                return;

            std::cerr << "mako: present diagnostics: operation=adaptive-plan"
                      << " context=" << activeDiagnosticsContextId
                      << " base_fps=" << plan.baseFps
                      << " target_fps=" << plan.targetFps
                      << " generated=" << plan.generatedFrames
                      << " max_generated=" << plan.maximumGeneratedFrames;
            if (plan.configuredMaximumGeneratedFrames) {
                std::cerr << " configured_max_generated="
                          << plan.configuredMaximumGeneratedFrames;
            }
            if (!plan.phase.empty())
                std::cerr << " phase=" << plan.phase;
            if (plan.recoveryGenerationLimit)
                std::cerr << " recovery_generated_limit="
                          << plan.recoveryGenerationLimit;
            if (!plan.rearmReason.empty()) {
                std::cerr << " rearm_reason=" << plan.rearmReason
                          << " cooldown_remaining_ms="
                          << std::chrono::duration_cast<std::chrono::milliseconds>(
                                 plan.rearmCooldownRemaining
                             ).count()
                          << " rearm_baseline_base_fps="
                          << plan.rearmBaselineBaseFps;
            }
            std::cerr << '\n';
        }

        void stabilization(std::string_view reason,
                std::chrono::steady_clock::duration duration) override {
            logAdaptiveStabilization(reason, duration);
        }
        void ramp(size_t previousLimit, size_t newLimit,
                double baseFps) override {
            logAdaptiveRamp(previousLimit, newLimit, baseFps);
        }
        void rampResult(bool accepted, size_t previousLimit,
                size_t testedLimit, double previousBaseFps,
                double currentBaseFps, double previousOutputFps,
                double currentOutputFps) override {
            logAdaptiveRampResult(
                accepted, previousLimit, testedLimit, previousBaseFps,
                currentBaseFps, previousOutputFps, currentOutputFps
            );
        }
        void bridge(size_t previousLimit, size_t testedLimit,
                size_t bridgeLimit, double previousBaseFps,
                double currentBaseFps, double previousOutputFps,
                double currentOutputFps) override {
            logAdaptiveBridge(
                previousLimit, testedLimit, bridgeLimit, previousBaseFps,
                currentBaseFps, previousOutputFps, currentOutputFps
            );
        }
        void bridgeResult(bool accepted, size_t baselineLimit,
                size_t testedLimit, double baselineBaseFps,
                double currentBaseFps, double baselineOutputFps,
                double currentOutputFps) override {
            logAdaptiveBridgeResult(
                accepted, baselineLimit, testedLimit, baselineBaseFps,
                currentBaseFps, baselineOutputFps, currentOutputFps
            );
        }
        void probeAborted(std::string_view reason,
                size_t testedLimit) override {
            logAdaptiveProbeAborted(reason, testedLimit);
        }
        void rearm(std::string_view operation, std::string_view reason,
                size_t failures, size_t fallbackLimit,
                std::chrono::steady_clock::duration cooldown,
                double baselineBaseFps, double currentBaseFps,
                std::string_view decision) override {
            logAdaptiveRearm(
                operation, reason, failures, fallbackLimit, cooldown,
                baselineBaseFps, currentBaseFps, decision
            );
        }
        void fastCadenceBurst(double baselineBaseFps,
                double instantaneousBaseFps, double thresholdFps,
                size_t ignoredFrames, size_t totalIgnoredFrames,
                std::chrono::steady_clock::duration duration) override {
            logAdaptiveFastCadenceBurst(
                baselineBaseFps, instantaneousBaseFps, thresholdFps,
                ignoredFrames, totalIgnoredFrames, duration
            );
        }
        void fastCadenceBurstComplete(size_t totalIgnoredFrames,
                std::chrono::steady_clock::duration duration) override {
            logAdaptiveFastCadenceBurstComplete(totalIgnoredFrames, duration);
        }
        void rampBackoff(size_t testedLimit, size_t failures,
                double baselineBaseFps,
                std::chrono::steady_clock::duration delay) override {
            logAdaptiveRampBackoff(
                testedLimit, failures, baselineBaseFps, delay
            );
        }
        void rampEarlyRetry(size_t testedLimit, double failedBaseFps,
                double currentBaseFps) override {
            logAdaptiveRampEarlyRetry(
                testedLimit, failedBaseFps, currentBaseFps
            );
        }
        void recoveryResume(size_t generationLimit,
                std::chrono::steady_clock::duration higherProbeDelay,
                std::string_view reason) override {
            logAdaptiveRecoveryResume(
                generationLimit, higherProbeDelay, reason
            );
        }
        void stableCadence(std::string_view operation,
                size_t generatedLimit, double baselineBaseFps,
                double currentBaseFps, std::string_view reason) override {
            logAdaptiveStableCadence(
                operation, generatedLimit, baselineBaseFps,
                currentBaseFps, reason
            );
        }
        void rescueStart(size_t generatedLimit, double baselineBaseFps,
                double currentBaseFps, double projectedOutputFps,
                std::string_view reason) override {
            logAdaptiveRescueStart(
                generatedLimit, baselineBaseFps, currentBaseFps,
                projectedOutputFps, reason
            );
        }
        void rescueComplete(size_t previousLimit, size_t resumedLimit,
                size_t requestedLimit, size_t configuredLimit,
                double baselineBaseFps, double measuredBaseFps,
                std::string_view decision) override {
            logAdaptiveRescueComplete(
                previousLimit, resumedLimit, requestedLimit, configuredLimit,
                baselineBaseFps, measuredBaseFps, decision
            );
        }
        void discontinuityRecoveryStart(size_t generationLimit,
                double baselineBaseFps, std::string_view reason,
                std::chrono::steady_clock::duration maximumDuration) override {
            logAdaptiveDiscontinuityRecoveryStart(
                generationLimit, baselineBaseFps, reason, maximumDuration
            );
        }
        void discontinuityRecoveryComplete(size_t generationLimit,
                double baselineBaseFps, double measuredBaseFps,
                std::string_view decision) override {
            logAdaptiveDiscontinuityRecoveryComplete(
                generationLimit, baselineBaseFps, measuredBaseFps, decision
            );
        }
        void twoXGameplayHitchRecovery(size_t generationLimit,
                double baselineBaseFps,
                std::chrono::steady_clock::duration rawInterval) override {
            logAdaptiveTwoXGameplayHitchRecovery(
                generationLimit, baselineBaseFps, rawInterval
            );
        }
        void cadenceRefresh(std::string_view reason,
                size_t retainedGenerationLimit,
                size_t historyFrames) override {
            logAdaptiveCadenceRefresh(
                reason, retainedGenerationLimit, historyFrames
            );
        }
        void loadShed(size_t previousLimit, size_t resumedLimit,
                double baselineBaseFps, double currentBaseFps,
                std::string_view reason) override {
            logAdaptiveLoadShed(
                previousLimit, resumedLimit, baselineBaseFps,
                currentBaseFps, reason
            );
        }
    };

    SwapchainAdaptiveSchedulerDiagnostics adaptiveSchedulerDiagnostics;

    void logSwapchainRecreationSuppressed(const std::string_view reason,
            const double remainingMs) {
        if (!presentDiagnosticsEnabled())
            return;

        std::cerr << "mako: present diagnostics: operation=swapchain-recreation-suppressed"
                  << " context=" << activeDiagnosticsContextId
                  << " reason=" << reason
                  << " remaining_ms=" << remainingMs << '\n';
    }

    VkImageMemoryBarrier barrierHelper(VkImage handle,
            VkAccessFlags srcAccessMask,
            VkAccessFlags dstAccessMask,
            VkImageLayout oldLayout,
            VkImageLayout newLayout) {
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

bool layer::context_ModifySwapchainCreateInfo(const ls::GameConf& profile,
        uint32_t maxImages,
        VkSwapchainCreateInfoKHR& createInfo, const bool gamescopeHdrActive,
        const bool gamescopeManaged, const bool hdrExposureDisabled) {
    const auto colorPipeline = classifySwapchainColor(
        createInfo.imageFormat, createInfo.imageColorSpace,
        gamescopeHdrActive
    );
    if (!colorPipeline.generationSupported ||
            (hdrExposureDisabled && colorPipeline.hdr))
        return false;

    createInfo.imageUsage |=
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    switch (profile.pacing) {
        case ls::Pacing::None:
            // Reserve the selected Fixed or Adaptive capacity even while live
            // generation is off. The game owns this swapchain, so retaining
            // its capacity is what lets configuration reload turn generation
            // back on without forcing a game restart or swapchain rebuild.
            createInfo.minImageCount += generatedFrameCapacity(profile) + 1;
            if (maxImages && createInfo.minImageCount > maxImages)
                createInfo.minImageCount = maxImages;

            const bool hdrCapableSwapchain = !hdrExposureDisabled &&
                (colorPipeline.hdr ||
                    colorPipeline.encoding == backend::FrameEncoding::SdrHighPrecision);
            const auto transport = selectPresentationTransport(
                gamescopeManaged, hdrCapableSwapchain
            );

            // This is a create-time transport choice, not the current HDR on/off
            // state. Plain SDR keeps the fork's known-good FIFO ordering. An
            // HDR-capable Gamescope swapchain preserves Gamescope's lower WSI
            // contract even while live HDR feedback says inactive, because
            // that feedback may turn on later without a swapchain replacement.
            // Forcing this HDR bridge to FIFO caused every generated/original
            // lower present to block for 30-40 ms.
            if (transport == PresentationTransport::OrderedSdr) {
                createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
                return true;
            }
            return false;
    }

    return false;
}

Swapchain::Swapchain(const vk::Vulkan& vk, backend::Instance& backend,
            ls::GameConf profile, SwapchainInfo info,
            const std::optional<bool> gamescopeHdrActive,
            const bool gamescopeManaged,
            const bool hdrExposureDisabled,
            const std::optional<uint32_t> gamescopeRefreshHz,
            const uint64_t runtimeStateRevision) :
        instance(backend),
        gamescopeManaged(gamescopeManaged),
        privateOrderedTransport(info.privateOrderedTransport),
        gamescopeRefreshHz(gamescopeRefreshHz),
        colorPipeline(initialColorPipeline(
            info.format, info.colorSpace, gamescopeHdrActive, gamescopeManaged,
            hdrExposureDisabled
        )),
        profile(std::move(profile)), info(std::move(info)) {
    this->diagnosticsContextId = allocateDiagnosticsContextId();
    const DiagnosticsContextScope diagnosticsContext(
        this->diagnosticsContextId
    );
    const VkExtent2D extent = this->info.extent;

    bool applicationPackedHdr10Supported = false;
    bool backendPackedHdr10Supported = false;
    selectPackedHdr10Transport(
        vk, backend, this->colorPipeline,
        applicationPackedHdr10Supported, backendPackedHdr10Supported
    );

    if (presentDiagnosticsEnabled()) {
        std::cerr << "mako: present diagnostics: operation=runtime-state-applied"
                  << " context=" << this->diagnosticsContextId
                  << " state_revision=" << runtimeStateRevision
                  << " adaptive=" << this->profile.adaptive
                  << " target_fps=" << this->profile.target_fps
                  << " multiplier=" << this->profile.multiplier
                  << " adaptive_max_multiplier="
                  << this->profile.adaptive_max_multiplier
                  << " stable_cadence="
                  << this->profile.adaptive_stable_cadence
                  << " hdr=" << this->colorPipeline.hdr
                  << '\n';
    }

    std::cerr << "mako: swapchain colour pipeline: format="
              << static_cast<int>(this->info.format)
              << "; color-space=" << static_cast<int>(this->info.colorSpace)
              << "; mode=" << this->colorPipeline.name
              << "; source="
              << (this->colorPipeline.gamescopeColorSpaceRecovered
                    ? "gamescope-normalized" : "application")
              << "; transport="
              << (this->colorPipeline.packedHdr10Transport
                    ? "packed-hdr10-32-bit"
                    : (transportBytesPerPixel(this->colorPipeline.encoding) == 8
                        ? "rgba16f-64-bit" : "rgba8-32-bit"))
              << "; frame-generation="
              << (this->colorPipeline.generationSupported ? "supported" : "passthrough")
              << '\n';
    if (this->gamescopeManaged && this->privateOrderedTransport) {
        std::cerr << "mako: Gamescope SDR presentation transport: "
                     "mode=fifo-ordered; source=fork-develop; "
                     "dynamic-mode-switch=filtered\n";
    }

    if (this->colorPipeline.encoding == backend::FrameEncoding::Hdr10Pq ||
            this->colorPipeline.encoding ==
                backend::FrameEncoding::Hdr10PqPacked) {
        const uint64_t transportImageCount = 2 +
            generatedFrameCapacity(this->profile);
        const uint64_t floatTransportBytes =
            static_cast<uint64_t>(extent.width) * extent.height *
            transportImageCount * 8;
        const uint64_t selectedTransportBytes =
            static_cast<uint64_t>(extent.width) * extent.height *
            transportImageCount *
            transportBytesPerPixel(this->colorPipeline.encoding);
        std::cerr << "mako: HDR10 transport: mode="
                  << (this->colorPipeline.packedHdr10Transport
                        ? "packed-10-bit" : "rgba16f")
                  << "; nominal_bytes=" << selectedTransportBytes
                  << "; nominal_bytes_saved="
                  << (floatTransportBytes - selectedTransportBytes)
                  << "; application_device_supported="
                  << applicationPackedHdr10Supported
                  << "; backend_device_supported="
                  << backendPackedHdr10Supported << '\n';
    }

    if (!this->colorPipeline.generationSupported) {
        std::cerr << "mako: frame generation disabled for this swapchain: "
                  << this->colorPipeline.reason << '\n';
        return;
    }

    try {
        std::vector<int> sourceFds(2);
        std::vector<int> destinationFds(generatedFrameCapacity(this->profile));

        this->sourceImages.reserve(sourceFds.size());
        for (int& fd : sourceFds)
            this->sourceImages.emplace_back(vk,
                extent, this->colorPipeline.exchangeFormat,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                std::nullopt, &fd);

        this->destinationImages.reserve(destinationFds.size());
        for (int& fd : destinationFds)
            this->destinationImages.emplace_back(vk,
                extent, this->colorPipeline.exchangeFormat,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                std::nullopt, &fd);

        if (this->profile.adaptive) {
            this->adaptiveScheduler.emplace(
                AdaptiveSchedulerConfig{
                    .targetFps = this->profile.target_fps,
                    .maximumMultiplier = this->profile.adaptive_max_multiplier,
                    .generatedFrameCapacity = this->destinationImages.size(),
                    .stableCadence = this->profile.adaptive_stable_cadence,
                    .recoveryPolicy = this->privateOrderedTransport
                        ? AdaptiveRecoveryPolicy::OrderedSdr
                        : AdaptiveRecoveryPolicy::ConservativeHdr,
                },
                &adaptiveSchedulerDiagnostics
            );
        }

        int syncFd{};
        this->syncSemaphore.emplace(vk, 0, std::nullopt, &syncFd);

        try {
            this->ctx = ls::owned_ptr<ls::R<backend::Context>>(
                new ls::R<backend::Context>(backend.openContext(
                    { sourceFds.at(0), sourceFds.at(1) }, destinationFds, syncFd,
                    extent.width, extent.height,
                    this->colorPipeline.encoding,
                    1.0F / this->profile.flow_scale, this->profile.performance_mode
                )),
                [backend = &backend](ls::R<backend::Context>& ctx) {
                    backend->closeContext(ctx);
                }
            );

            backend::makeLeaking(); // don't worry about it :3
        } catch (const std::exception& e) {
            throw ls::error("failed to create swapchain context", e);
        }

        this->renderCommandBuffer.emplace(vk);
        this->renderFence.emplace(vk);
        for (size_t i = 0; i < this->destinationImages.size(); i++) {
            this->passes.emplace_back(RenderPass {
                .commandBuffer = vk::CommandBuffer(vk),
                .acquireSemaphore = vk::Semaphore(vk)
            });
        }

        this->fixedFrameTimestamps = buildFixedFrameTimestamps(
            this->profile.multiplier, this->destinationImages.size()
        );

        if (!this->profile.frame_generation_enabled)
            std::cerr << "mako: frame generation is off; retained private "
                         "resources permit a live enable\n";

        const size_t frames = std::max(
            this->info.images.size(), this->destinationImages.size() + 2
        );
        for (size_t i = 0; i < frames; i++) {
            this->postCopySemaphores.emplace_back(
                vk::Semaphore(vk),
                vk::Semaphore(vk)
            );
        }

        if (presentDiagnosticsEnabled()) {
            std::cerr << "mako: present diagnostics enabled; context="
                      << activeDiagnosticsContextId
                      << "; slow operation threshold is "
                      << presentDiagnosticsThresholdMs() << " ms\n";
        }
        if (this->gamescopeManaged && !this->privateOrderedTransport) {
            std::cerr << "mako: Gamescope HDR generated-image admission is "
                         "nonblocking; native presentation is never held for "
                         "a synthetic destination\n";
        } else if (this->gamescopeManaged) {
            std::cerr << "mako: Gamescope SDR uses the fork's ordered "
                         "presentation path\n";
        } else if (const auto timeout = generatedImageAcquireTimeoutNs()) {
            std::cerr << "mako: generated-image acquire timeout enabled at "
                      << static_cast<double>(*timeout) / 1'000'000.0
                      << " ms for the legacy non-Gamescope path; stalled "
                         "generated frames will be skipped\n";
        }
        if (this->profile.adaptive) {
            std::cerr << "mako: adaptive frame generation enabled; target="
                      << this->profile.target_fps
                      << " fps, maximum multiplier="
                      << this->profile.adaptive_max_multiplier
                      << "x, stable cadence="
                      << (this->profile.adaptive_stable_cadence ? "enabled" : "disabled")
                      << '\n';
            const auto schedulerNow = DiagnosticsClock::now();
            this->adaptiveScheduler->beginStabilization(
                schedulerNow, "startup"
            );
        }
    } catch (const std::exception& e) {
        // Swapchain creation belongs to the game. A failure in LSFG's optional
        // interpolation resources must not turn a valid game swapchain into a
        // startup failure, especially when a driver exposes an HDR format that
        // cannot be initialized on this device. Keep the native swapchain and
        // present its real frames until the game recreates it.
        this->ctx = {};
        this->sourceImages.clear();
        this->destinationImages.clear();
        this->passes.clear();
        this->postCopySemaphores.clear();
        this->adaptiveScheduler.reset();
        this->colorPipeline.generationSupported = false;
        this->colorPipeline.name = "initialization-fallback";
        this->colorPipeline.reason =
            "LSFG frame-generation initialization failed; native presentation retained";
        std::cerr << "mako: " << this->colorPipeline.reason
                  << ": " << e.what() << '\n';
    }
}

void Swapchain::rebuildPrivateResources(const vk::Vulkan& vk,
        SwapchainColorPipeline pipeline) {
    auto& backendInstance = this->instance.get();
    bool applicationPackedHdr10Supported = false;
    bool backendPackedHdr10Supported = false;
    selectPackedHdr10Transport(
        vk, backendInstance, pipeline,
        applicationPackedHdr10Supported, backendPackedHdr10Supported
    );

    if (!pipeline.generationSupported) {
        this->ctx = {};
        this->sourceImages.clear();
        this->destinationImages.clear();
        this->syncSemaphore = {};
        this->renderCommandBuffer = {};
        this->renderFence = {};
        this->passes.clear();
        this->postCopySemaphores.clear();
        this->adaptiveScheduler.reset();
        this->colorPipeline = std::move(pipeline);
        this->configurationHistoryWarmupRemaining = 0;
        return;
    }

    const VkExtent2D extent = this->info.extent;
    std::vector<int> sourceFds(2);
    std::vector<int> destinationFds(generatedFrameCapacity(this->profile));
    std::vector<vk::Image> newSourceImages;
    std::vector<vk::Image> newDestinationImages;
    newSourceImages.reserve(sourceFds.size());
    newDestinationImages.reserve(destinationFds.size());
    for (int& fd : sourceFds) {
        newSourceImages.emplace_back(vk,
            extent, pipeline.exchangeFormat,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &fd);
    }
    for (int& fd : destinationFds) {
        newDestinationImages.emplace_back(vk,
            extent, pipeline.exchangeFormat,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &fd);
    }

    ls::lazy<vk::TimelineSemaphore> newSyncSemaphore;
    int syncFd{};
    newSyncSemaphore.emplace(vk, 0, std::nullopt, &syncFd);

    ls::lazy<vk::CommandBuffer> newRenderCommandBuffer;
    ls::lazy<vk::Fence> newRenderFence;
    newRenderCommandBuffer.emplace(vk);
    newRenderFence.emplace(vk);
    std::vector<RenderPass> newPasses;
    newPasses.reserve(newDestinationImages.size());
    for (size_t i = 0; i < newDestinationImages.size(); ++i) {
        static_cast<void>(i);
        newPasses.emplace_back(RenderPass{
            .commandBuffer = vk::CommandBuffer(vk),
            .acquireSemaphore = vk::Semaphore(vk),
        });
    }
    std::vector<std::pair<vk::Semaphore, vk::Semaphore>> newPostCopySemaphores;
    const size_t semaphoreFrames = std::max(
        this->info.images.size(), newDestinationImages.size() + 2
    );
    newPostCopySemaphores.reserve(semaphoreFrames);
    for (size_t i = 0; i < semaphoreFrames; ++i) {
        static_cast<void>(i);
        newPostCopySemaphores.emplace_back(
            vk::Semaphore(vk), vk::Semaphore(vk)
        );
    }

    ls::owned_ptr<ls::R<backend::Context>> newContext(
        new ls::R<backend::Context>(backendInstance.openContext(
            {sourceFds.at(0), sourceFds.at(1)}, destinationFds, syncFd,
            extent.width, extent.height, pipeline.encoding,
            1.0F / this->profile.flow_scale, this->profile.performance_mode
        )),
        [backend = &backendInstance](ls::R<backend::Context>& context) {
            backend->closeContext(context);
        }
    );
    backend::makeLeaking();

    // Everything above is constructed before the active resources are
    // touched. Moving the context first retires the old backend imports while
    // their Vulkan images are still alive.
    this->ctx = std::move(newContext);
    this->sourceImages = std::move(newSourceImages);
    this->destinationImages = std::move(newDestinationImages);
    this->syncSemaphore = std::move(newSyncSemaphore);
    this->renderCommandBuffer = std::move(newRenderCommandBuffer);
    this->renderFence = std::move(newRenderFence);
    this->passes = std::move(newPasses);
    this->postCopySemaphores = std::move(newPostCopySemaphores);
    this->colorPipeline = std::move(pipeline);

    this->idx = 1;
    this->backendFrameIndex = 0;
    this->renderFenceInFlight = false;
    this->backendRecoveryPending = false;
    this->generatedImageAdmission.reset();
    this->pipelineBusyRecovery.reset();
    this->fixedRefreshBudget.reset();
    this->fixedFrameTimestamps = buildFixedFrameTimestamps(
        this->profile.multiplier, this->destinationImages.size()
    );
    this->fixedDiagnosticWindowStarted.reset();
    this->fixedDiagnosticRealFrames = 0;
    this->fixedDiagnosticGeneratedFrames = 0;
    this->fixedDiagnosticSkippedFrames = 0;

    if (this->profile.adaptive) {
        this->adaptiveScheduler.emplace(
            AdaptiveSchedulerConfig{
                .targetFps = this->profile.target_fps,
                .maximumMultiplier = this->profile.adaptive_max_multiplier,
                .generatedFrameCapacity = this->destinationImages.size(),
                .stableCadence = this->profile.adaptive_stable_cadence,
                .recoveryPolicy = this->privateOrderedTransport
                    ? AdaptiveRecoveryPolicy::OrderedSdr
                    : AdaptiveRecoveryPolicy::ConservativeHdr,
            },
            &adaptiveSchedulerDiagnostics
        );
        this->adaptiveScheduler->beginStabilization(
            DiagnosticsClock::now(), "hdr-private-transition"
        );
        this->configurationHistoryWarmupRemaining = 0;
    } else {
        this->adaptiveScheduler.reset();
        this->configurationHistoryWarmupRemaining =
            AdaptiveScheduler::historyWarmupFrameCount();
    }

    std::cerr << "mako: swapchain colour pipeline transitioned in place: mode="
              << this->colorPipeline.name
              << "; transport="
              << (this->colorPipeline.packedHdr10Transport
                    ? "packed-hdr10-32-bit"
                    : (transportBytesPerPixel(this->colorPipeline.encoding) == 8
                        ? "rgba16f-64-bit" : "rgba8-32-bit"))
              << "; application_device_supported="
              << applicationPackedHdr10Supported
              << "; backend_device_supported="
              << backendPackedHdr10Supported << '\n';
    if (this->colorPipeline.encoding == backend::FrameEncoding::Hdr10Pq ||
            this->colorPipeline.encoding ==
                backend::FrameEncoding::Hdr10PqPacked) {
        const uint64_t transportImageCount = 2 +
            generatedFrameCapacity(this->profile);
        const uint64_t floatTransportBytes =
            static_cast<uint64_t>(extent.width) * extent.height *
            transportImageCount * 8;
        const uint64_t selectedTransportBytes =
            static_cast<uint64_t>(extent.width) * extent.height *
            transportImageCount *
            transportBytesPerPixel(this->colorPipeline.encoding);
        std::cerr << "mako: HDR10 transport: mode="
                  << (this->colorPipeline.packedHdr10Transport
                        ? "packed-10-bit" : "rgba16f")
                  << "; nominal_bytes=" << selectedTransportBytes
                  << "; nominal_bytes_saved="
                  << (floatTransportBytes - selectedTransportBytes)
                  << "; application_device_supported="
                  << applicationPackedHdr10Supported
                  << "; backend_device_supported="
                  << backendPackedHdr10Supported << '\n';
    }
}

bool Swapchain::applyPendingColorPipeline(const vk::Vulkan& vk) {
    if (!this->pendingGamescopeHdrActive)
        return true;

    const auto now = DiagnosticsClock::now();
    if (this->colorTransitionRetryAt && now < *this->colorTransitionRetryAt)
        return false;

    const bool resourcesAvailable = this->sourceImages.size() == 2 &&
        !this->destinationImages.empty() && this->syncSemaphore.has_value();
    if (resourcesAvailable) {
        try {
            if (!this->instance.get().contextReady(this->ctx.get()))
                return false;
            if (this->renderFenceInFlight &&
                    !this->renderFence->wait(vk, 0))
                return false;
        } catch (const std::exception& error) {
            std::cerr << "mako: private colour transition readiness poll "
                         "failed; real-frame passthrough retained: "
                      << error.what() << '\n';
            this->colorTransitionRetryAt = now + std::chrono::seconds(1);
            return false;
        }
    }

    auto desiredPipeline = classifySwapchainColor(
        this->info.format, this->info.colorSpace,
        *this->pendingGamescopeHdrActive
    );
    try {
        this->rebuildPrivateResources(vk, std::move(desiredPipeline));
    } catch (const std::exception& error) {
        std::cerr << "mako: private colour transition failed; real-frame "
                     "passthrough retained and retry scheduled: "
                  << error.what() << '\n';
        this->colorTransitionRetryAt = now + std::chrono::seconds(5);
        return false;
    }

    if (presentDiagnosticsEnabled()) {
        std::cerr << "mako: present diagnostics: "
                     "operation=runtime-transition-applied"
                  << " context=" << this->diagnosticsContextId
                  << " state_revision=" << this->pendingHdrStateRevision
                  << " reason=hdr-mode"
                  << " transition=private-context"
                  << " hdr=" << this->colorPipeline.hdr << '\n';
    }
    this->pendingGamescopeHdrActive.reset();
    this->pendingHdrStateRevision = 0;
    this->colorTransitionRetryAt.reset();
    return true;
}

ProfileUpdateAction Swapchain::updateProfile(
        const ls::GameConf& nextProfile,
        const uint64_t runtimeStateRevision) {
    if (!this->colorPipeline.generationSupported) {
        this->profile = nextProfile;
        return ProfileUpdateAction::NoRuntimeChange;
    }

    const bool resourcesAvailable = this->sourceImages.size() == 2 &&
        !this->destinationImages.empty() && this->syncSemaphore.has_value();
    const auto decision = classifyProfileUpdate(
        this->profile, nextProfile, this->destinationImages.size(), resourcesAvailable
    );

    if (decision.action == ProfileUpdateAction::DeferUntilSwapchainRecreation) {
        // Turning generation off is always safe and should not be delayed just
        // because the same write also changed a resource-shape setting.
        if (this->profile.frame_generation_enabled &&
                !nextProfile.frame_generation_enabled)
            this->disableFrameGeneration();
        if (presentDiagnosticsEnabled()) {
            std::cerr << "mako: present diagnostics: "
                         "operation=runtime-transition-pending"
                      << " context=" << this->diagnosticsContextId
                      << " state_revision=" << runtimeStateRevision
                      << " reason=profile-resources"
                      << " action=wait-for-natural-swapchain-recreation\n";
        }
        return decision.action;
    }

    if (decision.action == ProfileUpdateAction::NoRuntimeChange) {
        this->profile = nextProfile;
        return decision.action;
    }

    const bool wasAdaptive = this->profile.adaptive;
    const bool enabling = !this->profile.frame_generation_enabled &&
        nextProfile.frame_generation_enabled;
    const bool disabling = this->profile.frame_generation_enabled &&
        !nextProfile.frame_generation_enabled;
    this->profile = nextProfile;
    this->fixedFrameTimestamps = buildFixedFrameTimestamps(
        this->profile.multiplier, this->destinationImages.size()
    );
    if (decision.generationModeChanged || decision.fixedMultiplierChanged ||
            enabling || disabling) {
        this->fixedRefreshBudget.reset();
        this->fixedDiagnosticWindowStarted.reset();
        this->fixedDiagnosticRealFrames = 0;
        this->fixedDiagnosticGeneratedFrames = 0;
        this->fixedDiagnosticSkippedFrames = 0;
    }

    if (disabling) {
        this->configurationHistoryWarmupRemaining = 0;
        if (this->adaptiveScheduler)
            this->adaptiveScheduler->cancelHistoryWarmup();
    }

    if (enabling) {
        this->generatedImageAdmission.reset();
        this->pipelineBusyRecovery.reset();
        this->configurationHistoryWarmupRemaining = this->profile.adaptive
            ? 0
            : AdaptiveScheduler::historyWarmupFrameCount();
    }

    if (this->profile.adaptive &&
            (decision.adaptivePolicyChanged ||
             decision.generationModeChanged || enabling)) {
        this->adaptiveScheduler.emplace(
            AdaptiveSchedulerConfig{
                .targetFps = this->profile.target_fps,
                .maximumMultiplier = this->profile.adaptive_max_multiplier,
                .generatedFrameCapacity = this->destinationImages.size(),
                .stableCadence = this->profile.adaptive_stable_cadence,
                .recoveryPolicy = this->privateOrderedTransport
                    ? AdaptiveRecoveryPolicy::OrderedSdr
                    : AdaptiveRecoveryPolicy::ConservativeHdr,
            },
            &adaptiveSchedulerDiagnostics
        );
        this->adaptiveScheduler->beginStabilization(
            DiagnosticsClock::now(), "configuration-update"
        );
    } else if (wasAdaptive && !this->profile.adaptive) {
        this->adaptiveScheduler.reset();
        this->configurationHistoryWarmupRemaining =
            AdaptiveScheduler::historyWarmupFrameCount();
    }

    if (presentDiagnosticsEnabled()) {
        std::cerr << "mako: present diagnostics: operation=runtime-state-applied"
                  << " context=" << this->diagnosticsContextId
                  << " state_revision=" << runtimeStateRevision
                  << " transition=live"
                  << " adaptive=" << this->profile.adaptive
                  << " target_fps=" << this->profile.target_fps
                  << " multiplier=" << this->profile.multiplier
                  << " adaptive_max_multiplier="
                  << this->profile.adaptive_max_multiplier
                  << " stable_cadence="
                  << this->profile.adaptive_stable_cadence
                  << " hdr=" << this->colorPipeline.hdr
                  << '\n';
    }

    return decision.action;
}

bool Swapchain::updateGamescopeHdrState(
        const bool active, const uint64_t runtimeStateRevision) {
    const auto desiredPipeline = classifySwapchainColor(
        this->info.format, this->info.colorSpace, active
    );
    if (!this->pendingGamescopeHdrActive &&
            desiredPipeline.name == this->colorPipeline.name &&
            desiredPipeline.generationSupported ==
                this->colorPipeline.generationSupported) {
        return false;
    }

    this->pendingGamescopeHdrActive = active;
    this->pendingHdrStateRevision = runtimeStateRevision;
    this->colorTransitionRetryAt.reset();

    if (presentDiagnosticsEnabled()) {
        std::cerr << "mako: present diagnostics: "
                     "operation=runtime-transition-pending"
                  << " context=" << this->diagnosticsContextId
                  << " state_revision=" << runtimeStateRevision
                  << " reason=hdr-mode"
                  << " current=" << this->colorPipeline.name
                  << " requested=" << desiredPipeline.name
                  << " action=rebuild-private-context\n";
    }
    return true;
}

void Swapchain::updateGamescopeRefreshRate(
        const std::optional<uint32_t> refreshHz) {
    if (refreshHz == this->gamescopeRefreshHz)
        return;
    this->gamescopeRefreshHz = refreshHz;
    this->fixedRefreshBudget.reset();
    if (presentDiagnosticsEnabled()) {
        std::cerr << "mako: present diagnostics: "
                     "operation=gamescope-refresh-rate-applied"
                  << " context=" << this->diagnosticsContextId
                  << " refresh_hz=" << refreshHz.value_or(0) << '\n';
    }
}

void Swapchain::disableFrameGeneration() {
    if (!this->profile.frame_generation_enabled)
        return;

    this->profile.frame_generation_enabled = false;
    this->configurationHistoryWarmupRemaining = 0;
    if (this->adaptiveScheduler)
        this->adaptiveScheduler->cancelHistoryWarmup();
}

VkResult Swapchain::retireAcquiredImagesAndPresent(const vk::Vulkan& vk,
        const VkQueue queue, const VkSwapchainKHR swapchain,
        const void* nextChain, const uint32_t originalImageIndex,
        const std::vector<VkSemaphore>& applicationWaitSemaphores,
        const std::span<const uint32_t> acquiredImageIndices,
        const VkImage originalImage) {
    if (acquiredImageIndices.empty())
        throw ls::error("attempted to retire an empty acquired-image batch");

    this->renderFence->reset(vk);
    const size_t semaphoreBase = (
        this->idx + acquiredImageIndices.size() + 1
    ) % this->postCopySemaphores.size();

    for (size_t i = 0; i < acquiredImageIndices.size(); ++i) {
        const bool first = i == 0;
        const bool last = i + 1 == acquiredImageIndices.size();
        const size_t semaphoreIndex =
            (semaphoreBase + i) % this->postCopySemaphores.size();
        auto& pcs = this->postCopySemaphores.at(semaphoreIndex);
        auto& pass = this->passes.at(i);
        const auto acquiredImage = this->info.images.at(
            acquiredImageIndices[i]
        );

        std::vector<vk::Barrier> preBarriers;
        if (first) {
            preBarriers.push_back(barrierHelper(
                originalImage,
                VK_ACCESS_NONE,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            ));
        }
        preBarriers.push_back(barrierHelper(
            acquiredImage,
            VK_ACCESS_NONE,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        ));

        std::vector<vk::Barrier> postBarriers{
            barrierHelper(
                acquiredImage,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_MEMORY_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            )
        };
        if (last) {
            postBarriers.push_back(barrierHelper(
                originalImage,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_MEMORY_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            ));
        }

        auto& commandBuffer = pass.commandBuffer;
        commandBuffer.begin(vk);
        commandBuffer.blitImage(
            vk, preBarriers, {originalImage, acquiredImage},
            this->info.extent, postBarriers
        );
        commandBuffer.end(vk);

        std::vector<VkSemaphore> waits{
            pass.acquireSemaphore.handle()
        };
        if (first) {
            waits.insert(
                waits.end(), applicationWaitSemaphores.begin(),
                applicationWaitSemaphores.end()
            );
        } else {
            const size_t previousSemaphoreIndex =
                (semaphoreBase + i - 1) % this->postCopySemaphores.size();
            waits.push_back(
                this->postCopySemaphores.at(previousSemaphoreIndex)
                    .second.handle()
            );
        }
        commandBuffer.submit(
            vk, waits, VK_NULL_HANDLE, 0,
            {pcs.first.handle(), pcs.second.handle()},
            VK_NULL_HANDLE, 0,
            last ? this->renderFence->handle() : VK_NULL_HANDLE
        );
    }
    this->renderFenceInFlight = true;

    for (size_t i = 0; i < acquiredImageIndices.size(); ++i) {
        const size_t semaphoreIndex =
            (semaphoreBase + i) % this->postCopySemaphores.size();
        const VkSemaphore waitSemaphore =
            this->postCopySemaphores.at(semaphoreIndex).first.handle();
        const uint32_t acquiredImageIndex = acquiredImageIndices[i];
        const VkPresentInfoKHR acquiredPresentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &waitSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &acquiredImageIndex,
        };
        const auto acquiredResult = vk.df().QueuePresentKHR(
            queue, &acquiredPresentInfo
        );
        if (acquiredResult != VK_SUCCESS &&
                acquiredResult != VK_SUBOPTIMAL_KHR) {
            throw ls::vulkan_error(
                acquiredResult, "vkQueuePresentKHR() failed"
            );
        }
    }

    const size_t lastSemaphoreIndex = (
        semaphoreBase + acquiredImageIndices.size() - 1
    ) % this->postCopySemaphores.size();
    const VkSemaphore originalWaitSemaphore =
        this->postCopySemaphores.at(lastSemaphoreIndex).second.handle();
    const VkPresentInfoKHR originalPresentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nextChain,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &originalWaitSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &originalImageIndex,
    };
    const auto result = vk.df().QueuePresentKHR(queue, &originalPresentInfo);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");

    if (presentDiagnosticsEnabled()) {
        std::cerr << "mako: present diagnostics: "
                     "operation=retire-acquired-images"
                  << " context=" << this->diagnosticsContextId
                  << " images=" << acquiredImageIndices.size()
                  << " reason=backend-schedule-failure\n";
    }
    this->fidx++;
    return result;
}

VkResult Swapchain::present(const vk::Vulkan& vk,
        VkQueue queue, VkSwapchainKHR swapchain,
        void* next_chain, uint32_t imageIdx,
        const std::vector<VkSemaphore>& semaphores) {
    const DiagnosticsContextScope diagnosticsContext(
        this->diagnosticsContextId
    );
    const void* lowerNextChain = next_chain;
    // Match the immutable create-time choice. Ordered SDR filters Gamescope's
    // dynamic MAILBOX override so the lower FIFO swapchain stays ordered. HDR
    // preserves it. Never decide this from live colourPipeline.hdr: a feedback
    // transition cannot change the VkSwapchainKHR's creation contract.
    ScopedPNextRemoval presentMode(
        lowerNextChain, VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT,
        this->privateOrderedTransport
    );
    const bool gamescopeHdrTransport =
        this->gamescopeManaged && !this->privateOrderedTransport;

    const auto presentNow = DiagnosticsClock::now();
    const auto presentStarted = startPresentDiagnostic();
    if (this->lastPresentStarted) {
        const auto interval = presentNow - *this->lastPresentStarted;
        if (interval > DiagnosticsClock::duration::zero() &&
                interval < std::chrono::seconds(1))
            this->recentRealInterval = interval;
    }
    this->lastPresentStarted = presentNow;
    if (presentDiagnosticsEnabled() && !this->profile.adaptive) {
        const auto now = presentNow;
        if (!this->fixedDiagnosticWindowStarted)
            this->fixedDiagnosticWindowStarted = now;
        const double windowSeconds = std::chrono::duration<double>(
            now - *this->fixedDiagnosticWindowStarted
        ).count();
        if (windowSeconds >= 1.0) {
            const double realFps =
                static_cast<double>(this->fixedDiagnosticRealFrames) /
                    windowSeconds;
            const double observedOutputFps =
                static_cast<double>(this->fixedDiagnosticRealFrames +
                    this->fixedDiagnosticGeneratedFrames) / windowSeconds;
            std::cerr << "mako: present diagnostics: operation=fixed-plan"
                      << " context=" << this->diagnosticsContextId
                      << " base_fps=" << realFps
                      << " multiplier=" << this->profile.multiplier
                      << " generated_per_real="
                      << (this->profile.frame_generation_enabled
                            ? this->fixedFrameTimestamps.size() : 0)
                      << " observed_output_fps=" << observedOutputFps
                      << " generated_presented="
                      << this->fixedDiagnosticGeneratedFrames
                      << " generated_skipped="
                      << this->fixedDiagnosticSkippedFrames
                      << " configured_adaptive_target_fps="
                      << this->profile.target_fps
                      << " target_applies=0"
                      << " display_budget_hz="
                      << this->gamescopeRefreshHz.value_or(0)
                      << '\n';
            this->fixedDiagnosticWindowStarted = now;
            this->fixedDiagnosticRealFrames = 0;
            this->fixedDiagnosticGeneratedFrames = 0;
            this->fixedDiagnosticSkippedFrames = 0;
        }
        this->fixedDiagnosticRealFrames++;
    } else if (this->profile.adaptive) {
        this->fixedDiagnosticWindowStarted.reset();
        this->fixedDiagnosticRealFrames = 0;
        this->fixedDiagnosticGeneratedFrames = 0;
        this->fixedDiagnosticSkippedFrames = 0;
    }

    const auto presentNativeFrame = [&]() {
        const VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = lowerNextChain,
            .waitSemaphoreCount = static_cast<uint32_t>(semaphores.size()),
            .pWaitSemaphores = semaphores.data(),
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &imageIdx,
        };
        const auto result = vk.df().QueuePresentKHR(queue, &presentInfo);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");

        logSlowPresentOperation(
            "present-total", this->fidx, this->idx, presentStarted, result
        );
        this->fidx++;
        return result;
    };

    if (!this->applyPendingColorPipeline(vk))
        return presentNativeFrame();

    // Frame generation is live-disabled; hand the game's own image directly to
    // the driver without copies, model scheduling, fences or generated images.
    if (!this->profile.frame_generation_enabled ||
            !this->colorPipeline.generationSupported)
        return presentNativeFrame();

    if (this->backendRecoveryPending) {
        bool backendReady = false;
        try {
            backendReady = this->instance.get().contextReady(this->ctx.get());
        } catch (const std::exception& e) {
            std::cerr << "mako: backend recovery poll failed; native "
                         "presentation retained: " << e.what() << '\n';
        }
        if (!backendReady)
            return presentNativeFrame();

        this->backendRecoveryPending = false;
        this->generatedImageAdmission.reset();
        this->pipelineBusyRecovery.reset();
        if (this->profile.adaptive) {
            this->adaptiveScheduler.emplace(
                AdaptiveSchedulerConfig{
                    .targetFps = this->profile.target_fps,
                    .maximumMultiplier = this->profile.adaptive_max_multiplier,
                    .generatedFrameCapacity = this->destinationImages.size(),
                    .stableCadence = this->profile.adaptive_stable_cadence,
                    .recoveryPolicy = this->privateOrderedTransport
                        ? AdaptiveRecoveryPolicy::OrderedSdr
                        : AdaptiveRecoveryPolicy::ConservativeHdr,
                },
                &adaptiveSchedulerDiagnostics
            );
            this->adaptiveScheduler->beginStabilization(
                DiagnosticsClock::now(), "backend-recovery"
            );
        } else {
            this->configurationHistoryWarmupRemaining =
                AdaptiveScheduler::historyWarmupFrameCount();
        }

        std::cerr << "mako: backend work recovered; warming temporal "
                     "history before resuming frame generation\n";
    }

    const auto& swapchainImage = this->info.images.at(imageIdx);
    // Presentation diagnostics continue across live-off intervals, while the
    // backend's two-image temporal history does not. Select the source image
    // from the number of frames actually submitted to the backend so an odd
    // number of native-only frames cannot invert temporal history on re-enable.
    const auto& sourceImage = this->sourceImages.at(this->backendFrameIndex % 2);
    const bool historyWarmupActive =
        this->configurationHistoryWarmupRemaining > 0 ||
        (this->adaptiveScheduler && this->adaptiveScheduler->historyWarmupActive());
    const auto generatedFramePlan = this->profile.adaptive && !historyWarmupActive
        ? this->adaptiveScheduler->planFrame(
            presentNow, false
        )
        : AdaptiveFramePlan{};
    const size_t fixedGeneratedFrameCount = this->profile.adaptive
        ? 0
        : (gamescopeHdrTransport
            ? this->fixedRefreshBudget.plan(
                presentNow, this->gamescopeRefreshHz,
                this->fixedFrameTimestamps.size()
            )
            : this->fixedFrameTimestamps.size());
    if (!this->profile.adaptive &&
            fixedGeneratedFrameCount < this->fixedFrameTimestamps.size()) {
        this->fixedDiagnosticSkippedFrames +=
            this->fixedFrameTimestamps.size() - fixedGeneratedFrameCount;
    }
    const size_t generatedFrameCount = this->profile.adaptive
        ? generatedFramePlan.size() : fixedGeneratedFrameCount;
    const auto configuredAcquireTimeout = generatedImageAcquireTimeoutNs();
    bool renderFencePrepared = false;
    std::array<uint32_t, 3> preacquiredGeneratedImages{};
    size_t admittedGeneratedFrameCount = generatedFrameCount;

    const auto presentOriginalImage = [&](const VkSemaphore waitSemaphore,
            const void* presentNextChain) {
        const VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = presentNextChain,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &waitSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &imageIdx,
        };
        const auto originalPresentStarted = startPresentDiagnostic();
        const auto result = vk.df().QueuePresentKHR(queue, &presentInfo);
        logSlowPresentOperation(
            "present-original-image", this->fidx, this->idx,
            originalPresentStarted, result, std::nullopt, imageIdx
        );
        return result;
    };

    const auto ensureHistoryWarmup = [&]() {
        const size_t warmupFrames = AdaptiveScheduler::historyWarmupFrameCount();
        if (this->adaptiveScheduler) {
            this->adaptiveScheduler->ensureHistoryWarmup(
                warmupFrames, true
            );
        } else if (this->configurationHistoryWarmupRemaining == 0) {
            this->configurationHistoryWarmupRemaining = warmupFrames;
        }
    };

    // The Gamescope HDR bridge is native-first: never hold the application's
    // real present behind unfinished private work. Ordered SDR deliberately
    // retains the fork's synchronous FIFO/fence behavior. Applying this HDR
    // readiness bypass to SDR caused Deck-class GPUs to skip generated work on
    // ordinary one-frame overlap and made frame generation appear inactive.
    bool pipelineReady = true;
    if (gamescopeHdrTransport && this->renderFenceInFlight) {
        pipelineReady = this->renderFence->wait(vk, 0);
        if (pipelineReady)
            this->renderFenceInFlight = false;
    }
    if (gamescopeHdrTransport && pipelineReady) {
        try {
            pipelineReady = this->instance.get().contextReady(this->ctx.get());
        } catch (const std::exception& e) {
            std::cerr << "mako: backend readiness poll failed; native "
                         "presentation retained: " << e.what() << '\n';
            this->backendRecoveryPending = true;
            pipelineReady = false;
        }
    }
    if (gamescopeHdrTransport && !pipelineReady) {
        if (this->adaptiveScheduler)
            this->adaptiveScheduler->reportGeneratedFrameDelivery(
                generatedFrameCount, 0
            );
        if (!this->profile.adaptive)
            this->fixedDiagnosticSkippedFrames += generatedFrameCount;
        const auto busy = this->pipelineBusyRecovery.reportBusy(presentNow);
        if (busy.requestHistoryWarmup)
            ensureHistoryWarmup();
        if (presentDiagnosticsEnabled() && busy.diagnostic) {
            std::cerr << "mako: present diagnostics: "
                         "operation=pipeline-busy-bypass"
                      << " context=" << this->diagnosticsContextId
                      << " consecutive_frames=" << busy.consecutiveFrames
                      << " total_bypassed_frames="
                      << busy.totalBypassedFrames
                      << " duration_ms="
                      << std::chrono::duration<double, std::milli>(
                             busy.duration
                         ).count()
                      << " planned=" << generatedFrameCount
                      << " history_action="
                      << (busy.requestHistoryWarmup
                          ? "warmup-requested" : "preserved")
                      << " action=native-present\n";
        }
        return presentNativeFrame();
    }
    const auto pipelineRecovery =
        this->pipelineBusyRecovery.reportReady(presentNow);
    if (pipelineRecovery.resumed && pipelineRecovery.diagnostic) {
        if (presentDiagnosticsEnabled()) {
            std::cerr << "mako: present diagnostics: "
                         "operation=pipeline-busy-recovered"
                      << " context=" << this->diagnosticsContextId
                      << " bypassed_frames="
                      << pipelineRecovery.bypassedFrames
                      << " total_recoveries="
                      << pipelineRecovery.totalRecoveries
                      << " duration_ms="
                      << std::chrono::duration<double, std::milli>(
                             pipelineRecovery.duration
                         ).count()
                      << " history_warmup_requested="
                      << pipelineRecovery.historyWarmupRequested
                      << '\n';
        }
    }

    const auto prepareRenderFence = [&]() {
        if (renderFencePrepared)
            return;
        if (this->renderFenceInFlight) {
            const bool fenceSignaled = this->renderFence->wait(
                vk, 150ULL * 1000 * 1000
            );
            if (!fenceSignaled) {
                throw ls::error(
                    "timed out waiting for the previous render fence"
                );
            }
            this->renderFenceInFlight = false;
        }
        this->renderFence->reset(vk);
        renderFencePrepared = true;
    };

    // HDR bridge admission is nonblocking. Reserve every generated destination
    // before model work, then schedule only the contiguous admitted prefix.
    // A miss is compositor pressure, not a detach/history failure; the original
    // is presented natively and admission is retried next frame. This removes
    // the schedule-then-wait feedback loop measured at 90/120 Hz. Ordered SDR
    // intentionally does not enter this branch because FIFO owns its pacing.
    if (gamescopeHdrTransport && !historyWarmupActive &&
            generatedFrameCount > 0) {
        admittedGeneratedFrameCount = 0;
        bool logPressure = false;
        VkResult lastAcquireResult = VK_SUCCESS;
        for (size_t i = 0; i < generatedFrameCount; ++i) {
            uint32_t acquiredImage{};
            const auto acquireStarted = startPresentDiagnostic();
            lastAcquireResult = vk.df().AcquireNextImageKHR(
                vk.dev(), swapchain,
                generatedImageAcquireTimeout(true, configuredAcquireTimeout),
                this->passes.at(i).acquireSemaphore.handle(), VK_NULL_HANDLE,
                &acquiredImage
            );
            if (lastAcquireResult == VK_SUCCESS ||
                    lastAcquireResult == VK_SUBOPTIMAL_KHR) {
                preacquiredGeneratedImages.at(admittedGeneratedFrameCount++) =
                    acquiredImage;
                continue;
            }
            if (lastAcquireResult == VK_NOT_READY ||
                    lastAcquireResult == VK_TIMEOUT) {
                logPressure = this->generatedImageAdmission.reportUnavailable();
                if (logPressure) {
                    logSlowPresentOperation(
                        "acquire-generated-image", this->fidx, this->idx,
                        acquireStarted, lastAcquireResult, i, acquiredImage
                    );
                }
                break;
            }
            throw ls::vulkan_error(
                lastAcquireResult, "vkAcquireNextImageKHR() failed"
            );
        }

        if (admittedGeneratedFrameCount == generatedFrameCount) {
            const auto recovery = this->generatedImageAdmission.reportAvailable();
            if (recovery.resumed && presentDiagnosticsEnabled()) {
                std::cerr << "mako: present diagnostics: "
                             "operation=generated-admission-recovered"
                          << " context=" << this->diagnosticsContextId
                          << " missed_attempts=" << recovery.missedAttempts
                          << " bypassed_frames=" << recovery.bypassedFrames
                          << '\n';
            }
        } else {
            this->generatedImageAdmission.reportBypassedFrame();
            if (!this->profile.adaptive) {
                this->fixedDiagnosticSkippedFrames +=
                    generatedFrameCount - admittedGeneratedFrameCount;
            }
            if (logPressure && presentDiagnosticsEnabled()) {
                std::cerr << "mako: present diagnostics: "
                             "operation=generated-admission-pressure"
                          << " context=" << this->diagnosticsContextId
                          << " planned=" << generatedFrameCount
                          << " admitted=" << admittedGeneratedFrameCount
                          << " acquire_timeout_ns=0"
                          << " action=native-first\n";
            }
        }
    }

    const size_t scheduledGeneratedFrameCount = gamescopeHdrTransport
        ? admittedGeneratedFrameCount : generatedFrameCount;
    bool bypassGeneratedFrames =
        historyWarmupActive || scheduledGeneratedFrameCount == 0;
    if (historyWarmupActive && !this->profile.adaptive)
        this->fixedDiagnosticSkippedFrames += generatedFrameCount;
    std::array<float, 3> scheduledTimestamps{};
    for (size_t i = 0; i < scheduledGeneratedFrameCount; ++i) {
        scheduledTimestamps.at(i) = fixedFrameTimestamp(
            i, scheduledGeneratedFrameCount + 1
        );
    }
    const std::span<const float> timestamps{
        scheduledTimestamps.data(), scheduledGeneratedFrameCount
    };

    // schedule frame generation
    if (!bypassGeneratedFrames) {
        const auto scheduleStarted = startPresentDiagnostic();
        try {
            this->instance.get().scheduleFrames(this->ctx.get(), timestamps);
        } catch (const std::exception& e) {
            std::cerr << "mako: temporarily bypassing frame generation after "
                         "backend scheduling failure; native presentation retained: "
                      << e.what() << '\n';
            this->backendRecoveryPending = true;
            this->configurationHistoryWarmupRemaining = 0;
            if (this->adaptiveScheduler) {
                this->adaptiveScheduler->reportGeneratedFrameDelivery(
                    generatedFrameCount, 0
                );
                this->adaptiveScheduler->cancelHistoryWarmup();
            } else {
                this->fixedDiagnosticSkippedFrames +=
                    admittedGeneratedFrameCount;
            }
            if (!gamescopeHdrTransport ||
                    admittedGeneratedFrameCount == 0)
                return presentNativeFrame();
            return this->retireAcquiredImagesAndPresent(
                vk, queue, swapchain, lowerNextChain, imageIdx, semaphores,
                std::span<const uint32_t>(
                    preacquiredGeneratedImages.data(),
                    admittedGeneratedFrameCount
                ),
                swapchainImage
            );
        }
        this->backendFrameIndex++;
        logSlowPresentOperation("schedule-frames", this->fidx, this->idx, scheduleStarted);
    }

    // wait for completion of previous frame
    prepareRenderFence();

    // copy swapchain image into backend source image
    const auto& cmdbuf = *this->renderCommandBuffer;
    cmdbuf.begin(vk);

    cmdbuf.blitImage(vk,
        {
            barrierHelper(swapchainImage,
                VK_ACCESS_NONE,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            ),
            barrierHelper(sourceImage.handle(),
                VK_ACCESS_NONE,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            ),
        },
        { swapchainImage, sourceImage.handle() },
        sourceImage.getExtent(),
        {
            barrierHelper(swapchainImage,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_MEMORY_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            ),
        }
    );

    cmdbuf.end(vk);
    const auto sourceSubmitStarted = startPresentDiagnostic();
    cmdbuf.submit(vk,
        semaphores, VK_NULL_HANDLE, 0,
        {}, this->syncSemaphore->handle(), this->idx++
    );
    logSlowPresentOperation("submit-source-copy", this->fidx, this->idx, sourceSubmitStarted);

    if (bypassGeneratedFrames) {
        const uint64_t sourceTimelineValue = this->idx - 1;
        auto& fallbackPass = this->passes.front();
        auto& fallbackSemaphores = this->postCopySemaphores.at(
            this->idx % this->postCopySemaphores.size()
        );
        auto& fallbackSemaphore = fallbackSemaphores.second;

        auto& fallbackCommandBuffer = fallbackPass.commandBuffer;
        fallbackCommandBuffer.begin(vk);
        fallbackCommandBuffer.end(vk);
        fallbackCommandBuffer.submit(vk,
            {}, this->syncSemaphore->handle(), sourceTimelineValue,
            {fallbackSemaphore.handle()}, VK_NULL_HANDLE, 0,
            this->renderFence->handle()
        );
        this->renderFenceInFlight = true;

        try {
            this->instance.get().scheduleFrameHistory(this->ctx.get());
        } catch (const std::exception& e) {
            // The fallback copy is already queued and signals
            // fallbackSemaphore. Present the real image through that semaphore
            // and quarantine generation instead of returning an error to Steam.
            std::cerr << "mako: temporarily bypassing frame generation after "
                         "history scheduling failure; native presentation retained: "
                      << e.what() << '\n';
            this->backendRecoveryPending = true;
            this->configurationHistoryWarmupRemaining = 0;
            if (this->adaptiveScheduler)
                this->adaptiveScheduler->cancelHistoryWarmup();

            const auto fallbackResult = presentOriginalImage(
                fallbackSemaphore.handle(), lowerNextChain
            );
            if (fallbackResult != VK_SUCCESS &&
                    fallbackResult != VK_SUBOPTIMAL_KHR) {
                throw ls::vulkan_error(
                    fallbackResult, "vkQueuePresentKHR() failed"
                );
            }
            logSlowPresentOperation(
                "present-total", this->fidx, this->idx,
                presentStarted, fallbackResult
            );
            this->fidx++;
            return fallbackResult;
        }
        this->backendFrameIndex++;
        if (generatedFrameCount > admittedGeneratedFrameCount) {
            logPresentFallback(
                this->fidx, this->idx, 0,
                generatedFrameCount - admittedGeneratedFrameCount,
                sourceTimelineValue, "nonblocking-admission", "history-only"
            );
        }
        if (this->adaptiveScheduler && generatedFrameCount > 0) {
            this->adaptiveScheduler->reportGeneratedFrameDelivery(
                generatedFrameCount, admittedGeneratedFrameCount
            );
        }

        if (this->adaptiveScheduler &&
                this->adaptiveScheduler->historyWarmupActive()) {
            logHistoryWarmup(
                this->fidx, this->idx,
                this->adaptiveScheduler->historyWarmupRemaining(),
                this->adaptiveScheduler->historyWarmupIsRecovery(),
                std::nullopt
            );
            this->adaptiveScheduler->consumeHistoryWarmupFrame(
                DiagnosticsClock::now()
            );
        } else if (this->configurationHistoryWarmupRemaining > 0) {
            logHistoryWarmup(
                this->fidx, this->idx,
                this->configurationHistoryWarmupRemaining,
                false, std::nullopt
            );
            this->configurationHistoryWarmupRemaining--;
        }

        const auto res = presentOriginalImage(
            fallbackSemaphore.handle(), lowerNextChain
        );
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
            throw ls::vulkan_error(res, "vkQueuePresentKHR() failed");

        logSlowPresentOperation("present-total", this->fidx, this->idx, presentStarted, res);
        this->fidx++;
        return res;
    }

    for (size_t i = 0; i < scheduledGeneratedFrameCount; i++) {
        auto& pcs = this->postCopySemaphores.at(this->idx % this->postCopySemaphores.size());
        auto& destinationImage = this->destinationImages.at(i);
        auto& pass = this->passes.at(i);

        // acquire swapchain image
        uint32_t aqImageIdx{};
        VkResult res{};
        if (gamescopeHdrTransport) {
            aqImageIdx = preacquiredGeneratedImages.at(i);
            res = VK_SUCCESS;
        } else {
            const auto acquireStarted = startPresentDiagnostic();
            const uint64_t acquireTimeout = generatedImageAcquireTimeout(
                false, configuredAcquireTimeout
            );
            res = vk.df().AcquireNextImageKHR(vk.dev(), swapchain,
                acquireTimeout, pass.acquireSemaphore.handle(),
                VK_NULL_HANDLE,
                &aqImageIdx
            );
            logSlowPresentOperation(
                "acquire-generated-image", this->fidx, this->idx, acquireStarted, res,
                i, aqImageIdx
            );
        }
        if (configuredAcquireTimeout &&
                (res == VK_TIMEOUT || res == VK_NOT_READY)) {
            // The explicit legacy timeout is an anti-freeze ceiling. Backend
            // work is already scheduled on this non-Gamescope path, so drain
            // its final timeline value without reclassifying the miss as an
            // adaptive timing discontinuity.
            const size_t skippedFrames = scheduledGeneratedFrameCount - i;
            if (!this->profile.adaptive)
                this->fixedDiagnosticSkippedFrames += skippedFrames;
            const uint64_t finalGeneratedTimelineValue = this->idx + skippedFrames - 1;
            auto& fallbackSemaphore = pcs.second;
            if (this->adaptiveScheduler) {
                this->adaptiveScheduler->reportGeneratedFrameDelivery(
                    generatedFrameCount, i
                );
            }

            auto& fallbackCommandBuffer = pass.commandBuffer;
            fallbackCommandBuffer.begin(vk);
            fallbackCommandBuffer.end(vk);
            fallbackCommandBuffer.submit(vk,
                {}, this->syncSemaphore->handle(), finalGeneratedTimelineValue,
                { fallbackSemaphore.handle() }, VK_NULL_HANDLE, 0,
                this->renderFence->handle()
            );
            this->renderFenceInFlight = true;

            logPresentFallback(
                this->fidx, this->idx, i, skippedFrames, finalGeneratedTimelineValue,
                "initial-timeout", "scheduled"
            );
            this->idx += skippedFrames;

            res = presentOriginalImage(
                fallbackSemaphore.handle(),
                this->gamescopeManaged || i == 0 ? lowerNextChain : nullptr
            );
            if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
                throw ls::vulkan_error(res, "vkQueuePresentKHR() failed");

            logSlowPresentOperation("present-total", this->fidx, this->idx, presentStarted, res);
            if (presentDiagnosticsEnabled()) {
                std::cerr << "mako: present diagnostics: "
                             "operation=generated-delivery-miss"
                          << " context=" << this->diagnosticsContextId
                          << " planned=" << generatedFrameCount
                          << " on_time=" << i
                          << " deadline_ms="
                          << static_cast<double>(*configuredAcquireTimeout) /
                                1'000'000.0 << '\n';
            }
            this->fidx++;
            return res;
        }
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
            throw ls::vulkan_error(res, "vkAcquireNextImageKHR() failed");

        const auto& aquiredSwapchainImage = this->info.images.at(aqImageIdx);

        // copy backend destination image into swapchain image
        auto& cmdbuf = pass.commandBuffer;
        cmdbuf.begin(vk);

        cmdbuf.blitImage(vk,
            {
                barrierHelper(destinationImage.handle(),
                    VK_ACCESS_NONE,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                ),
                barrierHelper(aquiredSwapchainImage,
                    VK_ACCESS_NONE,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                ),
            },
            { destinationImage.handle(), aquiredSwapchainImage },
            destinationImage.getExtent(),
            {
                barrierHelper(aquiredSwapchainImage,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_MEMORY_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                ),
            }
        );

        std::vector<VkSemaphore> waitSemaphores{ pass.acquireSemaphore.handle() };
        if (i) { // non-first pass
            const auto& prevPCS = this->postCopySemaphores.at((this->idx - 1) % this->postCopySemaphores.size());
            waitSemaphores.push_back(prevPCS.second.handle());
        }

        const std::vector<VkSemaphore> signalSemaphores{
            pcs.first.handle(),
            pcs.second.handle()
        };

        cmdbuf.end(vk);
        const auto generatedSubmitStarted = startPresentDiagnostic();
        cmdbuf.submit(vk,
            waitSemaphores, this->syncSemaphore->handle(), this->idx,
            signalSemaphores, VK_NULL_HANDLE, 0,
            i == scheduledGeneratedFrameCount - 1
                ? this->renderFence->handle() : VK_NULL_HANDLE
        );
        if (i == scheduledGeneratedFrameCount - 1)
            this->renderFenceInFlight = true;
        logSlowPresentOperation(
            "submit-generated-copy", this->fidx, this->idx, generatedSubmitStarted,
            std::nullopt, i
        );

        // present swapchain image
        const VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = !this->gamescopeManaged && i == 0
                ? lowerNextChain : nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &pcs.first.handle(),
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &aqImageIdx,
        };
        const auto generatedPresentStarted = startPresentDiagnostic();
        res = vk.df().QueuePresentKHR(queue,
            &presentInfo);
        logSlowPresentOperation(
            "present-generated-image", this->fidx, this->idx, generatedPresentStarted, res,
            i, aqImageIdx
        );
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
            throw ls::vulkan_error(res, "vkQueuePresentKHR() failed");
        if (!this->profile.adaptive)
            this->fixedDiagnosticGeneratedFrames++;

        this->idx++;
    }

    // present original swapchain image
    auto& lastPCS = this->postCopySemaphores.at((this->idx - 1) % this->postCopySemaphores.size());
    auto res = presentOriginalImage(
        lastPCS.second.handle(), this->gamescopeManaged ? lowerNextChain : nullptr
    );
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
        throw ls::vulkan_error(res, "vkQueuePresentKHR() failed");

    if (this->adaptiveScheduler) {
        this->adaptiveScheduler->reportGeneratedFrameDelivery(
            generatedFrameCount, scheduledGeneratedFrameCount
        );
    }
    logSlowPresentOperation("present-total", this->fidx, this->idx, presentStarted, res);
    this->fidx++;
    return res;
}
