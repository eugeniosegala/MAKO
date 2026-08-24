/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "swapchain.hpp"
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

namespace {
    using DiagnosticsClock = present_diagnostics::Clock;
    using DiagnosticsContextScope = present_diagnostics::ContextScope;
    using present_diagnostics::logHistoryWarmup;
    using present_diagnostics::logPresentFallback;

    constexpr auto renderFenceWaitBudget = std::chrono::milliseconds(150);
    constexpr uint64_t renderFenceWaitBudgetNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            renderFenceWaitBudget
        ).count()
    );

    bool presentDiagnosticsEnabled() {
        return present_diagnostics::enabled();
    }

    DiagnosticsClock::time_point startPresentDiagnostic() {
        return present_diagnostics::start();
    }

    DiagnosticsClock::duration finishPresentDiagnostic(
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

    void logSlowPresentBreakdown(const uint64_t contextId,
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
        if (totalMs < present_diagnostics::thresholdMilliseconds())
            return;

        const auto attributedDuration =
            phases.renderFence + phases.schedule + phases.sourceCopy +
            phases.acquire + phases.generatedSubmit +
            phases.generatedPresent + phases.originalPresent;
        const double unattributedMs = std::max(
            0.0, totalMs - milliseconds(attributedDuration)
        );
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

    void logSlowPresentOperation(const std::string_view operation,
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

    VkImageMemoryBarrier barrierHelper(const VkImage handle,
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

VkResult Swapchain::retireAcquiredImagesAndPresent(const vk::Vulkan& vk,
        const VkQueue queue, const VkSwapchainKHR swapchain,
        const void* nextChain, const uint32_t originalImageIndex,
        const std::span<const VkSemaphore> applicationWaitSemaphores,
        const std::span<const uint32_t> acquiredImageIndices,
        const VkImage originalImage) {
    if (acquiredImageIndices.empty())
        throw ls::error("attempted to retire an empty acquired-image batch");

    this->renderFence->reset(vk);
    const size_t semaphoreBase = (
        this->frameState.sequenceIndex + acquiredImageIndices.size() + 1
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

        std::array<vk::Barrier, 2> preBarriers{};
        size_t preBarrierCount = 0;
        if (first) {
            preBarriers.at(preBarrierCount++) = barrierHelper(
                originalImage,
                VK_ACCESS_NONE,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );
        }
        preBarriers.at(preBarrierCount++) = barrierHelper(
            acquiredImage,
            VK_ACCESS_NONE,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );

        std::array<vk::Barrier, 2> postBarriers{};
        size_t postBarrierCount = 0;
        postBarriers.at(postBarrierCount++) = barrierHelper(
                acquiredImage,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_MEMORY_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        );
        if (last) {
            postBarriers.at(postBarrierCount++) = barrierHelper(
                originalImage,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_MEMORY_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            );
        }

        auto& commandBuffer = pass.commandBuffer;
        commandBuffer.begin(vk);
        commandBuffer.blitImage(
            vk, std::span{preBarriers}.first(preBarrierCount),
            {originalImage, acquiredImage}, this->info.extent,
            std::span{postBarriers}.first(postBarrierCount)
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
            std::array{pcs.first.handle(), pcs.second.handle()},
            VK_NULL_HANDLE, 0,
            last ? this->renderFence->handle() : VK_NULL_HANDLE
        );
    }
    this->frameState.renderFenceInFlight = true;

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
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=retire-acquired-images"
                  << " context=" << this->diagnosticsState.contextId
                  << " images=" << acquiredImageIndices.size()
                  << " reason=backend-schedule-failure\n";
    }
    this->frameState.realFrameIndex++;
    return result;
}

void Swapchain::recordPresentCadence(const DiagnosticsClock::time_point presentNow) {
    if (this->frameState.lastPresentStarted) {
        const auto interval = presentNow - *this->frameState.lastPresentStarted;
        if (interval > DiagnosticsClock::duration::zero() &&
                interval < std::chrono::seconds(1))
            this->frameState.recentRealInterval = interval;
    }
    this->frameState.lastPresentStarted = presentNow;

    if (presentDiagnosticsEnabled() && !this->adaptiveScheduler) {
        if (!this->diagnosticsState.fixedWindowStarted)
            this->diagnosticsState.fixedWindowStarted = presentNow;
        const double windowSeconds = std::chrono::duration<double>(
            presentNow - *this->diagnosticsState.fixedWindowStarted
        ).count();
        if (windowSeconds >= 1.0) {
            const double realFps =
                static_cast<double>(this->diagnosticsState.fixedRealFrames) /
                    windowSeconds;
            const double observedOutputFps =
                static_cast<double>(this->diagnosticsState.fixedRealFrames +
                    this->diagnosticsState.fixedGeneratedFrames) / windowSeconds;
            // Keep opt-in diagnostics from adding field-by-field flush points
            // to the presentation thread's once-per-second pacing report.
            std::ostringstream message;
            message << "MAKO Renderer: present diagnostics: operation=fixed-plan"
                    << " context=" << this->diagnosticsState.contextId
                    << " base_fps=" << realFps
                    << " multiplier=" << this->profile.multiplier
                    << " generated_per_real="
                    << (effectiveFrameGenerationEnabled(
                              this->profile, this->gamescopeRefreshHz
                          )
                          ? this->configuredFixedGeneratedFrames : 0)
                    << " observed_output_fps=" << observedOutputFps
                    << " generated_presented="
                    << this->diagnosticsState.fixedGeneratedFrames
                    << " generated_skipped="
                    << this->diagnosticsState.fixedSkippedFrames
                    << " configured_adaptive_target_fps="
                    << this->profile.target_fps
                    << " target_applies=0"
                    << " display_budget_hz="
                    << this->gamescopeRefreshHz.value_or(0)
                    << '\n';
            std::cerr << message.str();
            this->diagnosticsState.fixedWindowStarted = presentNow;
            this->diagnosticsState.fixedRealFrames = 0;
            this->diagnosticsState.fixedGeneratedFrames = 0;
            this->diagnosticsState.fixedSkippedFrames = 0;
        }
        this->diagnosticsState.fixedRealFrames++;
    } else if (this->adaptiveScheduler) {
        this->diagnosticsState.fixedWindowStarted.reset();
        this->diagnosticsState.fixedRealFrames = 0;
        this->diagnosticsState.fixedGeneratedFrames = 0;
        this->diagnosticsState.fixedSkippedFrames = 0;
    }
}

VkResult Swapchain::presentNativeFrame(const PresentInvocation& invocation) {
    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = invocation.nextChain,
        .waitSemaphoreCount = static_cast<uint32_t>(
            invocation.waitSemaphores.size()
        ),
        .pWaitSemaphores = invocation.waitSemaphores.data(),
        .swapchainCount = 1,
        .pSwapchains = &invocation.swapchain,
        .pImageIndices = &invocation.imageIndex,
    };
    const auto originalPresentStarted = startPresentDiagnostic();
    const auto result = invocation.vk.df().QueuePresentKHR(
        invocation.queue, &presentInfo
    );
    const PresentPhaseDurations phases{
        .originalPresent = finishPresentDiagnostic(originalPresentStarted),
    };
    const auto presentWorkDuration = finishPresentDiagnostic(
        invocation.started
    );
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");

    logSlowPresentBreakdown(
        this->diagnosticsState.contextId, this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, presentWorkDuration, phases
    );
    logSlowPresentOperation(
        "present-total", this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, invocation.started, result
    );
    this->frameState.realFrameIndex++;
    return result;
}

VkResult Swapchain::presentOriginalImage(
        const PresentInvocation& invocation,
        const VkSemaphore waitSemaphore, const void* nextChain,
        DiagnosticsClock::duration* const duration) {
    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nextChain,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &waitSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &invocation.swapchain,
        .pImageIndices = &invocation.imageIndex,
    };
    const auto originalPresentStarted = startPresentDiagnostic();
    const auto result = invocation.vk.df().QueuePresentKHR(
        invocation.queue, &presentInfo
    );
    if (duration)
        *duration = finishPresentDiagnostic(originalPresentStarted);
    logSlowPresentOperation(
        "present-original-image", this->frameState.realFrameIndex, this->frameState.sequenceIndex,
        originalPresentStarted, result, std::nullopt, invocation.imageIndex
    );
    return result;
}

bool Swapchain::recoverBackendIfReady(const vk::Vulkan& vk) {
    if (!this->recoveryState.backendPending)
        return true;

    bool backendReady = false;
    try {
        backendReady = this->instance.get().contextReady(this->ctx.get());
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

void Swapchain::ensureHistoryWarmup() {
    const size_t warmupFrames = AdaptiveScheduler::historyWarmupFrameCount();
    if (this->adaptiveScheduler) {
        this->adaptiveScheduler->ensureHistoryWarmup(warmupFrames, true);
    } else if (this->recoveryState.historyWarmupRemaining == 0) {
        this->recoveryState.historyWarmupRemaining = warmupFrames;
    }
}

Swapchain::PresentationFramePlan Swapchain::prepareFramePlan(
        const DiagnosticsClock::time_point presentNow,
        const bool gamescopeHdrTransport,
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
            presentNow, orderedAcquireRecoveryProbe
        )
        : AdaptiveFramePlan{};
    const size_t fixedGeneratedFrameCount = schedulerEnabled
        ? 0
        : (gamescopeHdrTransport
            ? this->fixedRefreshBudget.plan(
                presentNow, this->gamescopeRefreshHz,
                this->configuredFixedGeneratedFrames
            )
            : this->configuredFixedGeneratedFrames);
    if (!schedulerEnabled &&
            fixedGeneratedFrameCount < this->configuredFixedGeneratedFrames) {
        this->diagnosticsState.fixedSkippedFrames +=
            this->configuredFixedGeneratedFrames - fixedGeneratedFrameCount;
    }
    plan.requestedGeneratedFrames = schedulerEnabled
        ? adaptivePlan
        : GeneratedFramePlan::evenlySpaced(fixedGeneratedFrameCount);
    if (orderedAcquireRecoveryProbe && !plan.historyWarmupActive &&
            !plan.requestedGeneratedFrames.empty()) {
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
            pipelineReady = this->instance.get().contextReady(this->ctx.get());
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
        if (busy.requestHistoryWarmup)
            this->ensureHistoryWarmup();
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

void Swapchain::preacquireGeneratedImages(
        const PresentInvocation& invocation,
        PresentationFramePlan& plan, const bool trackGamescopeAdmission) {
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
            generatedImageAcquireTimeout(
                true, plan.configuredAcquireTimeout
            ),
            this->passes.at(i).acquireSemaphore.handle(), VK_NULL_HANDLE,
            &acquiredImage
        );
        if (lastAcquireResult == VK_SUCCESS ||
                lastAcquireResult == VK_SUBOPTIMAL_KHR) {
            plan.preacquiredGeneratedImages.at(
                plan.admittedGeneratedFrameCount++
            ) = acquiredImage;
            continue;
        }
        if (lastAcquireResult == VK_NOT_READY ||
                lastAcquireResult == VK_TIMEOUT) {
            logPressure = trackGamescopeAdmission &&
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
        if (!trackGamescopeAdmission)
            return;
        const auto recovery = this->recoveryState.generatedImageAdmission.reportAvailable();
        if (recovery.resumed && presentDiagnosticsEnabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=generated-admission-recovered"
                      << " context=" << this->diagnosticsState.contextId
                      << " missed_attempts=" << recovery.missedAttempts
                      << " bypassed_frames=" << recovery.bypassedFrames
                      << '\n';
        }
        return;
    }

    if (!trackGamescopeAdmission)
        return;
    this->recoveryState.generatedImageAdmission.reportBypassedFrame();
    if (!this->adaptiveScheduler) {
        this->diagnosticsState.fixedSkippedFrames +=
            plan.requestedGeneratedFrames.size() -
                plan.admittedGeneratedFrameCount;
    }
    if (logPressure && presentDiagnosticsEnabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=generated-admission-pressure"
                  << " context=" << this->diagnosticsState.contextId
                  << " planned=" << plan.requestedGeneratedFrames.size()
                  << " admitted=" << plan.admittedGeneratedFrameCount
                  << " acquire_timeout_ns=0"
                  << " action=native-first\n";
    }
}

void Swapchain::submitSourceCopy(const PresentInvocation& invocation,
        const VkImage swapchainImage, const vk::Image& sourceImage) {
    const auto& commandBuffer = *this->renderCommandBuffer;
    const std::array preBarriers{
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
    };
    const std::array postBarriers{
        barrierHelper(swapchainImage,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_MEMORY_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        ),
    };
    commandBuffer.begin(invocation.vk);
    commandBuffer.blitImage(invocation.vk,
        preBarriers,
        {swapchainImage, sourceImage.handle()},
        sourceImage.getExtent(),
        postBarriers
    );
    commandBuffer.end(invocation.vk);

    const auto sourceSubmitStarted = startPresentDiagnostic();
    commandBuffer.submit(invocation.vk,
        invocation.waitSemaphores, VK_NULL_HANDLE, 0,
        {}, this->syncSemaphore->handle(), this->frameState.sequenceIndex++
    );
    logSlowPresentOperation(
        "submit-source-copy", this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, sourceSubmitStarted
    );
}

VkResult Swapchain::presentHistoryOnly(
        const PresentInvocation& invocation,
        const PresentationFramePlan& plan) {
    PresentPhaseDurations phases{
        .renderFence = plan.renderFenceWaitDuration,
        .sourceCopy = plan.submitSourceCopyDuration,
    };
    const uint64_t sourceTimelineValue = this->frameState.sequenceIndex - 1;
    auto& fallbackPass = this->passes.front();
    auto& fallbackSemaphores = this->postCopySemaphores.at(
        this->frameState.sequenceIndex % this->postCopySemaphores.size()
    );
    auto& fallbackSemaphore = fallbackSemaphores.second;

    auto& fallbackCommandBuffer = fallbackPass.commandBuffer;
    const auto fallbackSubmitStarted = startPresentDiagnostic();
    fallbackCommandBuffer.begin(invocation.vk);
    fallbackCommandBuffer.end(invocation.vk);
    fallbackCommandBuffer.submit(invocation.vk,
        {}, this->syncSemaphore->handle(), sourceTimelineValue,
        std::array{fallbackSemaphore.handle()}, VK_NULL_HANDLE, 0,
        this->renderFence->handle()
    );
    phases.generatedSubmit = finishPresentDiagnostic(
        fallbackSubmitStarted
    );
    this->frameState.renderFenceInFlight = true;

    const auto historyScheduleStarted = startPresentDiagnostic();
    try {
        this->instance.get().scheduleFrameHistory(this->ctx.get());
    } catch (const std::exception& error) {
        phases.schedule = finishPresentDiagnostic(historyScheduleStarted);
        // The fallback copy is already queued and signals fallbackSemaphore.
        // Present the real image through it and quarantine generation instead
        // of returning an error to the application.
        std::cerr << "MAKO Renderer: temporarily bypassing frame generation after "
                     "history scheduling failure; native presentation retained: "
                  << error.what() << '\n';
        this->recoveryState.backendPending = true;
        this->recoveryState.historyWarmupRemaining = 0;
        if (this->adaptiveScheduler)
            this->adaptiveScheduler->cancelHistoryWarmup();

        const auto fallbackResult = this->presentOriginalImage(
            invocation, fallbackSemaphore.handle(), invocation.nextChain,
            &phases.originalPresent
        );
        if (fallbackResult != VK_SUCCESS &&
                fallbackResult != VK_SUBOPTIMAL_KHR) {
            throw ls::vulkan_error(
                fallbackResult, "vkQueuePresentKHR() failed"
            );
        }
        const auto presentWorkDuration = finishPresentDiagnostic(
            invocation.started
        );
        logSlowPresentBreakdown(
            this->diagnosticsState.contextId,
            this->frameState.realFrameIndex,
            this->frameState.sequenceIndex, presentWorkDuration, phases
        );
        logSlowPresentOperation(
            "present-total", this->frameState.realFrameIndex, this->frameState.sequenceIndex,
            invocation.started, fallbackResult
        );
        this->frameState.realFrameIndex++;
        return fallbackResult;
    }
    phases.schedule = finishPresentDiagnostic(historyScheduleStarted);

    this->frameState.backendFrameIndex++;
    if (plan.requestedGeneratedFrames.size() >
            plan.admittedGeneratedFrameCount) {
        logPresentFallback(
            this->frameState.realFrameIndex, this->frameState.sequenceIndex, 0,
            plan.requestedGeneratedFrames.size() -
                plan.admittedGeneratedFrameCount,
            sourceTimelineValue, "nonblocking-admission", "history-only"
        );
    }
    this->reportAdaptiveDelivery(
        plan, plan.admittedGeneratedFrameCount
    );

    if (this->adaptiveScheduler &&
            this->adaptiveScheduler->historyWarmupActive()) {
        logHistoryWarmup(
            this->frameState.realFrameIndex, this->frameState.sequenceIndex,
            this->adaptiveScheduler->historyWarmupRemaining(),
            this->adaptiveScheduler->historyWarmupIsRecovery(),
            std::nullopt
        );
        this->adaptiveScheduler->consumeHistoryWarmupFrame(
            DiagnosticsClock::now()
        );
    } else if (this->recoveryState.historyWarmupRemaining > 0) {
        logHistoryWarmup(
            this->frameState.realFrameIndex, this->frameState.sequenceIndex,
            this->recoveryState.historyWarmupRemaining,
            false, std::nullopt
        );
        this->recoveryState.historyWarmupRemaining--;
    }

    const auto result = this->presentOriginalImage(
        invocation, fallbackSemaphore.handle(), invocation.nextChain,
        &phases.originalPresent
    );
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");

    const auto presentWorkDuration = finishPresentDiagnostic(
        invocation.started
    );
    logSlowPresentBreakdown(
        this->diagnosticsState.contextId, this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, presentWorkDuration, phases
    );
    logSlowPresentOperation(
        "present-total", this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, invocation.started, result
    );
    this->frameState.realFrameIndex++;
    return result;
}

VkResult Swapchain::presentGeneratedFrames(
        const PresentInvocation& invocation,
        const PresentationFramePlan& plan,
        const bool gamescopeHdrTransport) {
    auto maximumAcquireDuration = DiagnosticsClock::duration::zero();
    auto totalAcquireDuration = DiagnosticsClock::duration::zero();
    auto generatedSubmitDuration = DiagnosticsClock::duration::zero();
    auto generatedPresentDuration = DiagnosticsClock::duration::zero();
    auto originalPresentDuration = DiagnosticsClock::duration::zero();
    bool acquireDeadlineExceeded = false;
    const auto reportOrderedAcquire = [&](const bool timedOut) {
        if (!this->privateOrderedTransport)
            return;

        const auto observedAt = DiagnosticsClock::now();
        const auto slowThreshold =
            OrderedAcquireRecovery::slowAcquireDuration(
                this->gamescopeRefreshHz
            );
        const auto observation =
            this->recoveryState.orderedAcquireRecovery.observe(
                observedAt, maximumAcquireDuration, slowThreshold, timedOut,
                acquireDeadlineExceeded
            );
        if (observation.quarantined) {
            this->fixedRefreshBudget.reset();
            if (presentDiagnosticsEnabled()) {
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=ordered-acquire-quarantine"
                          << " context=" << this->diagnosticsState.contextId
                          << " reason="
                          << (observation.timedOut
                                ? "timeout"
                                : observation.deadlineExceeded
                                    ? "deadline-overrun"
                                    : observation.severe
                                        ? "severe-slow-acquire"
                                        : "repeated-slow-acquire")
                          << " acquire_ms="
                          << std::chrono::duration<double, std::milli>(
                                 maximumAcquireDuration
                             ).count()
                          << " slow_threshold_ms="
                          << std::chrono::duration<double, std::milli>(
                                 slowThreshold
                             ).count()
                          << " consecutive_slow_frames="
                          << observation.consecutiveSlowFrames
                          << " consecutive_failures="
                          << observation.consecutiveFailures
                          << " retry_ms="
                          << std::chrono::duration<double, std::milli>(
                                 observation.retryDelay
                             ).count()
                          << " bypassed_frames="
                          << observation.bypassedFrames
                          << " action=native-drain\n";
            }
        } else if (observation.guardArmed &&
                presentDiagnosticsEnabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=ordered-acquire-guard"
                      << " context=" << this->diagnosticsState.contextId
                      << " acquire_ms="
                      << std::chrono::duration<double, std::milli>(
                             maximumAcquireDuration
                         ).count()
                      << " slow_threshold_ms="
                      << std::chrono::duration<double, std::milli>(
                             slowThreshold
                         ).count()
                      << " consecutive_slow_frames="
                      << observation.consecutiveSlowFrames
                      << " action=zero-wait-protection\n";
        } else if (observation.recovered &&
                presentDiagnosticsEnabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=ordered-acquire-recovered"
                      << " context=" << this->diagnosticsState.contextId
                      << " acquire_ms="
                      << std::chrono::duration<double, std::milli>(
                             maximumAcquireDuration
                         ).count()
                      << " consecutive_failures="
                      << observation.consecutiveFailures
                      << " bypassed_frames="
                      << observation.bypassedFrames
                      << " recovery_ms="
                      << std::chrono::duration<double, std::milli>(
                             observation.recoveryDuration
                         ).count()
                      << " source="
                      << (observation.guardCleared
                            ? "slow-acquire-guard" : "native-drain-probe")
                      << " action="
                      << (observation.stabilizing
                            ? "one-frame-stabilization"
                            : "generated-resume")
                      << '\n';
        }
    };

    for (size_t i = 0; i < plan.scheduledGeneratedFrames.size(); ++i) {
        auto& postCopy = this->postCopySemaphores.at(
            this->frameState.sequenceIndex % this->postCopySemaphores.size()
        );
        auto& destinationImage = this->destinationImages.at(i);
        auto& pass = this->passes.at(i);

        uint32_t acquiredImageIndex{};
        VkResult result{};
        if (plan.generatedImagesPreacquired) {
            acquiredImageIndex = plan.preacquiredGeneratedImages.at(i);
            result = VK_SUCCESS;
        } else {
            // Recovery classification is active even when diagnostics are
            // disabled, so this one clock sample cannot use the opt-in timer.
            const auto acquireStarted = DiagnosticsClock::now();
            const uint64_t acquireTimeout = generatedImageAcquireTimeout(
                false, plan.configuredAcquireTimeout
            );
            result = invocation.vk.df().AcquireNextImageKHR(
                invocation.vk.dev(), invocation.swapchain,
                acquireTimeout, pass.acquireSemaphore.handle(),
                VK_NULL_HANDLE, &acquiredImageIndex
            );
            const auto acquireDuration =
                DiagnosticsClock::now() - acquireStarted;
            maximumAcquireDuration = std::max(
                maximumAcquireDuration, acquireDuration
            );
            totalAcquireDuration += acquireDuration;
            if (plan.configuredAcquireTimeout &&
                    acquireDuration >=
                        std::chrono::duration_cast<
                            DiagnosticsClock::duration>(
                                std::chrono::nanoseconds(
                                    *plan.configuredAcquireTimeout
                                )
                            )) {
                acquireDeadlineExceeded = true;
            }
            logSlowPresentOperation(
                "acquire-generated-image", this->frameState.realFrameIndex,
                this->frameState.sequenceIndex,
                acquireStarted, result, i, acquiredImageIndex
            );
        }

        if (plan.configuredAcquireTimeout &&
                (result == VK_TIMEOUT || result == VK_NOT_READY)) {
            reportOrderedAcquire(true);
            // The explicit legacy timeout is an anti-freeze ceiling. Backend
            // work is already scheduled on this ordered path, so drain its
            // final timeline value without reclassifying the miss as an
            // Adaptive timing discontinuity. Cross-frame native quarantine is
            // already armed above for the next application present.
            const size_t skippedFrames =
                plan.scheduledGeneratedFrames.size() - i;
            if (!this->adaptiveScheduler)
                this->diagnosticsState.fixedSkippedFrames += skippedFrames;
            const uint64_t finalGeneratedTimelineValue =
                this->frameState.sequenceIndex + skippedFrames - 1;
            auto& fallbackSemaphore = postCopy.second;
            if (this->adaptiveScheduler) {
                this->reportAdaptiveDelivery(plan, i);
            }

            const auto fallbackSubmitStarted = startPresentDiagnostic();
            auto& fallbackCommandBuffer = pass.commandBuffer;
            fallbackCommandBuffer.begin(invocation.vk);
            fallbackCommandBuffer.end(invocation.vk);
            fallbackCommandBuffer.submit(invocation.vk,
                {}, this->syncSemaphore->handle(),
                finalGeneratedTimelineValue,
                std::array{fallbackSemaphore.handle()}, VK_NULL_HANDLE, 0,
                this->renderFence->handle()
            );
            generatedSubmitDuration += finishPresentDiagnostic(
                fallbackSubmitStarted
            );
            this->frameState.renderFenceInFlight = true;

            logPresentFallback(
                this->frameState.realFrameIndex, this->frameState.sequenceIndex, i, skippedFrames,
                finalGeneratedTimelineValue, "initial-timeout", "scheduled"
            );
            this->frameState.sequenceIndex += skippedFrames;

            result = this->presentOriginalImage(
                invocation, fallbackSemaphore.handle(),
                this->gamescopeDetected || i == 0
                    ? invocation.nextChain : nullptr,
                &originalPresentDuration
            );
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
                throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");

            const auto presentWorkDuration = finishPresentDiagnostic(
                invocation.started
            );
            logSlowPresentBreakdown(
                this->diagnosticsState.contextId,
                this->frameState.realFrameIndex,
                this->frameState.sequenceIndex, presentWorkDuration,
                {
                    .renderFence = plan.renderFenceWaitDuration,
                    .schedule = plan.scheduleFramesDuration,
                    .sourceCopy = plan.submitSourceCopyDuration,
                    .acquire = totalAcquireDuration,
                    .generatedSubmit = generatedSubmitDuration,
                    .generatedPresent = generatedPresentDuration,
                    .originalPresent = originalPresentDuration,
                }
            );
            logSlowPresentOperation(
                "present-total", this->frameState.realFrameIndex, this->frameState.sequenceIndex,
                invocation.started, result
            );
            if (presentDiagnosticsEnabled()) {
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=generated-delivery-miss"
                          << " context=" << this->diagnosticsState.contextId
                          << " planned="
                          << plan.requestedGeneratedFrames.size()
                          << " on_time=" << i
                          << " deadline_ms="
                          << static_cast<double>(
                                 *plan.configuredAcquireTimeout
                             ) / 1'000'000.0
                          << '\n';
            }
            this->frameState.realFrameIndex++;
            return result;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw ls::vulkan_error(
                result, "vkAcquireNextImageKHR() failed"
            );
        }

        const auto acquiredSwapchainImage = this->info.images.at(
            acquiredImageIndex
        );
        auto& commandBuffer = pass.commandBuffer;
        const std::array preBarriers{
            barrierHelper(destinationImage.handle(),
                VK_ACCESS_NONE,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            ),
            barrierHelper(acquiredSwapchainImage,
                VK_ACCESS_NONE,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            ),
        };
        const std::array postBarriers{
            barrierHelper(acquiredSwapchainImage,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_MEMORY_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            ),
        };
        commandBuffer.begin(invocation.vk);
        commandBuffer.blitImage(invocation.vk,
            preBarriers,
            {destinationImage.handle(), acquiredSwapchainImage},
            destinationImage.getExtent(),
            postBarriers
        );

        std::array<VkSemaphore, 2> waitSemaphores{
            pass.acquireSemaphore.handle(), VK_NULL_HANDLE
        };
        size_t waitSemaphoreCount = 1;
        if (i) {
            const auto& previousPostCopy = this->postCopySemaphores.at(
                (this->frameState.sequenceIndex - 1) % this->postCopySemaphores.size()
            );
            waitSemaphores.at(waitSemaphoreCount++) =
                previousPostCopy.second.handle();
        }
        const std::array signalSemaphores{
            postCopy.first.handle(), postCopy.second.handle()
        };

        commandBuffer.end(invocation.vk);
        const auto generatedSubmitStarted = startPresentDiagnostic();
        commandBuffer.submit(invocation.vk,
            std::span{waitSemaphores}.first(waitSemaphoreCount),
            this->syncSemaphore->handle(), this->frameState.sequenceIndex,
            signalSemaphores, VK_NULL_HANDLE, 0,
            i == plan.scheduledGeneratedFrames.size() - 1
                ? this->renderFence->handle() : VK_NULL_HANDLE
        );
        if (i == plan.scheduledGeneratedFrames.size() - 1)
            this->frameState.renderFenceInFlight = true;
        generatedSubmitDuration += finishPresentDiagnostic(
            generatedSubmitStarted
        );
        logSlowPresentOperation(
            "submit-generated-copy", this->frameState.realFrameIndex,
            this->frameState.sequenceIndex,
            generatedSubmitStarted, std::nullopt, i
        );

        const VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = !this->gamescopeDetected && i == 0
                ? invocation.nextChain : nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &postCopy.first.handle(),
            .swapchainCount = 1,
            .pSwapchains = &invocation.swapchain,
            .pImageIndices = &acquiredImageIndex,
        };
        const auto generatedPresentStarted = startPresentDiagnostic();
        result = invocation.vk.df().QueuePresentKHR(
            invocation.queue, &presentInfo
        );
        generatedPresentDuration += finishPresentDiagnostic(
            generatedPresentStarted
        );
        logSlowPresentOperation(
            "present-generated-image", this->frameState.realFrameIndex,
            this->frameState.sequenceIndex,
            generatedPresentStarted, result, i, acquiredImageIndex
        );
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");
        if (!this->adaptiveScheduler)
            this->diagnosticsState.fixedGeneratedFrames++;

        this->frameState.sequenceIndex++;
    }

    auto& lastPostCopy = this->postCopySemaphores.at(
        (this->frameState.sequenceIndex - 1) % this->postCopySemaphores.size()
    );
    const auto result = this->presentOriginalImage(
        invocation, lastPostCopy.second.handle(),
        this->gamescopeDetected ? invocation.nextChain : nullptr,
        &originalPresentDuration
    );
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");

    const auto presentWorkDuration = finishPresentDiagnostic(
        invocation.started
    );
    reportOrderedAcquire(false);
    this->reportAdaptiveDelivery(
        plan, plan.scheduledGeneratedFrames.size()
    );
    logSlowPresentBreakdown(
        this->diagnosticsState.contextId, this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, presentWorkDuration,
        {
            .renderFence = plan.renderFenceWaitDuration,
            .schedule = plan.scheduleFramesDuration,
            .sourceCopy = plan.submitSourceCopyDuration,
            .acquire = totalAcquireDuration,
            .generatedSubmit = generatedSubmitDuration,
            .generatedPresent = generatedPresentDuration,
            .originalPresent = originalPresentDuration,
        }
    );
    logSlowPresentOperation(
        "present-total", this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, invocation.started, result
    );
    this->frameState.realFrameIndex++;
    return result;
}

VkResult Swapchain::present(const vk::Vulkan& vk,
        const VkQueue queue, const VkSwapchainKHR swapchain,
        void* nextChain, const uint32_t imageIndex,
        const std::span<const VkSemaphore> waitSemaphores) {
    const DiagnosticsContextScope diagnosticsContext(
        this->diagnosticsState.contextId
    );
    const void* lowerNextChain = nextChain;
    // Match the immutable create-time choice. Ordered SDR filters Gamescope's
    // dynamic MAILBOX override so the lower FIFO swapchain stays ordered. HDR
    // preserves it. A feedback transition cannot change the game-owned
    // VkSwapchainKHR's creation contract.
    ScopedPNextRemoval presentMode(
        lowerNextChain, VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT,
        this->privateOrderedTransport
    );
    const bool gamescopeHdrTransport =
        this->gamescopeDetected && !this->privateOrderedTransport;

    const auto limiterArrival = DiagnosticsClock::now();
    const auto limiterDeadline = this->realFramePacer.schedule(
        limiterArrival, effectiveBaseFpsCap(this->profile)
    );
    if (limiterDeadline > limiterArrival)
        std::this_thread::sleep_until(limiterDeadline);

    const auto presentNow = DiagnosticsClock::now();
    const PresentInvocation invocation{
        .vk = vk,
        .queue = queue,
        .swapchain = swapchain,
        .nextChain = lowerNextChain,
        .imageIndex = imageIndex,
        .waitSemaphores = waitSemaphores,
        .started = startPresentDiagnostic(),
    };
    this->recordPresentCadence(presentNow);

    if (!this->applyPendingColorPipeline(vk))
        return this->presentNativeFrame(invocation);

    // Frame generation is live-disabled; hand the game's own image directly
    // to the driver without copies, model scheduling, fences or generated
    // images.
    if (!effectiveFrameGenerationEnabled(
            this->profile, this->gamescopeRefreshHz) ||
            !this->colorPipeline.generationSupported) {
        return this->presentNativeFrame(invocation);
    }
    if (!this->recoverBackendIfReady(vk))
        return this->presentNativeFrame(invocation);

    bool orderedAcquireRecoveryProbe = false;
    if (this->privateOrderedTransport) {
        const auto recovery =
            this->recoveryState.orderedAcquireRecovery.beforePresent(
                presentNow
            );
        if (recovery.bypassGeneration) {
            // Keep Adaptive's cadence clock current while freezing every
            // multiplier evaluation. No backend work or synthetic swapchain
            // acquire is attempted until the ordered FIFO has drained.
            if (this->adaptiveScheduler) {
                static_cast<void>(
                    this->adaptiveScheduler->planFrame(presentNow, true)
                );
            } else {
                this->diagnosticsState.fixedSkippedFrames +=
                    this->configuredFixedGeneratedFrames;
            }
            return this->presentNativeFrame(invocation);
        }
        orderedAcquireRecoveryProbe = recovery.limitGeneratedFrames;
        if (recovery.resetCadenceClock && this->adaptiveScheduler)
            this->adaptiveScheduler->resetTiming(presentNow);
        if (recovery.recoveryStabilized && presentDiagnosticsEnabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=ordered-acquire-stabilized"
                      << " context=" << this->diagnosticsState.contextId
                      << " consecutive_failures="
                      << recovery.consecutiveFailures
                      << " bypassed_frames=" << recovery.bypassedFrames
                      << " recovery_ms="
                      << std::chrono::duration<double, std::milli>(
                             recovery.drainDuration
                         ).count()
                      << " action=resume-normal-policy\n";
        }
        if (recovery.beginHistoryWarmup) {
            this->ensureHistoryWarmup();
            this->fixedRefreshBudget.reset();
            if (presentDiagnosticsEnabled()) {
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=ordered-acquire-retry"
                          << " context=" << this->diagnosticsState.contextId
                          << " consecutive_failures="
                          << recovery.consecutiveFailures
                          << " bypassed_frames="
                          << recovery.bypassedFrames
                          << " drain_ms="
                          << std::chrono::duration<double, std::milli>(
                                 recovery.drainDuration
                             ).count()
                          << " history_warmup_frames="
                          << AdaptiveScheduler::historyWarmupFrameCount()
                          << " action=warm-history-before-one-frame-probe\n";
            }
        }
    }

    const auto swapchainImage = this->info.images.at(imageIndex);
    // Presentation counters continue across live-off intervals, while the
    // backend's temporal history does not. Index sources by frames actually
    // submitted to the backend so native-only frames cannot invert history.
    const auto& sourceImage = this->sourceImages.at(
        this->frameState.backendFrameIndex % 2
    );
    auto plan = this->prepareFramePlan(
        presentNow, gamescopeHdrTransport, orderedAcquireRecoveryProbe
    );

    // The Gamescope HDR bridge is native-first: never hold the application's
    // real frame behind unfinished private work. Ordered SDR retains the
    // synchronous FIFO/fence behavior.
    if (!this->generationPipelineReady(
            vk, gamescopeHdrTransport, plan, presentNow)) {
        return this->presentNativeFrame(invocation);
    }

    if (gamescopeHdrTransport)
        this->preacquireGeneratedImages(invocation, plan, true);
    size_t scheduledGeneratedFrameCount = gamescopeHdrTransport
        ? plan.admittedGeneratedFrameCount
        : plan.requestedGeneratedFrames.size();
    plan.scheduledGeneratedFrames = scheduleAdmittedGeneratedFrames(
        plan.requestedGeneratedFrames, scheduledGeneratedFrameCount
    );
    bool bypassGeneratedFrames = plan.historyWarmupActive ||
        plan.scheduledGeneratedFrames.empty();
    if (plan.historyWarmupActive && !this->adaptiveScheduler)
        this->diagnosticsState.fixedSkippedFrames +=
            plan.requestedGeneratedFrames.size();

    // Resolve previous application-device work before scheduling another
    // backend frame. If the fence budget is missed, no new backend work has
    // been created and the current game image can be presented natively.
    const auto renderFenceStarted = startPresentDiagnostic();
    const bool renderFenceReady = this->prepareRenderFence(vk);
    plan.renderFenceWaitDuration = finishPresentDiagnostic(
        renderFenceStarted
    );
    if (!renderFenceReady) {
        this->handleRenderFenceBudgetMiss(plan);
        return this->presentNativeFrame(invocation);
    }

    if (orderedAcquireRecoveryProbe && !plan.historyWarmupActive) {
        // Do not repeat the ordinary 50 ms acquire during recovery. A missing
        // image proves that the ordered FIFO has not drained yet, so retain
        // native presentation and retry a zero-wait one-frame probe later.
        this->preacquireGeneratedImages(invocation, plan, false);
        if (plan.admittedGeneratedFrameCount == 0) {
            const auto miss = this->recoveryState.orderedAcquireRecovery
                .reportNonblockingProbeUnavailable(presentNow);
            if (miss.quarantined) {
                this->fixedRefreshBudget.reset();
                if (presentDiagnosticsEnabled()) {
                    std::cerr << "MAKO Renderer: present diagnostics: "
                                 "operation=ordered-acquire-quarantine"
                              << " context="
                              << this->diagnosticsState.contextId
                              << " reason=guard-probe-unavailable"
                              << " consecutive_failures="
                              << miss.consecutiveFailures
                              << " retry_ms="
                              << std::chrono::duration<double, std::milli>(
                                     miss.retryDelay
                                 ).count()
                              << " bypassed_frames="
                              << miss.bypassedFrames
                              << " action=native-drain\n";
                }
            } else if (miss.diagnostic && presentDiagnosticsEnabled()) {
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=ordered-acquire-probe-pending"
                          << " context=" << this->diagnosticsState.contextId
                          << " acquire_timeout_ns=0"
                          << " bypassed_frames=" << miss.bypassedFrames
                          << " action=native-present\n";
            }
            return this->presentNativeFrame(invocation);
        }
        scheduledGeneratedFrameCount = plan.admittedGeneratedFrameCount;
        plan.scheduledGeneratedFrames = scheduleAdmittedGeneratedFrames(
            plan.requestedGeneratedFrames, scheduledGeneratedFrameCount
        );
        bypassGeneratedFrames = false;
    }

    if (!bypassGeneratedFrames) {
        const auto scheduleStarted = startPresentDiagnostic();
        try {
            this->instance.get().scheduleFrames(
                this->ctx.get(), plan.scheduledGeneratedFrames.timestamps()
            );
        } catch (const std::exception& error) {
            std::cerr << "MAKO Renderer: temporarily bypassing frame generation after "
                         "backend scheduling failure; native presentation retained: "
                      << error.what() << '\n';
            this->recoveryState.backendPending = true;
            this->recoveryState.historyWarmupRemaining = 0;
            if (this->adaptiveScheduler) {
                this->reportAdaptiveDelivery(plan, 0);
                this->adaptiveScheduler->cancelHistoryWarmup();
            } else {
                this->diagnosticsState.fixedSkippedFrames +=
                    plan.admittedGeneratedFrameCount;
            }
            if (!gamescopeHdrTransport ||
                    plan.admittedGeneratedFrameCount == 0) {
                return this->presentNativeFrame(invocation);
            }
            return this->retireAcquiredImagesAndPresent(
                vk, queue, swapchain, lowerNextChain,
                imageIndex, waitSemaphores,
                std::span<const uint32_t>(
                    plan.preacquiredGeneratedImages.data(),
                    plan.admittedGeneratedFrameCount
                ),
                swapchainImage
            );
        }
        this->frameState.backendFrameIndex++;
        plan.scheduleFramesDuration = finishPresentDiagnostic(
            scheduleStarted
        );
        logSlowPresentOperation(
            "schedule-frames", this->frameState.realFrameIndex,
            this->frameState.sequenceIndex, scheduleStarted
        );
    }

    const auto sourceCopyStarted = startPresentDiagnostic();
    this->submitSourceCopy(invocation, swapchainImage, sourceImage);
    plan.submitSourceCopyDuration = finishPresentDiagnostic(
        sourceCopyStarted
    );
    if (bypassGeneratedFrames)
        return this->presentHistoryOnly(invocation, plan);
    return this->presentGeneratedFrames(
        invocation, plan, gamescopeHdrTransport
    );
}
