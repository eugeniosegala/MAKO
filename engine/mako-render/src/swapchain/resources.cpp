/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "swapchain/swapchain.hpp"
#include "present_diagnostics.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/file_descriptors.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

using namespace mako;
using namespace mako::layer;

namespace {
    using DiagnosticsClock = present_diagnostics::Clock;
    constexpr auto privateResourceDrainBudget =
        std::chrono::milliseconds(50);

    void logPrivateFrameGenerationMemory(const vk::Vulkan& vk,
            const uint64_t contextId, const uint64_t stateRevision,
            const std::string_view operation) {
        const auto memory = vk.deviceMemorySnapshot();
        std::clog << "MAKO Renderer: renderer-memory operation=" << operation
                  << " context=" << contextId
                  << " state_revision=" << stateRevision
                  << " live_internal_bytes=" << memory.internal.bytes
                  << " live_internal_allocations="
                  << memory.internal.allocations
                  << " live_exported_bytes=" << memory.exported.bytes
                  << " live_exported_allocations="
                  << memory.exported.allocations
                  << " peak_internal_bytes=" << memory.peakInternal.bytes
                  << " peak_internal_allocations="
                  << memory.peakInternal.allocations
                  << " peak_exported_bytes=" << memory.peakExported.bytes
                  << " peak_exported_allocations="
                  << memory.peakExported.allocations << '\n';
    }
}

bool Swapchain::directSpatialFrameGenerationOutputSupported(
        const vk::Vulkan& vk,
        const VkFormat format,
        const SpatialFramePipelinePlacement placement,
        const bool spatialScalingActive) {
    if (!directSpatialFrameGenerationOutputEligible(
            placement, spatialScalingActive, 2)) {
        return false;
    }
    return vk.supportsExternalImageFormat(
        format,
        frameGenerationSourceImageUsage(placement, true),
        VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT
    );
}

SwapchainColorPipeline Swapchain::initialColorPipeline(
        const VkFormat format, const VkColorSpaceKHR colorSpace,
        const std::optional<bool> gamescopeHdrActive,
        const bool gamescopeDetected,
        const bool hdrExposureDisabled) {
    auto pipeline = classifySwapchainColor(
        format, colorSpace, gamescopeHdrActive.value_or(false)
    );
    if (hdrExposureDisabled && pipeline.hdr) {
        pipeline.generationSupported = false;
        pipeline.name = "hdr-exposure-disabled";
        pipeline.reason =
            "HDR frame generation is disabled for this process";
        return pipeline;
    }
    if (gamescopeDetected && !gamescopeHdrActive &&
            pipeline.encoding == backend::FrameEncoding::SdrHighPrecision) {
        pipeline.generationSupported = false;
        pipeline.name = "gamescope-hdr-pending";
        pipeline.reason =
            "Gamescope HDR state is not confirmed; real-frame passthrough retained";
    }
    return pipeline;
}

void Swapchain::selectPackedHdr10Transport(const vk::Vulkan& vk,
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

void Swapchain::ensureFrameGenerationExecutionResources(
        const vk::Vulkan& vk) {
    if (!this->renderCommandBuffer.has_value())
        this->renderCommandBuffer.emplace(vk);
    if (!this->renderFence.has_value())
        this->renderFence.emplace(vk);

    // These objects are WSI-facing and intentionally remain stable while the
    // full-resolution images and backend context are replaced. Preallocating
    // four pass slots avoids retiring binary semaphores that may still be
    // referenced by a previously queued presentation.
    while (this->passes.size() < GeneratedFramePlan::capacity) {
        this->passes.emplace_back(RenderPass{
            .commandBuffer = vk::CommandBuffer(vk),
            .acquireSemaphore = vk::Semaphore(vk),
        });
    }
    const size_t semaphoreFrames = std::max(
        this->info.images.size(), GeneratedFramePlan::capacity + 2
    );
    while (this->postCopySemaphores.size() < semaphoreFrames) {
        this->postCopySemaphores.emplace_back(
            vk::Semaphore(vk), vk::Semaphore(vk)
        );
    }
}

Swapchain::FrameGenerationResources
Swapchain::buildFrameGenerationResources(const vk::Vulkan& vk,
        const SwapchainColorPipeline& pipeline,
        const ls::GameConf& resourceProfile) {
    if (!this->instance)
        throw ls::error("the frame-generation backend is unavailable");
    if (!pipeline.generationSupported)
        throw ls::error("the colour pipeline does not support frame generation");

    auto& backendInstance = *this->instance;
    const VkExtent2D extent = frameGenerationExtent(
        this->spatialFramePipelinePlacement,
        this->info.applicationExtent, this->info.extent
    );
    std::vector<int> sourceFds(2, -1);
    ls::FileDescriptorScope sourceScope{sourceFds};
    std::vector<int> destinationFds(generatedFrameCapacityForProfile(resourceProfile), -1);
    ls::FileDescriptorScope destinationScope{destinationFds};
    FrameGenerationResources resources;
    resources.sourceImages.reserve(sourceFds.size());
    resources.destinationImages.reserve(destinationFds.size());
    const auto sourceImageUsage = frameGenerationSourceImageUsage(
        this->spatialFramePipelinePlacement,
        directSpatialFrameGenerationOutputSupported(
            vk, pipeline.exchangeFormat,
            this->spatialFramePipelinePlacement,
            this->spatialScaler.has_value()
        )
    );
    for (int& fd : sourceFds) {
        resources.sourceImages.emplace_back(vk,
            extent, pipeline.exchangeFormat,
            sourceImageUsage,
            std::nullopt, &fd);
    }
    for (int& fd : destinationFds) {
        resources.destinationImages.emplace_back(vk,
            extent, pipeline.exchangeFormat,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &fd);
    }

    int syncFd{-1};
    ls::FileDescriptorScope syncScope{{&syncFd, 1}};
    resources.syncSemaphore.emplace(vk, 0, std::nullopt, &syncFd);
    resources.context = ls::owned_ptr<ls::R<backend::Context>>(
        new ls::R<backend::Context>(backendInstance.openContext(
            (sourceScope.release(), std::pair{sourceFds[0], sourceFds[1]}),
            (destinationScope.release(), destinationFds),
            (syncScope.release(), syncFd),
            extent.width, extent.height, pipeline.encoding,
            1.0F / ls::effectiveFlowScale(resourceProfile),
            ls::effectivePerformanceMode(resourceProfile)
        )),
        [backend = &backendInstance](ls::R<backend::Context>& context) {
            backend->closeContext(context);
        }
    );
    backend::makeLeaking();
    return resources;
}

void Swapchain::commitFrameGenerationResources(
        FrameGenerationResources resources,
        const ls::GameConf& resourceProfile,
        const std::string_view reason) {
    // Old backend work and application-device copies are proven idle by the
    // caller. The context is declared last in this aggregate and therefore
    // closes before its imported images and shared timeline are destroyed.
    FrameGenerationResources retiring;
    retiring.sourceImages = std::move(this->sourceImages);
    retiring.destinationImages = std::move(this->destinationImages);
    retiring.syncSemaphore = std::move(this->syncSemaphore);
    retiring.context = std::move(this->ctx);

    this->sourceImages = std::move(resources.sourceImages);
    this->destinationImages = std::move(resources.destinationImages);
    this->syncSemaphore = std::move(resources.syncSemaphore);
    this->ctx = std::move(resources.context);

    this->profile.flow_scale = resourceProfile.flow_scale;
    this->profile.performance_mode = resourceProfile.performance_mode;
    this->profile.adaptive = resourceProfile.adaptive;
    this->profile.multiplier = resourceProfile.multiplier;
    this->profile.adaptive_max_multiplier =
        resourceProfile.adaptive_max_multiplier;

    this->frameState.backendTimelineIndex = 1;
    this->frameState.backendFrameIndex = 0;
    this->frameState.renderFenceInFlight = false;
    this->recoveryState.backendPending = false;
    this->recoveryState.generatedImageAdmission.reset();
    this->recoveryState.orderedAcquireRecovery.reset();
    this->recoveryState.pipelineBusyRecovery.reset();
    this->recoveryState.lowerPresentStallRecovery.reset();
    this->fixedRefreshBudget.reset();
    this->realFramePacer.reset();
    this->smoothCadenceBaseCap.reset();
    this->smoothCadencePacerHandoff.reset();
    this->configuredFixedGeneratedFrames = fixedGeneratedFrameCount(
        this->profile.multiplier, this->destinationImages.size()
    );
    this->diagnosticsState.fixedWindowStarted.reset();
    this->diagnosticsState.fixedRealFrames = 0;
    this->diagnosticsState.fixedGeneratedFrames = 0;
    this->diagnosticsState.fixedSkippedFrames = 0;

    if (!this->resetGenerationScheduler(
            DiagnosticsClock::now(), reason)) {
        this->recoveryState.historyWarmupRemaining =
            AdaptiveScheduler::historyWarmupFrameCount();
    }
    this->ensureHistoryWarmup();
}

void Swapchain::rebuildPrivateResources(const vk::Vulkan& vk,
        SwapchainColorPipeline pipeline,
        const ls::GameConf& resourceProfile) {
    bool applicationPackedHdr10Supported = false;
    bool backendPackedHdr10Supported = false;
    if (this->instance) {
        selectPackedHdr10Transport(
            vk, *this->instance, pipeline,
            applicationPackedHdr10Supported, backendPackedHdr10Supported
        );
    }

    this->ensureFrameGenerationExecutionResources(vk);
    FrameGenerationResources replacement;
    if (this->instance && pipeline.generationSupported) {
        replacement = this->buildFrameGenerationResources(
            vk, pipeline, resourceProfile
        );
    }
    this->colorPipeline = std::move(pipeline);
    this->clearDirectSpatialFrameGenerationOutputs();
    this->commitFrameGenerationResources(
        std::move(replacement), resourceProfile, "hdr-private-transition"
    );
    if (this->spatialScaler) {
        this->configureDirectSpatialFrameGenerationOutputs(
            vk, *this->spatialScaler, this->sourceImages,
            "hdr-private-transition"
        );
    }
    if (this->preparedSpatialScaler) {
        this->configureDirectSpatialFrameGenerationOutputs(
            vk, *this->preparedSpatialScaler, this->sourceImages,
            "hdr-private-transition-prepared-scaler"
        );
    }

    std::cerr << "MAKO Renderer: swapchain colour pipeline transitioned in place: mode="
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
        const VkExtent2D extent = this->info.extent;
        const uint64_t transportImageCount = 2 +
            generatedFrameCapacityForProfile(resourceProfile);
        const uint64_t floatTransportBytes =
            static_cast<uint64_t>(extent.width) * extent.height *
            transportImageCount * 8;
        const uint64_t selectedTransportBytes =
            static_cast<uint64_t>(extent.width) * extent.height *
            transportImageCount *
            transportBytesPerPixel(this->colorPipeline.encoding);
        std::cerr << "MAKO Renderer: HDR10 transport: mode="
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
    if (!liveGamescopeHdrReclassificationAllowed(
            this->spatialScaler.has_value())) {
        this->colorTransitionState.pendingGamescopeHdrActive.reset();
        this->colorTransitionState.pendingHdrStateRevision = 0;
        this->colorTransitionState.retryAt.reset();
        return true;
    }
    if (!this->colorTransitionState.pendingGamescopeHdrActive)
        return true;

    const auto now = DiagnosticsClock::now();
    if (this->colorTransitionState.retryAt && now < *this->colorTransitionState.retryAt)
        return false;

    const bool resourcesAvailable = this->sourceImages.size() == 2 &&
        !this->destinationImages.empty() && this->syncSemaphore.has_value();
    if (resourcesAvailable) {
        try {
            if (!this->instance ||
                    !this->instance->contextReady(this->ctx.get()))
                return false;
            if (this->frameState.renderFenceInFlight &&
                    !this->renderFence->wait(vk, 0))
                return false;
        } catch (const std::exception& error) {
            std::cerr << "MAKO Renderer: private colour transition readiness poll "
                         "failed; real-frame passthrough retained: "
                      << error.what() << '\n';
            this->colorTransitionState.retryAt = now + std::chrono::seconds(1);
            return false;
        }
    }

    auto desiredPipeline = classifySwapchainColor(
        this->info.format, this->info.colorSpace,
        *this->colorTransitionState.pendingGamescopeHdrActive
    );
    try {
        this->rebuildPrivateResources(
            vk, std::move(desiredPipeline), this->profile
        );
    } catch (const std::exception& error) {
        std::cerr << "MAKO Renderer: private colour transition failed; real-frame "
                     "passthrough retained and retry scheduled: "
                  << error.what() << '\n';
        this->colorTransitionState.retryAt = now + std::chrono::seconds(5);
        return false;
    }

    // A prepared frame-generation context encodes the colour transport that
    // was active when it was built. If HDR reclassification commits first,
    // discard that private candidate and prepare the same last-value-wins
    // request again against the newly active transport.
    if (this->frameGenerationTransition.pendingRequest()) {
        this->preparedFrameGenerationResources.reset();
        this->frameGenerationTransition.restartPreparation(
            std::chrono::milliseconds(500), now
        );
    }
    this->runtimeStatusState.error.reset();
    this->publishRuntimeStatus("hdr-mode");

    if (present_diagnostics::enabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=runtime-transition-applied"
                  << " context=" << this->diagnosticsState.contextId
                  << " state_revision=" << this->colorTransitionState.pendingHdrStateRevision
                  << " reason=hdr-mode"
                  << " transition=private-context"
                  << " hdr=" << this->colorPipeline.hdr << '\n';
    }
    this->colorTransitionState.pendingGamescopeHdrActive.reset();
    this->colorTransitionState.pendingHdrStateRevision = 0;
    this->colorTransitionState.retryAt.reset();
    return true;
}

bool Swapchain::applyPendingFrameGenerationResources(
        const vk::Vulkan& vk) {
    if (!this->frameGenerationTransition.pendingRequest())
        return true;

    const auto now = DiagnosticsClock::now();
    if (this->frameGenerationTransition.beginPreparation(now)) {
        try {
            this->ensureFrameGenerationExecutionResources(vk);
            this->preparedFrameGenerationResources =
                this->buildFrameGenerationResources(
                    vk, this->colorPipeline,
                    this->frameGenerationTransition.value().profile
                );
            this->frameGenerationTransition.prepared();
            logPrivateFrameGenerationMemory(
                vk, this->diagnosticsState.contextId,
                this->frameGenerationTransition.stateRevision(),
                "private-context-prepared"
            );
            this->publishRuntimeStatus("frame-generation-resources");
            if (present_diagnostics::enabled()) {
                const auto& requested =
                    this->frameGenerationTransition.value().profile;
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=runtime-transition-prepared"
                          << " context=" << this->diagnosticsState.contextId
                          << " state_revision="
                          << this->frameGenerationTransition.stateRevision()
                          << " reason=frame-generation-resources"
                          << " requested_generated_capacity="
                          << generatedFrameCapacityForProfile(requested)
                          << " requested_flow_scale="
                          << ls::effectiveFlowScale(requested)
                          << " requested_lighter_model="
                          << ls::effectivePerformanceMode(requested)
                          << " action=drain-private-work\n";
            }
        } catch (const std::exception& error) {
            this->preparedFrameGenerationResources.reset();
            this->frameGenerationTransition.failed(
                std::chrono::seconds(5), now
            );
            this->runtimeStatusState.error = error.what();
            this->publishRuntimeStatus("frame-generation-resources");
            std::cerr << "MAKO Renderer: private frame-generation resource "
                         "transition failed; the previous resources remain "
                         "active and retry is scheduled: "
                      << error.what() << '\n';
            if (present_diagnostics::enabled()) {
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=runtime-transition-failed"
                          << " context=" << this->diagnosticsState.contextId
                          << " state_revision="
                          << this->frameGenerationTransition.stateRevision()
                          << " reason=frame-generation-resources"
                          << " retry_ms=5000"
                          << " active_generated_capacity="
                          << this->destinationImages.size()
                          << " action=retain-active-resources\n";
            }
            return true;
        }
    }

    if (!this->frameGenerationTransition.draining())
        return true;
    if (!this->preparedFrameGenerationResources)
        return true;

    try {
        if (!this->instance || !this->instance->contextReady(this->ctx.get()))
            return false;
        if (this->frameState.renderFenceInFlight) {
            if (!this->renderFence->wait(vk, 0))
                return false;
            this->frameState.renderFenceInFlight = false;
        }
        if (this->spatialScaler && !this->spatialScalingPassesReady(vk))
            return false;
    } catch (const std::exception& error) {
        std::cerr << "MAKO Renderer: private frame-generation drain poll failed; "
                     "native presentation retained: "
                  << error.what() << '\n';
        return false;
    }

    const auto committedProfile =
        this->frameGenerationTransition.value().profile;
    auto replacement = std::move(*this->preparedFrameGenerationResources);
    this->preparedFrameGenerationResources.reset();
    this->clearDirectSpatialFrameGenerationOutputs();
    this->commitFrameGenerationResources(
        std::move(replacement), committedProfile,
        "private-resource-transition"
    );
    if (this->spatialScaler) {
        this->configureDirectSpatialFrameGenerationOutputs(
            vk, *this->spatialScaler, this->sourceImages,
            "private-resource-transition"
        );
    }
    if (this->preparedSpatialScaler) {
        this->configureDirectSpatialFrameGenerationOutputs(
            vk, *this->preparedSpatialScaler, this->sourceImages,
            "private-resource-transition-prepared-scaler"
        );
    }
    const auto committedRevision =
        this->frameGenerationTransition.committed();
    logPrivateFrameGenerationMemory(
        vk, this->diagnosticsState.contextId,
        committedRevision.value_or(0), "private-context-applied"
    );
    this->runtimeStatusState.error.reset();
    this->publishRuntimeStatus("frame-generation-resources");

    std::cerr << "MAKO Renderer: frame-generation resources changed live: "
              << "generated_capacity=" << this->destinationImages.size()
              << "; flow_scale=" << ls::effectiveFlowScale(this->profile)
              << "; lighter_model="
              << ls::effectivePerformanceMode(this->profile)
              << "; transition=private-context\n";
    if (present_diagnostics::enabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=runtime-transition-applied"
                  << " context=" << this->diagnosticsState.contextId
                  << " state_revision=" << committedRevision.value_or(0)
                  << " reason=frame-generation-resources"
                  << " transition=private-context"
                  << " effective_flow_scale="
                  << ls::effectiveFlowScale(this->profile)
                  << " lighter_model="
                  << ls::effectivePerformanceMode(this->profile)
                  << " generated_frame_capacity="
                  << this->destinationImages.size()
                  << " history_warmup_frames="
                  << AdaptiveScheduler::historyWarmupFrameCount()
                  << '\n';
    }
    return true;
}

void Swapchain::applyPendingSpatialScaler(const vk::Vulkan& vk) {
    if (!this->spatialTransition.pendingRequest() || !this->spatialScaler)
        return;

    const auto now = DiagnosticsClock::now();
    if (this->spatialTransition.beginPreparation(now)) {
        try {
            const auto& requested = this->spatialTransition.value();
            this->preparedSpatialScaler.emplace(
                vk, this->info.applicationExtent, this->info.extent,
                this->colorPipeline.exchangeFormat, requested.method,
                requested.sharpness, this->scalingShaderDll
            );
            this->configureDirectSpatialFrameGenerationOutputs(
                vk, *this->preparedSpatialScaler, this->sourceImages,
                "spatial-scaler-prepared"
            );
            this->spatialTransition.prepared();
            this->publishRuntimeStatus("spatial-scaler");
        } catch (const std::exception& error) {
            std::cerr << "MAKO Renderer: spatial scaling model transition "
                         "failed; the previous model remains active and retry "
                         "is scheduled: "
                      << error.what() << '\n';
            this->preparedSpatialScaler.reset();
            this->spatialTransition.failed(std::chrono::seconds(5), now);
            this->runtimeStatusState.error = error.what();
            this->publishRuntimeStatus("spatial-scaler");
            if (present_diagnostics::enabled()) {
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=runtime-transition-failed"
                          << " context=" << this->diagnosticsState.contextId
                          << " state_revision="
                          << this->spatialTransition.stateRevision()
                          << " reason=spatial-scaler"
                          << " retry_ms=5000"
                          << " action=retain-active-resources\n";
            }
            return;
        }
    }
    if (!this->spatialTransition.draining() ||
            !this->preparedSpatialScaler) {
        return;
    }

    try {
        // In the post-FG placement, generated-image command buffers record
        // reconstruction with the active scaler and are retired by the main
        // render fence rather than a SpatialScalingPass fence. Do not replace
        // their pipelines, descriptors, or private images while that work is
        // still in flight. present() temporarily drains through the real-frame
        // path, so this nonblocking poll cannot be starved by another generated
        // batch using the old scaler.
        const auto generatedRenderDrainRequired =
            spatialScalerTransitionRequiresGeneratedRenderDrain(
                this->spatialFramePipelinePlacement
            );
        const auto generatedRenderWorkInFlight =
            generatedRenderDrainRequired &&
            this->frameState.renderFenceInFlight;
        if (generatedRenderWorkInFlight) {
            if (!this->renderFence->wait(vk, 0))
                return;
            this->frameState.renderFenceInFlight = false;
        }
        if (!this->spatialScalingPassesReady(vk))
            return;
        if (generatedRenderDrainRequired && present_diagnostics::enabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=runtime-transition-drain-ready"
                      << " context=" << this->diagnosticsState.contextId
                      << " reason=spatial-scaler"
                      << " generated_render_work_in_flight="
                      << generatedRenderWorkInFlight
                      << " action=replace-private-scaler\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "MAKO Renderer: private spatial-scaler drain poll failed; "
                     "the previous scaler remains active: "
                  << error.what() << '\n';
        return;
    }

    const auto requested = this->spatialTransition.value();
    *this->spatialScaler = std::move(*this->preparedSpatialScaler);
    this->preparedSpatialScaler.reset();
    this->profile.scaling_method = requested.method;
    this->profile.scaling_sharpness = requested.sharpness;
    const auto committedRevision = this->spatialTransition.committed();
    this->runtimeStatusState.error.reset();
    this->publishRuntimeStatus("spatial-scaler");
    try {
        this->ensureHistoryWarmup();
        this->fixedRefreshBudget.reset();
        this->recoveryState.fixedCadenceCollapseRecovery.reset();
        this->recoveryState.lowerPresentStallRecovery.reset();

        const auto activeMethod = this->spatialScaler->activeMethod();
        std::cerr << "MAKO Renderer: spatial scaling model changed live: "
                  << "requested_method="
                  << ls::scalingMethodName(requested.method)
                  << "; active_method="
                  << ls::scalingMethodName(activeMethod)
                  << "; sharpness=" << requested.sharpness
                  << "; transition=private-scaler\n";
        if (!this->spatialScaler->fallbackReason().empty()) {
            std::cerr << "MAKO Renderer: LS1 scaling unavailable; using MAKO "
                         "fallback: "
                      << this->spatialScaler->fallbackReason() << '\n';
        }
        if (present_diagnostics::enabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=runtime-transition-applied"
                      << " context=" << this->diagnosticsState.contextId
                      << " state_revision="
                      << committedRevision.value_or(0)
                      << " reason=spatial-scaler"
                      << " transition=private-context"
                      << " requested_method="
                      << ls::scalingMethodName(requested.method)
                      << " active_method="
                      << ls::scalingMethodName(activeMethod)
                      << " sharpness=" << requested.sharpness << '\n';
        }
    } catch (const std::exception& error) {
        // Resource construction and the atomic model swap have completed.
        // Post-commit scheduler housekeeping must not roll the active model
        // back or leave the transition permanently pending.
        std::cerr << "MAKO Renderer: spatial scaling model changed live, but "
                     "post-transition housekeeping reported: "
                  << error.what() << '\n';
    }
}

void Swapchain::prepareSpatialScalingPass(const vk::Vulkan& vk,
        SpatialScalingPass& pass) {
    if (pass.completionInFlight) {
        // Reacquiring the same WSI image means the lower present has already
        // consumed the ready semaphore. Its private compute submission should
        // therefore be complete; the fence makes that ownership explicit.
        static_cast<void>(pass.completionFence.wait(vk));
        pass.completionInFlight = false;
    }
    pass.completionFence.reset(vk);
}

bool Swapchain::spatialScalingPassesReady(const vk::Vulkan& vk) {
    const auto deadline = DiagnosticsClock::now() +
        privateResourceDrainBudget;
    for (auto& pass : this->spatialScalingPasses) {
        if (!pass.completionInFlight)
            continue;
        const auto now = DiagnosticsClock::now();
        if (now >= deadline)
            return false;
        const auto remaining = std::chrono::duration_cast<
            std::chrono::nanoseconds>(deadline - now);
        if (!pass.completionFence.wait(
                vk, static_cast<uint64_t>(remaining.count()))) {
            return false;
        }
        pass.completionInFlight = false;
    }
    return true;
}

void Swapchain::configureDirectSpatialFrameGenerationOutputs(
        const vk::Vulkan& vk, SpatialScaler& scaler,
        const std::span<const vk::Image> outputs,
        const std::string_view reason) noexcept {
    if (!directSpatialFrameGenerationOutputEligible(
            this->spatialFramePipelinePlacement,
            this->info.spatialScalingActive, outputs.size())) {
        scaler.clearDirectFrameGenerationOutputs();
        return;
    }
    if (!directSpatialFrameGenerationOutputSupported(
            vk, this->colorPipeline.exchangeFormat,
            this->spatialFramePipelinePlacement,
            this->info.spatialScalingActive)) {
        scaler.clearDirectFrameGenerationOutputs();
        std::cerr << "MAKO Renderer: spatial scaling FG source transport: "
                     "mode=copy-private-output; direct-output-unavailable="
                     "external-storage-format-unsupported"
                  << "; reason=" << reason << '\n';
        return;
    }

    try {
        std::vector<std::reference_wrapper<const vk::Image>> references;
        references.reserve(outputs.size());
        for (const auto& output : outputs)
            references.push_back(std::cref(output));
        scaler.configureDirectFrameGenerationOutputs(vk, references);
        std::cerr << "MAKO Renderer: spatial scaling FG source transport: "
                     "mode=direct-reconstruction; outputs="
                  << scaler.directFrameGenerationOutputCount()
                  << "; fallback=copy-private-output"
                  << "; reason=" << reason << '\n';
    } catch (const std::exception& error) {
        scaler.clearDirectFrameGenerationOutputs();
        std::cerr << "MAKO Renderer: spatial scaling FG source transport: "
                     "mode=copy-private-output; direct-output-unavailable="
                  << error.what() << "; reason=" << reason << '\n';
    }
}

void Swapchain::clearDirectSpatialFrameGenerationOutputs() noexcept {
    if (this->spatialScaler)
        this->spatialScaler->clearDirectFrameGenerationOutputs();
    if (this->preparedSpatialScaler)
        this->preparedSpatialScaler->clearDirectFrameGenerationOutputs();
}

bool Swapchain::updateGamescopeHdrState(
        const bool active, const uint64_t runtimeStateRevision) {
    if (!liveGamescopeHdrReclassificationAllowed(
            this->spatialScaler.has_value())) {
        return false;
    }
    const auto desiredPipeline = classifySwapchainColor(
        this->info.format, this->info.colorSpace, active
    );
    if (!this->colorTransitionState.pendingGamescopeHdrActive &&
            desiredPipeline.name == this->colorPipeline.name &&
            desiredPipeline.generationSupported ==
                this->colorPipeline.generationSupported) {
        return false;
    }

    this->colorTransitionState.pendingGamescopeHdrActive = active;
    this->colorTransitionState.pendingHdrStateRevision = runtimeStateRevision;
    this->colorTransitionState.retryAt.reset();

    if (present_diagnostics::enabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=runtime-transition-pending"
                  << " context=" << this->diagnosticsState.contextId
                  << " state_revision=" << runtimeStateRevision
                  << " reason=hdr-mode"
                  << " current=" << this->colorPipeline.name
                  << " requested=" << desiredPipeline.name
                  << " action=rebuild-private-context\n";
    }
    return true;
}
