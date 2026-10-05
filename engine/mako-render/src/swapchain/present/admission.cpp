/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "swapchain/swapchain.hpp"
#include "swapchain/present/internal.hpp"
#include "swapchain/retirement.hpp"
#include "adaptive_scheduler.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/vulkan/command_buffer.hpp"
#include "mako-common/vulkan/image.hpp"
#include "mako-common/vulkan/semaphore.hpp"
#include "mako-common/vulkan/vulkan.hpp"
#include "present_diagnostics.hpp"
#include "pnext_chain.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

using namespace mako;
using namespace mako::layer;

using namespace mako::layer::present_detail;

namespace {
    constexpr auto renderFenceWaitBudget = std::chrono::milliseconds(150);
    constexpr uint64_t renderFenceWaitBudgetNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            renderFenceWaitBudget
        ).count()
    );
}

bool Swapchain::recoverBackendIfReady(const vk::Vulkan& vk) {
    if (!this->recoveryState.backendPending)
        return true;

    bool backendReady = false;
    try {
        backendReady = this->instance &&
            this->instance->contextReady(this->ctx.get());
    } catch (const std::exception& error) {
        std::cerr << "MAKO Renderer: backend recovery poll failed; native "
                     "presentation retained: " << error.what() << '\n';
    }
    if (!backendReady)
        return false;

    // A fence-budget miss leaves the previous application-device submission
    // in flight. Backend readiness alone does not make that fence reusable,
    // so retain nonblocking native presentation until both sides are idle.
    if (this->frameState.renderFenceInFlight) {
        try {
            if (!this->renderFence->wait(vk, 0))
                return false;
            this->frameState.renderFenceInFlight = false;
        } catch (const std::exception& error) {
            std::cerr << "MAKO Renderer: render fence recovery poll failed; "
                         "native presentation retained: "
                      << error.what() << '\n';
            return false;
        }
    }

    this->recoveryState.backendPending = false;
    this->recoveryState.generatedImageAdmission.reset();
    this->recoveryState.pipelineBusyRecovery.reset();
    if (!this->resetGenerationScheduler(
            DiagnosticsClock::now(), "backend-recovery")) {
        this->recoveryState.historyWarmupRemaining =
            AdaptiveScheduler::historyWarmupFrameCount();
    }

    std::cerr << "MAKO Renderer: backend work recovered; warming temporal "
                 "history before resuming frame generation\n";
    return true;
}

void Swapchain::ensureHistoryWarmup(const bool restart) {
    const size_t warmupFrames = AdaptiveScheduler::historyWarmupFrameCount();
    if (this->adaptiveScheduler) {
        if (restart)
            this->adaptiveScheduler->beginHistoryWarmup(warmupFrames, true);
        else
            this->adaptiveScheduler->ensureHistoryWarmup(warmupFrames, true);
    } else {
        this->recoveryState.historyWarmupRemaining = historyWarmupFramesAfterRequest(
            this->recoveryState.historyWarmupRemaining, warmupFrames, restart);
    }
}

Swapchain::PresentationFramePlan Swapchain::prepareFramePlan(
        const DiagnosticsClock::time_point presentNow,
        const bool orderedAcquireRecoveryProbe) {
    PresentationFramePlan plan;
    plan.orderedAcquireRecoveryProbe = orderedAcquireRecoveryProbe;
    plan.historyWarmupActive =
        this->recoveryState.historyWarmupRemaining > 0 ||
        (this->adaptiveScheduler &&
            this->adaptiveScheduler->historyWarmupActive());
    const bool schedulerEnabled = this->adaptiveScheduler.has_value();
    const auto adaptivePlan = schedulerEnabled &&
            !plan.historyWarmupActive
        ? this->adaptiveScheduler->planFrame(
            presentNow, orderedAcquireRecoveryProbe,
            this->gamescopePresentationFeedback.variableRefreshRequested()
                ? this->smoothCadencePacerHandoff.activeGenerationLimit()
                : std::nullopt
        )
        : AdaptiveFramePlan{};
    const bool fixedSmoothCadenceFullMultiplier = !schedulerEnabled &&
        fixedSmoothCadenceFifoEligible(
            this->profile,
            this->privateOrderedTransport,
            orderedAcquireRecoveryProbe ||
                this->recoveryState.orderedAcquireRecovery.active(),
            this->gamescopeRefreshHz
        ) && this->configuredFixedGeneratedFrames + 1 ==
            this->profile.multiplier;
    const size_t fixedGeneratedFrameCount = schedulerEnabled
        ? 0
        : this->fixedRefreshBudget.plan(
            presentNow, this->gamescopeRefreshHz,
            this->configuredFixedGeneratedFrames,
            fixedSmoothCadenceFullMultiplier
        );
    // Fixed is a user-selected workload. Only explicit menu/lifecycle
    // transitions and direct transport failures may interrupt it; ordinary
    // gameplay cadence is never used to infer a recovery episode.
    if (!schedulerEnabled &&
            fixedGeneratedFrameCount <
                this->configuredFixedGeneratedFrames) {
        this->diagnosticsState.fixedSkippedFrames +=
            this->configuredFixedGeneratedFrames -
                fixedGeneratedFrameCount;
    }
    plan.requestedGeneratedFrames = schedulerEnabled
        ? adaptivePlan
        : GeneratedFramePlan::evenlySpaced(
            fixedGeneratedFrameCount
        );
    if (orderedAcquireProbeEligible(orderedAcquireRecoveryProbe,
            plan.historyWarmupActive, plan.requestedGeneratedFrames.size())) {
        // A successful native drain proves only that one image can traverse
        // the ordered FIFO again. Do not turn that narrow observation into a
        // full normal plan before the recovery state has seen it complete.
        plan.requestedGeneratedFrames = GeneratedFramePlan::evenlySpaced(1);
    }
    plan.admittedGeneratedFrameCount = plan.requestedGeneratedFrames.size();
    plan.configuredAcquireTimeout = generatedImageAcquireTimeoutNs();
    return plan;
}

void Swapchain::reportAdaptiveDelivery(
        const PresentationFramePlan& plan,
        const size_t acceptedForPresentation) {
    if (!this->adaptiveScheduler || plan.requestedGeneratedFrames.empty())
        return;
    // Ordered acquire recovery owns the cadence boundary until its one-frame
    // probe completes. Its synthetic delivery must not advance an unrelated
    // Adaptive ramp or stable-cadence evaluation.
    if (this->privateOrderedTransport &&
            (plan.orderedAcquireRecoveryProbe ||
             this->recoveryState.orderedAcquireRecovery.active())) {
        return;
    }
    this->adaptiveScheduler->reportGeneratedFrameDelivery({
        .requested = plan.requestedGeneratedFrames.size(),
        .acceptedForPresentation = acceptedForPresentation,
    });
}

bool Swapchain::generationPipelineReady(const vk::Vulkan& vk,
        const bool gamescopeHdrTransport,
        const PresentationFramePlan& plan,
        const DiagnosticsClock::time_point presentNow) {
    bool pipelineReady = true;
    if (gamescopeHdrTransport && this->frameState.renderFenceInFlight) {
        pipelineReady = this->renderFence->wait(vk, 0);
        if (pipelineReady)
            this->frameState.renderFenceInFlight = false;
    }
    if (gamescopeHdrTransport && pipelineReady) {
        try {
            pipelineReady = this->instance &&
                this->instance->contextReady(this->ctx.get());
        } catch (const std::exception& error) {
            std::cerr << "MAKO Renderer: backend readiness poll failed; native "
                         "presentation retained: " << error.what() << '\n';
            this->recoveryState.backendPending = true;
            pipelineReady = false;
        }
    }
    if (gamescopeHdrTransport && !pipelineReady) {
        this->reportAdaptiveDelivery(plan, 0);
        if (!this->adaptiveScheduler)
            this->diagnosticsState.fixedSkippedFrames +=
                plan.requestedGeneratedFrames.size();
        const auto busy = this->recoveryState.pipelineBusyRecovery.reportBusy(presentNow);
        if (busy.requestHistoryWarmup) {
            this->ensureHistoryWarmup();
        }
        if (presentDiagnosticsEnabled() && busy.diagnostic) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=pipeline-busy-bypass"
                      << " context=" << this->diagnosticsState.contextId
                      << " consecutive_frames=" << busy.consecutiveFrames
                      << " total_bypassed_frames="
                      << busy.totalBypassedFrames
                      << " duration_ms="
                      << std::chrono::duration<double, std::milli>(
                             busy.duration
                         ).count()
                      << " planned=" << plan.requestedGeneratedFrames.size()
                      << " history_action="
                      << (busy.requestHistoryWarmup
                          ? "warmup-requested" : "preserved")
                      << " action=native-present\n";
        }
        return false;
    }

    const auto recovery = this->recoveryState.pipelineBusyRecovery.reportReady(presentNow);
    if (recovery.resumed && recovery.diagnostic &&
            presentDiagnosticsEnabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=pipeline-busy-recovered"
                  << " context=" << this->diagnosticsState.contextId
                  << " bypassed_frames=" << recovery.bypassedFrames
                  << " total_recoveries=" << recovery.totalRecoveries
                  << " duration_ms="
                  << std::chrono::duration<double, std::milli>(
                         recovery.duration
                     ).count()
                  << " history_warmup_requested="
                  << recovery.historyWarmupRequested
                  << '\n';
    }
    return true;
}

bool Swapchain::prepareRenderFence(const vk::Vulkan& vk) {
    if (this->frameState.renderFenceInFlight) {
        const bool fenceSignaled = this->renderFence->wait(
            vk, renderFenceWaitBudgetNs
        );
        if (!fenceSignaled)
            return false;
        this->frameState.renderFenceInFlight = false;
    }
    this->renderFence->reset(vk);
    return true;
}

void Swapchain::handleRenderFenceBudgetMiss(
        const PresentationFramePlan& plan) {
    std::cerr << "MAKO Renderer: previous render work missed the "
              << renderFenceWaitBudget.count() << " ms "
                 "fence budget; bypassing frame generation for this present; "
                 "native presentation retained\n";
    if (this->adaptiveScheduler) {
        this->reportAdaptiveDelivery(plan, 0);
        this->adaptiveScheduler->cancelHistoryWarmup();
    } else if (!plan.historyWarmupActive) {
        // Fixed history warmup was already recorded as skipped when the plan
        // was built, so count only a delivery that was still intended.
        this->diagnosticsState.fixedSkippedFrames +=
            plan.requestedGeneratedFrames.size();
    }
    this->recoveryState.backendPending = true;
    this->recoveryState.historyWarmupRemaining = 0;
    if (presentDiagnosticsEnabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=render-fence-budget-missed"
                  << " context=" << this->diagnosticsState.contextId
                  << " planned=" << plan.requestedGeneratedFrames.size()
                  << " action=native-present\n";
    }
}

void Swapchain::handleGeneratedImageAdmissionPressure(
        const PresentationFramePlan& plan,
        const size_t admittedGeneratedFrames,
        const bool logPressure,
        const bool retainPartialAdmissionCapacity,
        const bool classifyAdaptiveLoadFailure,
        const uint64_t acquireTimeoutNanoseconds,
        const char* const action) {
    this->recoveryState.generatedImageAdmission.reportBypassedFrame();
    const bool adaptiveOrderedAdmissionMiss =
        classifyAdaptiveLoadFailure && this->adaptiveScheduler &&
        adaptiveOrderedDeliveryMissRequiresFallback(
            this->profile.adaptive,
            this->privateOrderedTransport,
            plan.requestedGeneratedFrames.size(),
            admittedGeneratedFrames
        );
    const bool higherMultiplierEvaluationActive =
        adaptiveOrderedAdmissionMiss &&
        this->adaptiveScheduler->snapshot().rampEvaluationActive;
    if (adaptiveOrderedAdmissionMiss) {
        const auto observedAt = DiagnosticsClock::now();
        if (!this->adaptiveScheduler->rejectActiveRampForTransportMiss(
                observedAt)) {
            this->adaptiveScheduler->beginTransportRecovery(
                observedAt, true
            );
        }
    }
    const auto constrainedAdaptiveOrderedWsiCapacity =
        retainPartialAdmissionCapacity
            ? adaptiveOrderedWsiLimitAfterPartialAdmission(
                this->profile.adaptive,
                this->privateOrderedTransport,
                higherMultiplierEvaluationActive,
                plan.requestedGeneratedFrames.size(),
                admittedGeneratedFrames,
                this->adaptiveOrderedWsiGeneratedCapacityLimit
            )
            : std::nullopt;
    if (constrainedAdaptiveOrderedWsiCapacity) {
        const size_t previousGeneratedCapacity =
            this->adaptiveOrderedWsiGeneratedCapacityLimit.value_or(
                this->destinationImages.size()
            );
        this->adaptiveOrderedWsiGeneratedCapacityLimit =
            *constrainedAdaptiveOrderedWsiCapacity;
        if (presentDiagnosticsEnabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=adaptive-wsi-headroom-limit"
                      << " context=" << this->diagnosticsState.contextId
                      << " requested_min_images="
                      << this->info.requestedMinImageCount
                      << " images=" << this->info.images.size()
                      << " requested_generated="
                      << plan.requestedGeneratedFrames.size()
                      << " admitted_generated="
                      << admittedGeneratedFrames
                      << " previous_generated_capacity="
                      << previousGeneratedCapacity
                      << " effective_generated_capacity="
                      << *this->adaptiveOrderedWsiGeneratedCapacityLimit
                      << " evidence=higher-multiplier-partial-admission"
                      << " action=retain-proven-generated-capacity\n";
        }
        this->recoveryState.generatedImageAdmission.reset();
        static_cast<void>(this->resetGenerationScheduler(
            DiagnosticsClock::now(), "ordered-wsi-partial-admission"
        ));
    }
    if (!this->adaptiveScheduler) {
        this->diagnosticsState.fixedSkippedFrames +=
            plan.requestedGeneratedFrames.size() -
                admittedGeneratedFrames;
    }
    if (logPressure && presentDiagnosticsEnabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=generated-admission-pressure"
                  << " context=" << this->diagnosticsState.contextId
                  << " planned=" << plan.requestedGeneratedFrames.size()
                  << " admitted=" << admittedGeneratedFrames
                  << " acquire_timeout_ns=" << acquireTimeoutNanoseconds
                  << " action=" << action << '\n';
    }
}

void Swapchain::reportGeneratedImageAdmissionAvailable(
        const size_t requiredStableBatches) {
    const auto recovery =
        this->recoveryState.generatedImageAdmission.reportAvailable(
            requiredStableBatches
        );
    if (recovery.resumed && presentDiagnosticsEnabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=generated-admission-recovered"
                  << " context=" << this->diagnosticsState.contextId
                  << " missed_attempts=" << recovery.missedAttempts
                  << " bypassed_frames=" << recovery.bypassedFrames
                  << " stable_batches=" << recovery.stableBatches
                  << " required_stable_batches="
                  << recovery.requiredStableBatches
                  << '\n';
    }
}

void Swapchain::preacquireGeneratedImages(
        const PresentInvocation& invocation,
        PresentationFramePlan& plan, const bool trackNonblockingAdmission,
        const bool classifyAdaptiveLoadFailure,
        const uint64_t acquireTimeout,
        const bool reportAvailableOnFullAdmission) {
    if (plan.requestedGeneratedFrames.empty() || plan.historyWarmupActive)
        return;

    plan.generatedImagesPreacquired = true;
    plan.admittedGeneratedFrameCount = 0;
    bool logPressure = false;
    VkResult lastAcquireResult = VK_SUCCESS;
    for (size_t i = 0; i < plan.requestedGeneratedFrames.size(); ++i) {
        uint32_t acquiredImage{};
        const auto acquireStarted = startPresentDiagnostic();
        lastAcquireResult = invocation.vk.df().AcquireNextImageKHR(
            invocation.vk.dev(), invocation.swapchain,
            acquireTimeout,
            this->passes.at(i).acquireSemaphore.handle(), VK_NULL_HANDLE,
            &acquiredImage
        );
        plan.preacquireDuration += finishPresentDiagnostic(acquireStarted);
        if (lastAcquireResult == VK_SUCCESS ||
                lastAcquireResult == VK_SUBOPTIMAL_KHR) {
            plan.preacquiredGeneratedImages.at(
                plan.admittedGeneratedFrameCount++
            ) = acquiredImage;
            continue;
        }
        if (lastAcquireResult == VK_NOT_READY ||
                lastAcquireResult == VK_TIMEOUT) {
            logPressure = trackNonblockingAdmission &&
                this->recoveryState.generatedImageAdmission.reportUnavailable();
            if (logPressure) {
                logSlowPresentOperation(
                    "acquire-generated-image",
                    this->frameState.realFrameIndex,
                    this->frameState.sequenceIndex,
                    acquireStarted, lastAcquireResult, i, acquiredImage
                );
            }
            break;
        }
        throw ls::vulkan_error(
            lastAcquireResult, "vkAcquireNextImageKHR() failed"
        );
    }

    if (plan.admittedGeneratedFrameCount ==
            plan.requestedGeneratedFrames.size()) {
        if (trackNonblockingAdmission && reportAvailableOnFullAdmission)
            this->reportGeneratedImageAdmissionAvailable();
        return;
    }

    if (trackNonblockingAdmission) {
        this->handleGeneratedImageAdmissionPressure(
            plan, plan.admittedGeneratedFrameCount, logPressure, true,
            classifyAdaptiveLoadFailure, 0,
            classifyAdaptiveLoadFailure
                ? "native-first" : "adaptive-pressure-retry"
        );
    }
}
