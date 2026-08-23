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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
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
            vk, std::move(waits), VK_NULL_HANDLE, 0,
            {pcs.first.handle(), pcs.second.handle()},
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
            std::cerr << "MAKO Renderer: present diagnostics: operation=fixed-plan"
                      << " context=" << this->diagnosticsState.contextId
                      << " base_fps=" << realFps
                      << " multiplier=" << this->profile.multiplier
                      << " generated_per_real="
                      << (this->profile.frame_generation_enabled
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
    const auto result = invocation.vk.df().QueuePresentKHR(
        invocation.queue, &presentInfo
    );
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");

    logSlowPresentOperation(
        "present-total", this->frameState.realFrameIndex,
        this->frameState.sequenceIndex, invocation.started, result
    );
    this->frameState.realFrameIndex++;
    return result;
}

VkResult Swapchain::presentOriginalImage(
        const PresentInvocation& invocation,
        const VkSemaphore waitSemaphore, const void* nextChain) {
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
        const bool gamescopeHdrTransport) {
    PresentationFramePlan plan;
    plan.historyWarmupActive =
        this->recoveryState.historyWarmupRemaining > 0 ||
        (this->adaptiveScheduler &&
            this->adaptiveScheduler->historyWarmupActive());
    const bool schedulerEnabled = this->adaptiveScheduler.has_value();
    const auto adaptivePlan = schedulerEnabled &&
            !plan.historyWarmupActive
        ? this->adaptiveScheduler->planFrame(presentNow, false)
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
    plan.admittedGeneratedFrameCount = plan.requestedGeneratedFrames.size();
    plan.configuredAcquireTimeout = generatedImageAcquireTimeoutNs();
    return plan;
}

void Swapchain::reportAdaptiveDelivery(
        const PresentationFramePlan& plan,
        const size_t acceptedForPresentation) {
    if (!this->adaptiveScheduler || plan.requestedGeneratedFrames.empty())
        return;
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
        PresentationFramePlan& plan) {
    if (plan.requestedGeneratedFrames.empty() || plan.historyWarmupActive)
        return;

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
            logPressure = this->recoveryState.generatedImageAdmission.reportUnavailable();
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
    commandBuffer.begin(invocation.vk);
    commandBuffer.blitImage(invocation.vk,
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
        {swapchainImage, sourceImage.handle()},
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
    commandBuffer.end(invocation.vk);

    const auto sourceSubmitStarted = startPresentDiagnostic();
    std::vector<VkSemaphore> sourceWaitSemaphores(
        invocation.waitSemaphores.begin(), invocation.waitSemaphores.end()
    );
    commandBuffer.submit(invocation.vk,
        std::move(sourceWaitSemaphores), VK_NULL_HANDLE, 0,
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
    const uint64_t sourceTimelineValue = this->frameState.sequenceIndex - 1;
    auto& fallbackPass = this->passes.front();
    auto& fallbackSemaphores = this->postCopySemaphores.at(
        this->frameState.sequenceIndex % this->postCopySemaphores.size()
    );
    auto& fallbackSemaphore = fallbackSemaphores.second;

    auto& fallbackCommandBuffer = fallbackPass.commandBuffer;
    fallbackCommandBuffer.begin(invocation.vk);
    fallbackCommandBuffer.end(invocation.vk);
    fallbackCommandBuffer.submit(invocation.vk,
        {}, this->syncSemaphore->handle(), sourceTimelineValue,
        {fallbackSemaphore.handle()}, VK_NULL_HANDLE, 0,
        this->renderFence->handle()
    );
    this->frameState.renderFenceInFlight = true;

    try {
        this->instance.get().scheduleFrameHistory(this->ctx.get());
    } catch (const std::exception& error) {
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
            invocation, fallbackSemaphore.handle(), invocation.nextChain
        );
        if (fallbackResult != VK_SUCCESS &&
                fallbackResult != VK_SUBOPTIMAL_KHR) {
            throw ls::vulkan_error(
                fallbackResult, "vkQueuePresentKHR() failed"
            );
        }
        logSlowPresentOperation(
            "present-total", this->frameState.realFrameIndex, this->frameState.sequenceIndex,
            invocation.started, fallbackResult
        );
        this->frameState.realFrameIndex++;
        return fallbackResult;
    }

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
        invocation, fallbackSemaphore.handle(), invocation.nextChain
    );
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");

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
    for (size_t i = 0; i < plan.scheduledGeneratedFrames.size(); ++i) {
        auto& postCopy = this->postCopySemaphores.at(
            this->frameState.sequenceIndex % this->postCopySemaphores.size()
        );
        auto& destinationImage = this->destinationImages.at(i);
        auto& pass = this->passes.at(i);

        uint32_t acquiredImageIndex{};
        VkResult result{};
        if (gamescopeHdrTransport) {
            acquiredImageIndex = plan.preacquiredGeneratedImages.at(i);
            result = VK_SUCCESS;
        } else {
            const auto acquireStarted = startPresentDiagnostic();
            const uint64_t acquireTimeout = generatedImageAcquireTimeout(
                false, plan.configuredAcquireTimeout
            );
            result = invocation.vk.df().AcquireNextImageKHR(
                invocation.vk.dev(), invocation.swapchain,
                acquireTimeout, pass.acquireSemaphore.handle(),
                VK_NULL_HANDLE, &acquiredImageIndex
            );
            logSlowPresentOperation(
                "acquire-generated-image", this->frameState.realFrameIndex,
                this->frameState.sequenceIndex,
                acquireStarted, result, i, acquiredImageIndex
            );
        }

        if (plan.configuredAcquireTimeout &&
                (result == VK_TIMEOUT || result == VK_NOT_READY)) {
            // The explicit legacy timeout is an anti-freeze ceiling. Backend
            // work is already scheduled on this non-Gamescope path, so drain
            // its final timeline value without reclassifying the miss as an
            // adaptive timing discontinuity.
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

            auto& fallbackCommandBuffer = pass.commandBuffer;
            fallbackCommandBuffer.begin(invocation.vk);
            fallbackCommandBuffer.end(invocation.vk);
            fallbackCommandBuffer.submit(invocation.vk,
                {}, this->syncSemaphore->handle(),
                finalGeneratedTimelineValue,
                {fallbackSemaphore.handle()}, VK_NULL_HANDLE, 0,
                this->renderFence->handle()
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
                    ? invocation.nextChain : nullptr
            );
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
                throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");

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
        commandBuffer.begin(invocation.vk);
        commandBuffer.blitImage(invocation.vk,
            {
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
            },
            {destinationImage.handle(), acquiredSwapchainImage},
            destinationImage.getExtent(),
            {
                barrierHelper(acquiredSwapchainImage,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_MEMORY_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                ),
            }
        );

        std::vector<VkSemaphore> waitSemaphores{
            pass.acquireSemaphore.handle()
        };
        if (i) {
            const auto& previousPostCopy = this->postCopySemaphores.at(
                (this->frameState.sequenceIndex - 1) % this->postCopySemaphores.size()
            );
            waitSemaphores.push_back(previousPostCopy.second.handle());
        }
        std::vector<VkSemaphore> signalSemaphores{
            postCopy.first.handle(), postCopy.second.handle()
        };

        commandBuffer.end(invocation.vk);
        const auto generatedSubmitStarted = startPresentDiagnostic();
        commandBuffer.submit(invocation.vk,
            std::move(waitSemaphores),
            this->syncSemaphore->handle(), this->frameState.sequenceIndex,
            std::move(signalSemaphores), VK_NULL_HANDLE, 0,
            i == plan.scheduledGeneratedFrames.size() - 1
                ? this->renderFence->handle() : VK_NULL_HANDLE
        );
        if (i == plan.scheduledGeneratedFrames.size() - 1)
            this->frameState.renderFenceInFlight = true;
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
        this->gamescopeDetected ? invocation.nextChain : nullptr
    );
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw ls::vulkan_error(result, "vkQueuePresentKHR() failed");

    this->reportAdaptiveDelivery(
        plan, plan.scheduledGeneratedFrames.size()
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
    if (!this->profile.frame_generation_enabled ||
            !this->colorPipeline.generationSupported) {
        return this->presentNativeFrame(invocation);
    }
    if (!this->recoverBackendIfReady(vk))
        return this->presentNativeFrame(invocation);

    const auto swapchainImage = this->info.images.at(imageIndex);
    // Presentation counters continue across live-off intervals, while the
    // backend's temporal history does not. Index sources by frames actually
    // submitted to the backend so native-only frames cannot invert history.
    const auto& sourceImage = this->sourceImages.at(
        this->frameState.backendFrameIndex % 2
    );
    auto plan = this->prepareFramePlan(presentNow, gamescopeHdrTransport);

    // The Gamescope HDR bridge is native-first: never hold the application's
    // real frame behind unfinished private work. Ordered SDR retains the
    // synchronous FIFO/fence behavior.
    if (!this->generationPipelineReady(
            vk, gamescopeHdrTransport, plan, presentNow)) {
        return this->presentNativeFrame(invocation);
    }

    if (gamescopeHdrTransport)
        this->preacquireGeneratedImages(invocation, plan);
    const size_t scheduledGeneratedFrameCount = gamescopeHdrTransport
        ? plan.admittedGeneratedFrameCount
        : plan.requestedGeneratedFrames.size();
    plan.scheduledGeneratedFrames = scheduleAdmittedGeneratedFrames(
        plan.requestedGeneratedFrames, scheduledGeneratedFrameCount
    );
    const bool bypassGeneratedFrames = plan.historyWarmupActive ||
        plan.scheduledGeneratedFrames.empty();
    if (plan.historyWarmupActive && !this->adaptiveScheduler)
        this->diagnosticsState.fixedSkippedFrames +=
            plan.requestedGeneratedFrames.size();

    // Resolve previous application-device work before scheduling another
    // backend frame. If the fence budget is missed, no new backend work has
    // been created and the current game image can be presented natively.
    if (!this->prepareRenderFence(vk)) {
        this->handleRenderFenceBudgetMiss(plan);
        return this->presentNativeFrame(invocation);
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
        logSlowPresentOperation(
            "schedule-frames", this->frameState.realFrameIndex,
            this->frameState.sequenceIndex, scheduleStarted
        );
    }

    this->submitSourceCopy(invocation, swapchainImage, sourceImage);
    if (bypassGeneratedFrames)
        return this->presentHistoryOnly(invocation, plan);
    return this->presentGeneratedFrames(
        invocation, plan, gamescopeHdrTransport
    );
}
