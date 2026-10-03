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

void Swapchain::recordPresentCadence(const DiagnosticsClock::time_point presentNow) {
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
                    << " display_budget_applies="
                    << (this->gamescopeRefreshHz.value_or(0) > 0 ? 1 : 0)
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

VkResult Swapchain::presentSpatiallyScaledFrame(
        const PresentInvocation& invocation) {
    auto& pass = this->spatialScalingPasses.at(invocation.imageIndex);
    this->prepareSpatialScalingPass(invocation.vk, pass);
    const VkImage applicationImage = this->info.images.at(
        invocation.imageIndex
    );
    const auto scalingStarted = startPresentDiagnostic();
    pass.commandBuffer.begin(invocation.vk);
    this->spatialScaler->record(
        invocation.vk, pass.commandBuffer, applicationImage
    );
    pass.commandBuffer.end(invocation.vk);
    pass.commandBuffer.submit(
        invocation.vk,
        invocation.waitSemaphores, VK_NULL_HANDLE, 0,
        std::array{pass.readySemaphore.handle()},
        VK_NULL_HANDLE, 0, pass.completionFence.handle(), invocation.queue
    );
    pass.completionInFlight = true;
    const auto scalingDuration = finishPresentDiagnostic(scalingStarted);
    logSlowPresentOperation(
        "submit-spatial-scaling", this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, scalingStarted,
        std::nullopt, std::nullopt, invocation.imageIndex
    );

    const VkSemaphore ready = pass.readySemaphore.handle();
    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = invocation.nextChain,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &ready,
        .swapchainCount = 1,
        .pSwapchains = &invocation.swapchain,
        .pImageIndices = &invocation.imageIndex,
    };
    const auto originalPresentStarted = startPresentDiagnostic();
    const auto result = this->queuePresentWithRetirementFence(
        invocation.vk, invocation.queue, presentInfo
    );
    const auto originalPresentDuration = finishPresentDiagnostic(
        originalPresentStarted
    );
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");

    const auto presentWorkDuration = finishPresentDiagnostic(
        invocation.started
    );
    logSlowPresentOperation(
        "present-total", this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, invocation.started, result
    );
    logSlowPresentBreakdown(
        this->diagnosticsState.contextId, this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, presentWorkDuration,
        {
            .sourceCopy = scalingDuration,
            .originalPresent = originalPresentDuration,
        }
    );
    this->frameState.realFrameIndex++;
    return result;
}

VkResult Swapchain::presentNativeFrame(const PresentInvocation& invocation) {
    if (this->spatialScaler)
        return this->presentSpatiallyScaledFrame(invocation);

    return this->presentDirectApplicationFrame(invocation);
}

VkResult Swapchain::presentDirectApplicationFrame(
        const PresentInvocation& invocation,
        const std::string_view healthSource) {
    if (invocation.originalPresentDeadline)
        invocation.waitForOutput(*invocation.originalPresentDeadline);
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
    const auto result = this->queuePresentWithRetirementFence(
        invocation.vk, invocation.queue, presentInfo
    );
    const PresentPhaseDurations phases{
        .originalPresent = finishPresentDiagnostic(originalPresentStarted),
    };
    const auto presentWorkDuration = finishPresentDiagnostic(
        invocation.started
    );
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");
    static_cast<void>(healthSource);

    logSlowPresentOperation(
        "present-total", this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, invocation.started, result
    );
    logSlowPresentBreakdown(
        this->diagnosticsState.contextId, this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, presentWorkDuration, phases
    );
    this->frameState.realFrameIndex++;
    return result;
}

VkResult Swapchain::presentOriginalImage(
        const PresentInvocation& invocation,
        const VkSemaphore waitSemaphore, const void* nextChain,
        DiagnosticsClock::duration* const duration) {
    VkSemaphore presentWaitSemaphore = waitSemaphore;
    if (this->spatialScaler &&
            this->spatialFramePipelinePlacement ==
                SpatialFramePipelinePlacement::PostFrameGeneration) {
        auto& pass = this->spatialScalingPasses.at(invocation.imageIndex);
        this->prepareSpatialScalingPass(invocation.vk, pass);
        const VkImage applicationImage = this->info.images.at(
            invocation.imageIndex
        );
        const auto scalingStarted = startPresentDiagnostic();
        pass.commandBuffer.begin(invocation.vk);
        this->spatialScaler->record(
            invocation.vk, pass.commandBuffer, applicationImage
        );
        pass.commandBuffer.end(invocation.vk);
        pass.commandBuffer.submit(
            invocation.vk,
            std::array{waitSemaphore}, VK_NULL_HANDLE, 0,
            std::array{pass.readySemaphore.handle()},
            VK_NULL_HANDLE, 0, pass.completionFence.handle(), invocation.queue
        );
        pass.completionInFlight = true;
        presentWaitSemaphore = pass.readySemaphore.handle();
        logSlowPresentOperation(
            "submit-post-frame-spatial-original",
            this->frameState.realFrameIndex,
            this->frameState.sequenceIndex, scalingStarted,
            std::nullopt, std::nullopt, invocation.imageIndex
        );
    }
    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nextChain,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &presentWaitSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &invocation.swapchain,
        .pImageIndices = &invocation.imageIndex,
    };
    if (invocation.originalPresentDeadline)
        invocation.waitForOutput(*invocation.originalPresentDeadline);
    const auto originalPresentStarted = startPresentDiagnostic();
    const auto result = this->queuePresentWithRetirementFence(
        invocation.vk, invocation.queue, presentInfo, duration
    );
    logSlowPresentOperation(
        "present-original-image", this->frameState.realFrameIndex, this->frameState.sequenceIndex,
        originalPresentStarted, result, std::nullopt, invocation.imageIndex
    );
    if (result == VK_ERROR_OUT_OF_DATE_KHR && presentDiagnosticsEnabled()) {
        // Every direct original-image path has already submitted the
        // application's acquired image to the lower presentation role. Keep
        // that fact explicit so callers may safely propagate OUT_OF_DATE
        // without attempting a duplicate present during recreation.
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=original-present-recreation-propagate"
                  << " context=" << this->diagnosticsState.contextId
                  << " frame=" << this->frameState.realFrameIndex
                  << " sequence=" << this->frameState.sequenceIndex
                  << " result=" << result
                  << " action=application-image-submitted-before-recreation\n";
    }
    return result;
}

void Swapchain::submitSourceCopy(const PresentInvocation& invocation,
        const VkImage swapchainImage, const vk::Image& sourceImage) {
    if (this->spatialScaler &&
            this->spatialFramePipelinePlacement ==
                SpatialFramePipelinePlacement::PreFrameGeneration) {
        auto& pass = this->spatialScalingPasses.at(invocation.imageIndex);
        this->prepareSpatialScalingPass(invocation.vk, pass);
        pass.commandBuffer.begin(invocation.vk);
        this->spatialScaler->record(
            invocation.vk, pass.commandBuffer,
            swapchainImage, sourceImage.handle()
        );
        pass.commandBuffer.end(invocation.vk);

        const auto sourceSubmitStarted = startPresentDiagnostic();
        const auto sourceTimelineValue =
            this->frameState.backendTimelineIndex++;
        this->frameState.sequenceIndex++;
        pass.commandBuffer.submit(
            invocation.vk,
            invocation.waitSemaphores, VK_NULL_HANDLE, 0,
            {}, this->syncSemaphore->handle(),
            sourceTimelineValue,
            pass.completionFence.handle(), invocation.queue
        );
        pass.completionInFlight = true;
        logSlowPresentOperation(
            "submit-spatial-source", this->frameState.realFrameIndex,
            this->frameState.sequenceIndex, sourceSubmitStarted,
            std::nullopt, std::nullopt, invocation.imageIndex
        );
        return;
    }

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
    const auto sourceTimelineValue =
        this->frameState.backendTimelineIndex++;
    this->frameState.sequenceIndex++;
    commandBuffer.submit(invocation.vk,
        invocation.waitSemaphores, VK_NULL_HANDLE, 0,
        {}, this->syncSemaphore->handle(), sourceTimelineValue
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
    const uint64_t sourceTimelineValue =
        this->frameState.backendTimelineIndex - 1;
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
        this->instance->scheduleFrameHistory(this->ctx.get());
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
        logSlowPresentOperation(
            "present-total", this->frameState.realFrameIndex, this->frameState.sequenceIndex,
            invocation.started, fallbackResult
        );
        logSlowPresentBreakdown(
            this->diagnosticsState.contextId,
            this->frameState.realFrameIndex,
            this->frameState.sequenceIndex, presentWorkDuration, phases
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
        const auto recoveryCompleted = std::max(DiagnosticsClock::now(),
            invocation.originalPresentDeadline.value_or(
                DiagnosticsClock::time_point{}));
        const bool transitionRecoveryActive = this->recoveryState
            .orderedAcquireRecovery.transitionRecoveryActive();
        this->adaptiveScheduler->consumeHistoryWarmupFrame(
            historyWarmupCadenceBoundary(
                invocation.cadenceStarted, recoveryCompleted,
                transitionRecoveryActive
            )
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
    logSlowPresentOperation(
        "present-total", this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, invocation.started, result
    );
    logSlowPresentBreakdown(
        this->diagnosticsState.contextId, this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, presentWorkDuration, phases
    );
    this->frameState.realFrameIndex++;
    return result;
}
