/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "present_diagnostics.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string_view>

#include <unistd.h>

namespace mako::layer::present_diagnostics {
namespace {
    std::atomic<uint32_t> nextContextSequence{1};
    thread_local uint64_t activeContextId{0};

    double elapsedMilliseconds(const Clock::time_point started) {
        return std::chrono::duration<double, std::milli>(
            Clock::now() - started
        ).count();
    }

    void logAdaptiveStabilization(const std::string_view reason,
            const Clock::duration duration) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-stabilization"
                  << " context=" << activeContextId
                  << " reason=" << reason
                  << " duration_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         duration
                     ).count()
                  << '\n';
    }

    void logAdaptiveRamp(const size_t previousLimit, const size_t newLimit,
            const double baseFps) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-ramp"
                  << " context=" << activeContextId
                  << " previous_generated_limit=" << previousLimit
                  << " generated_limit=" << newLimit
                  << " base_fps=" << baseFps << '\n';
    }

    void logAdaptiveRampResult(const bool accepted,
            const size_t previousLimit, const size_t testedLimit,
            const double previousBaseFps, const double currentBaseFps,
            const double previousOutputFps, const double currentOutputFps) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << (accepted ? "adaptive-ramp-accepted" : "adaptive-load-shed")
                  << " context=" << activeContextId
                  << " previous_generated_limit=" << previousLimit
                  << " tested_generated_limit=" << testedLimit
                  << " previous_base_fps=" << previousBaseFps
                  << " current_base_fps=" << currentBaseFps
                  << " previous_output_fps=" << previousOutputFps
                  << " current_output_fps=" << currentOutputFps << '\n';
    }

    void logAdaptiveBridge(const size_t previousLimit,
            const size_t testedLimit, const size_t bridgeLimit,
            const double previousBaseFps, const double currentBaseFps,
            const double previousOutputFps, const double currentOutputFps) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-bridge"
                  << " context=" << activeContextId
                  << " previous_generated_limit=" << previousLimit
                  << " tested_generated_limit=" << testedLimit
                  << " bridge_generated_limit=" << bridgeLimit
                  << " previous_base_fps=" << previousBaseFps
                  << " current_base_fps=" << currentBaseFps
                  << " previous_output_fps=" << previousOutputFps
                  << " current_output_fps=" << currentOutputFps << '\n';
    }

    void logAdaptiveBridgeResult(const bool accepted,
            const size_t baselineLimit, const size_t testedLimit,
            const double baselineBaseFps, const double currentBaseFps,
            const double baselineOutputFps, const double currentOutputFps) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << (accepted
                      ? "adaptive-bridge-accepted"
                      : "adaptive-bridge-rejected")
                  << " context=" << activeContextId
                  << " baseline_generated_limit=" << baselineLimit
                  << " tested_generated_limit=" << testedLimit
                  << " baseline_base_fps=" << baselineBaseFps
                  << " current_base_fps=" << currentBaseFps
                  << " baseline_output_fps=" << baselineOutputFps
                  << " current_output_fps=" << currentOutputFps << '\n';
    }

    void logAdaptiveProbeAborted(const std::string_view reason,
            const size_t testedLimit) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-probe-aborted"
                  << " context=" << activeContextId
                  << " reason=" << reason
                  << " tested_generated_limit=" << testedLimit << '\n';
    }

    void logAdaptiveRearm(const std::string_view operation,
            const std::string_view reason, const size_t failures,
            const size_t fallbackLimit, const Clock::duration cooldown,
            const double baselineBaseFps, const double currentBaseFps,
            const std::string_view decision) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation=" << operation
                  << " context=" << activeContextId
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

    void logAdaptiveFastCadenceBurst(const double baselineBaseFps,
            const double instantaneousBaseFps, const double thresholdFps,
            const size_t ignoredFrames, const size_t totalIgnoredFrames,
            const Clock::duration duration) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-fast-cadence-burst"
                  << " context=" << activeContextId
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

    void logAdaptiveFastCadenceBurstComplete(
            const size_t totalIgnoredFrames, const Clock::duration duration) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-fast-cadence-burst-complete"
                  << " context=" << activeContextId
                  << " total_ignored_frames=" << totalIgnoredFrames
                  << " duration_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         duration
                     ).count()
                  << '\n';
    }

    void logAdaptiveRampBackoff(const size_t testedLimit,
            const size_t failures, const double baselineBaseFps,
            const Clock::duration delay) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-ramp-backoff"
                  << " context=" << activeContextId
                  << " tested_generated_limit=" << testedLimit
                  << " consecutive_failures=" << failures
                  << " baseline_base_fps=" << baselineBaseFps
                  << " retry_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         delay
                     ).count()
                  << '\n';
    }

    void logAdaptiveRampEarlyRetry(const size_t testedLimit,
            const double failedBaseFps, const double currentBaseFps) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-ramp-early-retry"
                  << " context=" << activeContextId
                  << " tested_generated_limit=" << testedLimit
                  << " failed_base_fps=" << failedBaseFps
                  << " current_base_fps=" << currentBaseFps << '\n';
    }

    void logAdaptiveRecoveryResume(const size_t generationLimit,
            const Clock::duration higherProbeDelay,
            const std::string_view reason) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-recovery-resume-scheduled"
                  << " context=" << activeContextId
                  << " generated_limit=" << generationLimit
                  << " higher_probe_delay_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         higherProbeDelay
                     ).count()
                  << " reason=" << reason << '\n';
    }

    void logAdaptiveStableCadence(const std::string_view operation,
            const size_t generatedLimit, const double baselineBaseFps,
            const double currentBaseFps, const std::string_view reason) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation=" << operation
                  << " context=" << activeContextId
                  << " generated_limit=" << generatedLimit
                  << " baseline_base_fps=" << baselineBaseFps
                  << " current_base_fps=" << currentBaseFps
                  << " projected_output_fps="
                  << currentBaseFps * static_cast<double>(generatedLimit + 1);
        if (!reason.empty())
            std::cerr << " reason=" << reason;
        std::cerr << '\n';
    }

    void logAdaptiveRescueStart(const size_t generatedLimit,
            const double baselineBaseFps, const double currentBaseFps,
            const double projectedOutputFps, const std::string_view reason) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-rescue-start"
                  << " context=" << activeContextId
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

    void logAdaptiveRescueComplete(const size_t previousLimit,
            const size_t resumedLimit, const size_t requestedLimit,
            const size_t configuredLimit, const double baselineBaseFps,
            const double measuredBaseFps, const std::string_view decision) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation=adaptive-rescue-complete"
                  << " context=" << activeContextId
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

    void logAdaptiveDiscontinuityRecoveryStart(
            const size_t generationLimit, const double baselineBaseFps,
            const std::string_view reason,
            const Clock::duration maximumDuration) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-discontinuity-recovery-start"
                  << " context=" << activeContextId
                  << " generated_limit=" << generationLimit
                  << " baseline_base_fps=" << baselineBaseFps
                  << " reason=" << reason
                  << " maximum_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         maximumDuration
                     ).count()
                  << '\n';
    }

    void logAdaptiveDiscontinuityRecoveryComplete(
            const size_t generationLimit, const double baselineBaseFps,
            const double measuredBaseFps, const std::string_view decision) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-discontinuity-recovery-complete"
                  << " context=" << activeContextId
                  << " generated_limit=" << generationLimit
                  << " baseline_base_fps=" << baselineBaseFps
                  << " measured_base_fps=" << measuredBaseFps
                  << " decision=" << decision << '\n';
    }

    void logAdaptiveTwoXGameplayHitchRecovery(
            const size_t generationLimit, const double baselineBaseFps,
            const Clock::duration rawInterval) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-gameplay-hitch-recovery"
                  << " context=" << activeContextId
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

    void logAdaptiveSdrGameplayHitchBridge(
            const size_t generationLimit, const double baselineBaseFps,
            const Clock::duration rawInterval) {
        if (!enabled())
            return;

        std::cerr << "mako-render: present diagnostics: operation="
                  << "adaptive-sdr-gameplay-hitch-bridged"
                  << " context=" << activeContextId
                  << " generated_limit=" << generationLimit
                  << " baseline_base_fps=" << baselineBaseFps
                  << " raw_interval_ms="
                  << std::chrono::duration<double, std::milli>(
                         rawInterval
                     ).count()
                  << " action=retain-generated-frame"
                  << '\n';
    }

    void logAdaptiveCadenceRefresh(const std::string_view reason,
            const size_t retainedGenerationLimit, const size_t historyFrames) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-cadence-refresh"
                  << " context=" << activeContextId
                  << " reason=" << reason
                  << " retained_generated_limit=" << retainedGenerationLimit
                  << " history_warmup_frames=" << historyFrames
                  << " policy=sdr-fast-resume\n";
    }

    void logAdaptiveLoadShed(const size_t previousLimit,
            const size_t resumedLimit, const double baselineBaseFps,
            const double currentBaseFps, const std::string_view reason) {
        if (!enabled())
            return;

        std::cerr << "mako: present diagnostics: operation="
                  << "adaptive-load-shed"
                  << " context=" << activeContextId
                  << " previous_generated_limit=" << previousLimit
                  << " resumed_generated_limit=" << resumedLimit
                  << " baseline_base_fps=" << baselineBaseFps
                  << " current_base_fps=" << currentBaseFps
                  << " reason=" << reason
                  << " action=retain-generated-output\n";
    }

    class SchedulerDiagnostics final : public AdaptiveSchedulerDiagnostics {
    public:
        [[nodiscard]] bool enabled() const override {
            return present_diagnostics::enabled();
        }

        void plan(const AdaptivePlanDiagnostic& plan) override {
            if (!present_diagnostics::enabled())
                return;

            std::cerr << "mako: present diagnostics: operation=adaptive-plan"
                      << " context=" << activeContextId
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
            if (plan.recoveryGenerationLimit) {
                std::cerr << " recovery_generated_limit="
                          << plan.recoveryGenerationLimit;
            }
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

        void stabilization(const std::string_view reason,
                const Clock::duration duration) override {
            logAdaptiveStabilization(reason, duration);
        }

        void ramp(const size_t previousLimit, const size_t newLimit,
                const double baseFps) override {
            logAdaptiveRamp(previousLimit, newLimit, baseFps);
        }

        void rampResult(const bool accepted, const size_t previousLimit,
                const size_t testedLimit, const double previousBaseFps,
                const double currentBaseFps, const double previousOutputFps,
                const double currentOutputFps) override {
            logAdaptiveRampResult(
                accepted, previousLimit, testedLimit, previousBaseFps,
                currentBaseFps, previousOutputFps, currentOutputFps
            );
        }

        void bridge(const size_t previousLimit, const size_t testedLimit,
                const size_t bridgeLimit, const double previousBaseFps,
                const double currentBaseFps, const double previousOutputFps,
                const double currentOutputFps) override {
            logAdaptiveBridge(
                previousLimit, testedLimit, bridgeLimit, previousBaseFps,
                currentBaseFps, previousOutputFps, currentOutputFps
            );
        }

        void bridgeResult(const bool accepted, const size_t baselineLimit,
                const size_t testedLimit, const double baselineBaseFps,
                const double currentBaseFps, const double baselineOutputFps,
                const double currentOutputFps) override {
            logAdaptiveBridgeResult(
                accepted, baselineLimit, testedLimit, baselineBaseFps,
                currentBaseFps, baselineOutputFps, currentOutputFps
            );
        }

        void probeAborted(const std::string_view reason,
                const size_t testedLimit) override {
            logAdaptiveProbeAborted(reason, testedLimit);
        }

        void rearm(const std::string_view operation,
                const std::string_view reason, const size_t failures,
                const size_t fallbackLimit, const Clock::duration cooldown,
                const double baselineBaseFps, const double currentBaseFps,
                const std::string_view decision) override {
            logAdaptiveRearm(
                operation, reason, failures, fallbackLimit, cooldown,
                baselineBaseFps, currentBaseFps, decision
            );
        }

        void fastCadenceBurst(const double baselineBaseFps,
                const double instantaneousBaseFps, const double thresholdFps,
                const size_t ignoredFrames, const size_t totalIgnoredFrames,
                const Clock::duration duration) override {
            logAdaptiveFastCadenceBurst(
                baselineBaseFps, instantaneousBaseFps, thresholdFps,
                ignoredFrames, totalIgnoredFrames, duration
            );
        }

        void fastCadenceBurstComplete(const size_t totalIgnoredFrames,
                const Clock::duration duration) override {
            logAdaptiveFastCadenceBurstComplete(totalIgnoredFrames, duration);
        }

        void rampBackoff(const size_t testedLimit, const size_t failures,
                const double baselineBaseFps,
                const Clock::duration delay) override {
            logAdaptiveRampBackoff(
                testedLimit, failures, baselineBaseFps, delay
            );
        }

        void rampEarlyRetry(const size_t testedLimit,
                const double failedBaseFps,
                const double currentBaseFps) override {
            logAdaptiveRampEarlyRetry(
                testedLimit, failedBaseFps, currentBaseFps
            );
        }

        void recoveryResume(const size_t generationLimit,
                const Clock::duration higherProbeDelay,
                const std::string_view reason) override {
            logAdaptiveRecoveryResume(
                generationLimit, higherProbeDelay, reason
            );
        }

        void stableCadence(const std::string_view operation,
                const size_t generatedLimit, const double baselineBaseFps,
                const double currentBaseFps,
                const std::string_view reason) override {
            logAdaptiveStableCadence(
                operation, generatedLimit, baselineBaseFps,
                currentBaseFps, reason
            );
        }

        void rescueStart(const size_t generatedLimit,
                const double baselineBaseFps, const double currentBaseFps,
                const double projectedOutputFps,
                const std::string_view reason) override {
            logAdaptiveRescueStart(
                generatedLimit, baselineBaseFps, currentBaseFps,
                projectedOutputFps, reason
            );
        }

        void rescueComplete(const size_t previousLimit,
                const size_t resumedLimit, const size_t requestedLimit,
                const size_t configuredLimit, const double baselineBaseFps,
                const double measuredBaseFps,
                const std::string_view decision) override {
            logAdaptiveRescueComplete(
                previousLimit, resumedLimit, requestedLimit, configuredLimit,
                baselineBaseFps, measuredBaseFps, decision
            );
        }

        void discontinuityRecoveryStart(const size_t generationLimit,
                const double baselineBaseFps, const std::string_view reason,
                const Clock::duration maximumDuration) override {
            logAdaptiveDiscontinuityRecoveryStart(
                generationLimit, baselineBaseFps, reason, maximumDuration
            );
        }

        void discontinuityRecoveryComplete(const size_t generationLimit,
                const double baselineBaseFps, const double measuredBaseFps,
                const std::string_view decision) override {
            logAdaptiveDiscontinuityRecoveryComplete(
                generationLimit, baselineBaseFps, measuredBaseFps, decision
            );
        }

        void twoXGameplayHitchRecovery(const size_t generationLimit,
                const double baselineBaseFps,
                const Clock::duration rawInterval) override {
            logAdaptiveTwoXGameplayHitchRecovery(
                generationLimit, baselineBaseFps, rawInterval
            );
        }

        void sdrGameplayHitchBridge(const size_t generationLimit,
                const double baselineBaseFps,
                const Clock::duration rawInterval) override {
            logAdaptiveSdrGameplayHitchBridge(
                generationLimit, baselineBaseFps, rawInterval
            );
        }

        void cadenceRefresh(const std::string_view reason,
                const size_t retainedGenerationLimit,
                const size_t historyFrames) override {
            logAdaptiveCadenceRefresh(
                reason, retainedGenerationLimit, historyFrames
            );
        }

        void loadShed(const size_t previousLimit, const size_t resumedLimit,
                const double baselineBaseFps, const double currentBaseFps,
                const std::string_view reason) override {
            logAdaptiveLoadShed(
                previousLimit, resumedLimit, baselineBaseFps,
                currentBaseFps, reason
            );
        }
    };
}

uint64_t allocateContextId() {
    const auto processId = static_cast<uint64_t>(
        static_cast<uint32_t>(::getpid())
    );
    const auto sequence = static_cast<uint64_t>(
        nextContextSequence.fetch_add(1, std::memory_order_relaxed)
    );
    return (processId << 32) | sequence;
}

bool enabled() {
    static const bool value = [] {
        const char* environment = std::getenv("MAKO_PRESENT_DIAGNOSTICS");
        return environment && std::string_view(environment) != "0";
    }();
    return value;
}

double thresholdMilliseconds() {
    static const double threshold = [] {
        constexpr double defaultThresholdMs = 20.0;
        const char* value = std::getenv(
            "MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS"
        );
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

Clock::time_point start() {
    if (!enabled())
        return {};
    return Clock::now();
}

ContextScope::ContextScope(const uint64_t contextId) :
        previousContextId(activeContextId) {
    activeContextId = contextId;
}

ContextScope::~ContextScope() {
    activeContextId = this->previousContextId;
}

void logSlowOperation(const std::string_view operation,
        const size_t frameIndex, const size_t sequenceIndex,
        const Clock::time_point started,
        const std::optional<VkResult> result,
        const std::optional<size_t> passIndex,
        const std::optional<uint32_t> imageIndex) {
    if (!enabled())
        return;

    const double durationMs = elapsedMilliseconds(started);
    const bool resultFailed = result &&
        *result != VK_SUCCESS && *result != VK_SUBOPTIMAL_KHR;
    if (durationMs < thresholdMilliseconds() && !resultFailed)
        return;

    std::ostringstream message;
    message << "mako: present diagnostics: operation=" << operation
            << " context=" << activeContextId
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

void logPresentFallback(const size_t frameIndex,
        const size_t sequenceIndex, const size_t passIndex,
        const size_t skippedFrames, const uint64_t timelineValue,
        const std::string_view acquireMode,
        const std::string_view backendWork) {
    if (!enabled())
        return;

    std::cerr << "mako: present diagnostics: operation=skip-generated-frames"
              << " context=" << activeContextId
              << " frame=" << frameIndex
              << " sequence=" << sequenceIndex
              << " pass=" << passIndex
              << " skipped=" << skippedFrames
              << " wait_timeline=" << timelineValue
              << " acquire_mode=" << acquireMode
              << " backend_work=" << backendWork
              << '\n';
}

void logHistoryWarmup(const size_t frameIndex,
        const size_t sequenceIndex, const size_t remainingFrames,
        const bool recovery,
        const std::optional<uint32_t> acquiredImage) {
    if (!enabled())
        return;

    std::cerr << "mako: present diagnostics: operation=history-warmup"
              << " context=" << activeContextId
              << " frame=" << frameIndex
              << " sequence=" << sequenceIndex
              << " reason=" << (recovery ? "recovery" : "startup")
              << " remaining_frames=" << remainingFrames;
    if (acquiredImage)
        std::cerr << " released_image=" << *acquiredImage;
    std::cerr << '\n';
}

AdaptiveSchedulerDiagnostics& adaptiveScheduler() {
    static SchedulerDiagnostics diagnostics;
    return diagnostics;
}

}
