/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "swapchain/swapchain.hpp"
#include "swapchain/present/internal.hpp"
#include "swapchain/retirement.hpp"
#include "adaptive_scheduler.hpp"
#include "gamescope_scaling_surface.hpp"
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

VkResult Swapchain::queuePresentWithRetirementFence(
        const vk::Vulkan& vk, const VkQueue queue,
        const VkPresentInfoKHR& incomingPresentInfo) {
    VkPresentInfoKHR presentInfo = incomingPresentInfo;
    // Use the effective policy, not this frame's batch size: Fractional and
    // temporary native relief still belong to the active generated timeline.
    const bool generationEnabled = effectiveFrameGenerationEnabled(
        this->profile, this->gamescopeRefreshHz);
    VkPresentTimeGOOGLE presentTime{};
    VkPresentTimesInfoGOOGLE presentTimes{
        .sType = VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE,
        .pNext = presentInfo.pNext,
        .swapchainCount = 1,
        .pTimes = &presentTime,
    };
    if (this->wsiPresentTimingQuery && this->privateOrderedTransport &&
            !this->info.gamescopeScalingSurface &&
            presentInfo.swapchainCount == 1 &&
            !hasPresentTiming(presentInfo.pNext) &&
            this->gamescopeRefreshHz.value_or(0) > 0) {
        const auto slot = this->wsiPresentTimeline.schedule(
            DiagnosticsClock::now(),
            gamescopeBridgeOutputFps(this->profile, *this->gamescopeRefreshHz),
            *this->gamescopeRefreshHz, this->bridgeOutputBatchSize,
            generationEnabled);
        if (slot) {
            this->orderedPresentationDeadline = slot->presentAt;
            // Google timing creates feedback even when diagnostics are off.
            // Drain our namespace in bounded batches so old WSI versions
            // cannot retain a history proportional to session length.
            if (++this->wsiTimingPresentsSinceDrain >= 32) {
                this->wsiTimingPresentsSinceDrain = 0;
                if (!discardOwnedDisplayTiming(this->wsiPresentTimingQuery,
                        vk.dev(), presentInfo.pSwapchains[0])) {
                    this->wsiPresentTimingQuery = nullptr;
                    return this->queuePresentWithRetirementFence(
                        vk, queue, incomingPresentInfo);
                }
            }
            std::this_thread::sleep_until(slot->submitAt);
            presentTime.desiredPresentTime = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    slot->presentAt.time_since_epoch()).count());
            presentInfo.pNext = &presentTimes;
            if (!this->wsiPresentTimingLogged) {
                std::cerr << "MAKO Renderer: Gamescope WSI ordered output timing enabled; "
                             "owner=renderer; transport=VK_GOOGLE_display_timing\n";
                this->wsiPresentTimingLogged = true;
            }
        }
    }
    this->lastLowerPresentRetirementProtected = false;
    // The protocol mode applies to one surface commit. Preparing only the
    // application's outer present would leave the remaining generated/real
    // outputs unannotated and allow the compositor to replace them.
    if (this->info.gamescopeScalingSurface) {
        for (uint32_t i = 0; i < presentInfo.swapchainCount; ++i) {
            const auto prepared = this->info.gamescopeScalingSurface->preparePresent(
                    this->info.surface, presentInfo.pSwapchains[i],
                    gamescopeBridgeOutputFps(this->profile,
                        this->gamescopeRefreshHz.value_or(0)),
                    this->gamescopeRefreshHz.value_or(0),
                    this->bridgeOutputBatchSize, generationEnabled,
                    this->frameState.realFrameIndex,
                    &this->orderedPresentationDeadline);
            if (prepared != VK_SUCCESS)
                return completeRejectedPresent(vk.df().QueueSubmit,
                    queue, presentInfo, prepared);
        }
    }
    if (this->presentRetirementFences.empty() ||
            presentInfo.swapchainCount != 1 ||
            !presentInfo.pImageIndices) {
        return vk.df().QueuePresentKHR(queue, &presentInfo);
    }

    if (const auto* upstreamFence =
            findSwapchainPresentFenceInfo(presentInfo.pNext)) {
        // Exactly one present fence can be associated with each swapchain in
        // a present operation. Preserve the upstream owner's fence unchanged.
        // For the final lower present it is already the spec-defined proof
        // that the caller may destroy this swapchain; MAKO retains only its
        // own earlier fences and the compositor grace period after destroy.
        if (!this->externalPresentFenceLogged) {
            std::cerr << "MAKO Renderer: upstream presentation fence observed; "
                         "preserving owner and enabling guarded live resource "
                         "recreation on protected final presents\n";
            this->externalPresentFenceLogged = true;
        }
        const auto result = vk.df().QueuePresentKHR(queue, &presentInfo);
        this->lastLowerPresentRetirementProtected =
            upstreamPresentFenceProtectsSwapchain(upstreamFence) &&
            presentFenceWillSignal(result);
        return result;
    }

    const uint32_t imageIndex = presentInfo.pImageIndices[0];
    if (imageIndex >= this->presentRetirementFences.size())
        return vk.df().QueuePresentKHR(queue, &presentInfo);

    auto& slot = this->presentRetirementFences.at(imageIndex);
    try {
        if (slot.associated) {
            if (!slot.fence.wait(vk, 0)) {
                if (!this->presentRetirementBusyLogged) {
                    std::cerr << "MAKO Renderer: presentation retirement fence "
                                 "remained busy after image reacquisition; "
                                 "this present will not trigger live recreation\n";
                    this->presentRetirementBusyLogged = true;
                }
                return vk.df().QueuePresentKHR(queue, &presentInfo);
            }
            slot.associated = false;
        }
        if (slot.used)
            slot.fence.reset(vk);
    } catch (const std::exception& error) {
        std::cerr << "MAKO Renderer: presentation retirement fence preparation "
                     "failed; this present will not trigger live recreation: "
                  << error.what() << '\n';
        return vk.df().QueuePresentKHR(queue, &presentInfo);
    }

    const VkFence fence = slot.fence.handle();
    const VkSwapchainPresentFenceInfoEXT fenceInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,
        .pNext = presentInfo.pNext,
        .swapchainCount = 1,
        .pFences = &fence,
    };
    auto protectedPresentInfo = presentInfo;
    protectedPresentInfo.pNext = &fenceInfo;
    const auto result = vk.df().QueuePresentKHR(queue, &protectedPresentInfo);
    slot.used = true;
    slot.associated = presentFenceWillSignal(result);
    this->lastLowerPresentRetirementProtected = slot.associated;
    if (slot.associated)
        this->presentRetirementBusyLogged = false;
    return result;
}

bool Swapchain::waitForPresentRetirement(
        const vk::Vulkan& vk, const uint64_t timeoutNs) {
    const auto started = std::chrono::steady_clock::now();
    for (auto& slot : this->presentRetirementFences) {
        if (!slot.associated)
            continue;

        uint64_t remaining = timeoutNs;
        if (timeoutNs != UINT64_MAX) {
            const auto elapsed = std::chrono::duration_cast<
                std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started
                ).count();
            if (elapsed >= static_cast<int64_t>(timeoutNs))
                remaining = 0;
            else
                remaining = timeoutNs - static_cast<uint64_t>(elapsed);
        }
        try {
            if (!slot.fence.wait(vk, remaining))
                return false;
        } catch (const std::exception& error) {
            std::cerr << "MAKO Renderer: presentation retirement fence wait "
                         "failed: " << error.what() << '\n';
            return false;
        }
        slot.associated = false;
    }
    if (timeoutNs == 0) {
        // Presentation can finish while history-only backend work is still
        // running. Do not enter closeContext's bounded wait from a present.
        try {
            if (!this->sourceImages.empty() &&
                    (!this->instance ||
                     !this->instance->contextReady(this->ctx.get())))
                return false;
            if (this->frameState.renderFenceInFlight &&
                    !this->renderFence->wait(vk, 0))
                return false;
            for (const auto& pass : this->spatialScalingPasses) {
                if (pass.completionInFlight &&
                        !pass.completionFence.wait(vk, 0))
                    return false;
            }
        } catch (const std::exception& error) {
            std::cerr << "MAKO Renderer: private work retirement poll failed: "
                      << error.what() << '\n';
            return false;
        }
    }
    return true;
}

VkResult Swapchain::retireAcquiredImagesAndPresent(const vk::Vulkan& vk,
        const VkQueue queue, const VkSwapchainKHR swapchain,
        const void* nextChain, const uint32_t originalImageIndex,
        const std::span<const VkSemaphore> applicationWaitSemaphores,
        const std::span<const uint32_t> acquiredImageIndices,
        const VkImage originalImage) {
    if (acquiredImageIndices.empty())
        throw ls::error("attempted to retire an empty acquired-image batch");

    VkSemaphore spatialScalingReady = VK_NULL_HANDLE;
    if (this->spatialScaler) {
        auto& scalingPass = this->spatialScalingPasses.at(
            originalImageIndex
        );
        this->prepareSpatialScalingPass(vk, scalingPass);
        scalingPass.commandBuffer.begin(vk);
        this->spatialScaler->record(
            vk, scalingPass.commandBuffer, originalImage
        );
        scalingPass.commandBuffer.end(vk);
        scalingPass.commandBuffer.submit(
            vk, applicationWaitSemaphores, VK_NULL_HANDLE, 0,
            std::array{scalingPass.readySemaphore.handle()},
            VK_NULL_HANDLE, 0, scalingPass.completionFence.handle(), queue
        );
        scalingPass.completionInFlight = true;
        spatialScalingReady = scalingPass.readySemaphore.handle();
    }

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
            if (spatialScalingReady != VK_NULL_HANDLE) {
                waits.push_back(spatialScalingReady);
            } else {
                waits.insert(
                    waits.end(), applicationWaitSemaphores.begin(),
                    applicationWaitSemaphores.end()
                );
            }
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
        const auto acquiredResult = this->queuePresentWithRetirementFence(
            vk, queue, acquiredPresentInfo
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
    const auto result = this->queuePresentWithRetirementFence(
        vk, queue, originalPresentInfo
    );
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
