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

VkResult Swapchain::presentGeneratedFrames(
        const PresentInvocation& invocation,
        const PresentationFramePlan& plan,
        const bool gamescopeHdrTransport) {
    const bool adaptiveVariableRefreshBounded =
        plan.adaptiveOrderedDeliveryPolicy ==
            AdaptiveOrderedDeliveryPolicy::VariableRefreshBounded;
    auto maximumAcquireDuration = plan.preacquireDuration;
    auto totalAcquireDuration = plan.preacquireDuration;
    auto generatedSubmitDuration = DiagnosticsClock::duration::zero();
    auto generatedPresentDuration = DiagnosticsClock::duration::zero();
    auto originalPresentDuration = DiagnosticsClock::duration::zero();
    bool acquireDeadlineExceeded = false;
    uint64_t lastAcquireTimeout = 0;
    const auto reportOrderedAcquire = [&](const bool timedOut,
            const bool budgetExhausted,
            const size_t presentedGeneratedFrames) {
        if (!this->privateOrderedTransport)
            return;

        const auto observedAt = DiagnosticsClock::now();
        const bool transitionRecoveryActive = this->recoveryState
            .orderedAcquireRecovery.transitionRecoveryActive();
        const auto observation =
            this->recoveryState.orderedAcquireRecovery.observe(
                observedAt, timedOut, acquireDeadlineExceeded,
                plan.boundedOrderedAcquireProbe
            );
        if (transitionRecoveryActive && (timedOut || budgetExhausted) &&
                this->adaptiveScheduler) {
            static_cast<void>(this->adaptiveScheduler
                ->rejectActiveRampForTransportMiss(observedAt));
        }
        if (observation.recovered && transitionRecoveryActive &&
                this->adaptiveScheduler) {
            this->adaptiveScheduler->beginTransportRecovery(
                observedAt, true
            );
        }
        if (observation.quarantined) {
            this->fixedRefreshBudget.reset();
            if (presentDiagnosticsEnabled()) {
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=ordered-acquire-quarantine"
                          << " context=" << this->diagnosticsState.contextId
                          << " phase=native-drain"
                          << " reason="
                          << (budgetExhausted
                                ? "budget-exhausted"
                                : "generated-image-timeout")
                          << " acquire_total_ms="
                          << std::chrono::duration<double, std::milli>(
                                 totalAcquireDuration
                             ).count()
                          << " acquire_max_ms="
                          << std::chrono::duration<double, std::milli>(
                                 maximumAcquireDuration
                             ).count()
                          << " acquire_budget_ms="
                          << (plan.configuredAcquireTimeout
                                ? static_cast<double>(
                                      *plan.configuredAcquireTimeout
                                  ) / 1'000'000.0
                                : 0.0)
                          << " acquire_attempt_timeout_ms="
                          << static_cast<double>(lastAcquireTimeout) /
                                1'000'000.0
                          << " consecutive_failures="
                          << observation.consecutiveFailures
                          << " retry_ms="
                          << std::chrono::duration<double, std::milli>(
                                 observation.retryDelay
                             ).count()
                          << " bypassed_frames="
                          << observation.bypassedFrames
                          << " recovery_ms="
                          << std::chrono::duration<double, std::milli>(
                                 observation.recoveryDuration
                             ).count()
                          << " requested_generated="
                          << plan.requestedGeneratedFrames.size()
                          << " admitted_generated="
                          << plan.admittedGeneratedFrameCount
                          << " presented_generated="
                          << presentedGeneratedFrames
                          << " transition_authorized="
                          << transitionRecoveryActive
                          << " frame=" << this->frameState.realFrameIndex
                          << " sequence=" << this->frameState.sequenceIndex
                          << " action=native-drain\n";
            }
        } else if (observation.recovered && presentDiagnosticsEnabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=ordered-acquire-recovered"
                      << " context=" << this->diagnosticsState.contextId
                      << " consecutive_failures="
                      << observation.consecutiveFailures
                      << " requested_generated="
                      << plan.requestedGeneratedFrames.size()
                      << " admitted_generated="
                      << plan.admittedGeneratedFrameCount
                      << " presented_generated="
                      << presentedGeneratedFrames
                      << " action=generated-resume\n";
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
        bool acquireBudgetExhausted = false;
        if (plan.generatedImagesPreacquired) {
            acquiredImageIndex = plan.preacquiredGeneratedImages.at(i);
            result = VK_SUCCESS;
        } else {
            // Recovery classification is active even when diagnostics are
            // disabled, so this one clock sample cannot use the opt-in timer.
            const auto acquireStarted = DiagnosticsClock::now();
            const auto consumedAcquireNanoseconds =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    totalAcquireDuration
                ).count();
            const auto remainingAcquireBudget =
                remainingGeneratedImageAcquireBudget(
                    plan.configuredAcquireTimeout,
                    consumedAcquireNanoseconds > 0
                        ? static_cast<uint64_t>(consumedAcquireNanoseconds)
                        : 0
                );
            acquireBudgetExhausted = remainingAcquireBudget &&
                *remainingAcquireBudget == 0;
            const uint64_t acquireTimeout = orderedGeneratedImageAcquireTimeout(
                this->gamescopeRefreshHz, remainingAcquireBudget
            );
            lastAcquireTimeout = acquireTimeout;
            if (acquireBudgetExhausted) {
                result = VK_TIMEOUT;
                acquireDeadlineExceeded = true;
            } else {
                result = invocation.vk.df().AcquireNextImageKHR(
                    invocation.vk.dev(), invocation.swapchain,
                    acquireTimeout, pass.acquireSemaphore.handle(),
                    VK_NULL_HANDLE, &acquiredImageIndex
                );
            }
            const auto acquireDuration =
                DiagnosticsClock::now() - acquireStarted;
            maximumAcquireDuration = std::max(
                maximumAcquireDuration, acquireDuration
            );
            totalAcquireDuration += acquireDuration;
            if (plan.configuredAcquireTimeout) {
                const auto acquireBudget =
                    std::chrono::duration_cast<DiagnosticsClock::duration>(
                        std::chrono::nanoseconds(
                            *plan.configuredAcquireTimeout
                        )
                    );
                if (totalAcquireDuration >= acquireBudget)
                    acquireDeadlineExceeded = true;
            }
            if (acquireBudgetExhausted && presentDiagnosticsEnabled()) {
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=ordered-acquire-budget-exhausted"
                          << " context=" << this->diagnosticsState.contextId
                          << " phase=acquire"
                          << " acquire_total_ms="
                          << std::chrono::duration<double, std::milli>(
                                 totalAcquireDuration
                             ).count()
                          << " acquire_max_ms="
                          << std::chrono::duration<double, std::milli>(
                                 maximumAcquireDuration
                             ).count()
                          << " budget_ms="
                          << static_cast<double>(
                                 *plan.configuredAcquireTimeout
                             ) / 1'000'000.0
                          << " requested_generated="
                          << plan.requestedGeneratedFrames.size()
                          << " admitted_generated="
                          << plan.admittedGeneratedFrameCount
                          << " presented_generated=" << i
                          << " frame=" << this->frameState.realFrameIndex
                          << " sequence=" << this->frameState.sequenceIndex
                          << " action=stop-acquiring\n";
            }
            logSlowPresentOperation(
                "acquire-generated-image", this->frameState.realFrameIndex,
                this->frameState.sequenceIndex,
                acquireStarted, result, i, acquiredImageIndex
            );
        }

        if (plan.configuredAcquireTimeout &&
                (result == VK_TIMEOUT || result == VK_NOT_READY)) {
            // Record this normal batch's delivery loss before changing its
            // transport state. Otherwise intermittent timeouts separated by
            // healthy batches disappear from Adaptive's multiplier evaluation
            // even though generated outputs were lost.
            this->reportAdaptiveDelivery(plan, i);
            if (adaptiveVariableRefreshBounded) {
                const bool logPressure = this->recoveryState
                    .generatedImageAdmission.reportUnavailable();
                this->handleGeneratedImageAdmissionPressure(
                    plan, i, logPressure, false,
                    false,
                    lastAcquireTimeout,
                    "adaptive-fallback"
                );
            } else {
                reportOrderedAcquire(
                    !acquireBudgetExhausted, acquireBudgetExhausted, i
                );
            }
            // Backend work is already scheduled on this ordered path, so drain
            // its final timeline value. Fixed-refresh Adaptive retains the 3.3
            // ordered recovery contract. A bounded VRR timeout instead arms
            // zero-wait admission for later frames without changing the
            // scheduler's validated load. Fixed and explicit recovery probes
            // retain their direct transport contracts.
            const size_t skippedFrames =
                plan.scheduledGeneratedFrames.size() - i;
            if (!this->adaptiveScheduler)
                this->diagnosticsState.fixedSkippedFrames += skippedFrames;
            const uint64_t finalGeneratedTimelineValue =
                this->frameState.backendTimelineIndex + skippedFrames - 1;
            auto& fallbackSemaphore = postCopy.second;

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
            this->frameState.backendTimelineIndex += skippedFrames;

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
            logSlowPresentOperation(
                "present-total", this->frameState.realFrameIndex, this->frameState.sequenceIndex,
                invocation.started, result
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
            if (presentDiagnosticsEnabled()) {
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=generated-delivery-miss"
                          << " context=" << this->diagnosticsState.contextId
                          << " reason="
                          << (acquireBudgetExhausted
                                ? "acquire-budget-exhausted"
                                : "acquire-timeout")
                          << " planned="
                          << plan.requestedGeneratedFrames.size()
                          << " on_time=" << i
                          << " deadline_ms="
                          << static_cast<double>(
                                acquireBudgetExhausted
                                    ? *plan.configuredAcquireTimeout
                                    : lastAcquireTimeout
                             ) / 1'000'000.0
                          << " deadline_scope="
                          << (acquireBudgetExhausted
                                ? "application-present"
                                : "generated-image")
                          << " application_present_budget_ms="
                          << static_cast<double>(
                                plan.configuredAcquireTimeout.value_or(0)
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
        commandBuffer.begin(invocation.vk);
        if (this->spatialScaler &&
                this->spatialFramePipelinePlacement ==
                    SpatialFramePipelinePlacement::PostFrameGeneration) {
            this->spatialScaler->recordSourceToPresentation(
                invocation.vk, commandBuffer,
                destinationImage.handle(), acquiredSwapchainImage
            );
        } else {
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
            commandBuffer.blitImage(invocation.vk,
                preBarriers,
                {destinationImage.handle(), acquiredSwapchainImage},
                destinationImage.getExtent(),
                postBarriers
            );
        }

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
            this->syncSemaphore->handle(),
            this->frameState.backendTimelineIndex,
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
        if (invocation.pacedOutputs)
            invocation.waitForOutput(invocation.pacedOutputs->at(i));
        else if (invocation.originalPresentDeadline)
            invocation.waitForOutput(*invocation.originalPresentDeadline);
        const auto generatedPresentStarted = startPresentDiagnostic();
        result = this->queuePresentWithRetirementFence(
            invocation.vk, invocation.queue, presentInfo
        );
        const auto oneGeneratedPresentDuration = finishPresentDiagnostic(
            generatedPresentStarted
        );
        generatedPresentDuration += oneGeneratedPresentDuration;
        logSlowPresentOperation(
            "present-generated-image", this->frameState.realFrameIndex,
            this->frameState.sequenceIndex,
            generatedPresentStarted, result, i, acquiredImageIndex
        );
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            // The generated image belongs to MAKO, but the application's real
            // image is still acquired and its copy submission has already
            // consumed the application's wait semaphores. Return that real
            // image through the normal lower present path before propagating
            // OUT_OF_DATE, otherwise a game can block while tearing down an
            // acquired image that never reached the presentation engine.
            const auto originalResult = this->presentOriginalImage(
                invocation, postCopy.second.handle(),
                this->gamescopeDetected ? invocation.nextChain : nullptr,
                &originalPresentDuration
            );
            if (presentDiagnosticsEnabled()) {
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=generated-present-recreation-drain"
                          << " context=" << this->diagnosticsState.contextId
                          << " frame=" << this->frameState.realFrameIndex
                          << " sequence=" << this->frameState.sequenceIndex
                          << " pass=" << i
                          << " generated_result=" << result
                          << " original_result=" << originalResult
                          << " action=return-application-image-before-recreation\n";
            }
            if (originalResult != VK_SUCCESS &&
                    originalResult != VK_SUBOPTIMAL_KHR &&
                    originalResult != VK_ERROR_OUT_OF_DATE_KHR) {
                throw ls::vulkan_error(
                    originalResult, "vkQueuePresentKHR() failed"
                );
            }
            if (this->adaptiveScheduler)
                this->reportAdaptiveDelivery(plan, i);
            else
                this->diagnosticsState.fixedSkippedFrames +=
                    plan.scheduledGeneratedFrames.size() - i;
            this->frameState.sequenceIndex++;
            this->frameState.backendTimelineIndex++;
            this->frameState.realFrameIndex++;
            return VK_ERROR_OUT_OF_DATE_KHR;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");
        if (!this->adaptiveScheduler)
            this->diagnosticsState.fixedGeneratedFrames++;

        this->frameState.sequenceIndex++;
        this->frameState.backendTimelineIndex++;
    }

    auto& lastPostCopy = this->postCopySemaphores.at(
        (this->frameState.sequenceIndex - 1) % this->postCopySemaphores.size()
    );
    const auto result = this->presentOriginalImage(
        invocation, lastPostCopy.second.handle(),
        this->gamescopeDetected ? invocation.nextChain : nullptr,
        &originalPresentDuration
    );
    const bool originalPresentRequestedRecreation =
        result == VK_ERROR_OUT_OF_DATE_KHR;
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR &&
            !originalPresentRequestedRecreation)
        throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");

    const auto presentWorkDuration = finishPresentDiagnostic(
        invocation.started
    );
    logSlowPresentOperation(
        "present-total", this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, invocation.started, result
    );
    if (adaptiveVariableRefreshBounded) {
        if (plan.scheduledGeneratedFrames.size() ==
                plan.requestedGeneratedFrames.size()) {
            const size_t requiredStableBatches =
                adaptiveVariableRefreshBounded
                    ? generatedImageAdmissionRecoveryBatches(
                        this->info.images.size(),
                        plan.requestedGeneratedFrames.size()
                    )
                    : 1;
            this->reportGeneratedImageAdmissionAvailable(
                requiredStableBatches
            );
        }
    } else {
        reportOrderedAcquire(
            false, false, plan.scheduledGeneratedFrames.size()
        );
    }
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
    this->frameState.realFrameIndex++;
    return result;
}
