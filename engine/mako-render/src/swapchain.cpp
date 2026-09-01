/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "swapchain.hpp"
#include "adaptive_scheduler.hpp"
#include "mako-backend/mako.hpp"
#include "mako-common/configuration/config.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/image.hpp"
#include "mako-common/vulkan/vulkan.hpp"
#include "layer_role.hpp"
#include "present_diagnostics.hpp"
#include "spatial_scaling_policy.hpp"
#include "swapchain_create_policy.hpp"

#include <algorithm>
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
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

using namespace mako;
using namespace mako::layer;

namespace {
    using DiagnosticsClock = present_diagnostics::Clock;
    using DiagnosticsContextScope = present_diagnostics::ContextScope;

    constexpr auto privateResourceDrainBudget =
        std::chrono::milliseconds(50);

    uint64_t allocateDiagnosticsContextId() {
        const auto contextId = present_diagnostics::allocateContextId();
        if constexpr (spatialScalingLayer)
            return contextId | uint64_t{0x80000000};
        return contextId;
    }

    bool presentDiagnosticsEnabled() {
        return present_diagnostics::enabled();
    }

    double presentDiagnosticsThresholdMs() {
        return present_diagnostics::thresholdMilliseconds();
    }

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

    size_t generatedFrameCapacity(const ls::GameConf& profile) {
        return generatedFrameCapacityForProfile(profile);
    }

    bool directSpatialFrameGenerationOutputSupported(
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

    SwapchainColorPipeline initialColorPipeline(
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


    std::optional<uint64_t> configuredGeneratedImageAcquireTimeoutNs() {
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
}

std::optional<uint64_t> Swapchain::generatedImageAcquireTimeoutNs() {
    return configuredGeneratedImageAcquireTimeoutNs();
}

bool layer::context_ModifySwapchainCreateInfo(const ls::GameConf& profile,
        uint32_t maxImages,
        VkSwapchainCreateInfoKHR& createInfo, const bool gamescopeHdrActive,
        const bool gamescopeDetected,
        const PresentationEnvironmentPolicy& presentationEnvironment,
        const bool frameGenerationInteropEnabled,
        const bool spatialScalingActive) {
    const auto colorPipeline = classifySwapchainColor(
        createInfo.imageFormat, createInfo.imageColorSpace,
        gamescopeHdrActive
    );
    const bool frameGenerationSupported =
        colorPipeline.generationSupported &&
        !(presentationEnvironment.hdrExposureDisabled && colorPipeline.hdr);
    const bool frameGenerationProvisioned =
        frameGenerationInteropEnabled && frameGenerationSupported;
    const bool hdrCapableSwapchain =
        !presentationEnvironment.hdrExposureDisabled &&
        (colorPipeline.hdr ||
            colorPipeline.encoding == backend::FrameEncoding::SdrHighPrecision);
    const bool orderedFrameGenerationTransport =
        frameGenerationProvisioned &&
        selectPresentationTransport(
            gamescopeDetected, hdrCapableSwapchain,
            presentationEnvironment
        ) == PresentationTransport::OrderedSdr;

    return applySwapchainCreateProvisioning(
        profile, maxImages, createInfo,
        frameGenerationProvisioned, spatialScalingActive,
        orderedFrameGenerationTransport
    );
}

Swapchain::Swapchain(const vk::Vulkan& vk, backend::Instance* backend,
            ls::GameConf profile, SwapchainInfo info,
            const std::optional<std::filesystem::path> scalingShaderDll,
            const std::optional<bool> gamescopeHdrActive,
            const bool gamescopeDetected,
            const bool hdrExposureDisabled,
            const std::optional<uint32_t> gamescopeRefreshHz,
            const uint64_t runtimeStateRevision,
            const bool swapchainMaintenance1Enabled) :
        instance(backend),
        gamescopeDetected(gamescopeDetected),
        privateOrderedTransport(info.privateOrderedTransport),
        gamescopeRefreshHz(gamescopeRefreshHz),
        colorPipeline(initialColorPipeline(
            info.format, info.colorSpace, gamescopeHdrActive, gamescopeDetected,
            hdrExposureDisabled
        )),
        scalingShaderDll(scalingShaderDll),
        profile(std::move(profile)), info(std::move(info)) {
    this->diagnosticsState.contextId = allocateDiagnosticsContextId();
    this->runtimeStatusPublisher = RuntimeStatusPublisher(
        this->diagnosticsState.contextId, layerRoleName
    );
    this->runtimeStatusState.requestedProfile = this->profile;
    this->runtimeStatusState.stateRevision = runtimeStateRevision;
    const DiagnosticsContextScope diagnosticsContext(
        this->diagnosticsState.contextId
    );
    const VkExtent2D extent = this->info.extent;

    if (swapchainMaintenance1Enabled && this->profile.scaling_enabled) {
        try {
            this->presentRetirementFences.reserve(this->info.images.size());
            for (size_t index = 0; index < this->info.images.size(); ++index) {
                static_cast<void>(index);
                this->presentRetirementFences.emplace_back(
                    PresentRetirementFence{.fence = vk::Fence(vk)}
                );
            }
            std::cerr << "MAKO Renderer: swapchain presentation retirement: "
                         "mode=maintenance1-per-image-fence; images="
                      << this->presentRetirementFences.size() << '\n';
        } catch (const std::exception& error) {
            this->presentRetirementFences.clear();
            std::cerr << "MAKO Renderer: swapchain presentation retirement "
                         "unavailable; live resource recreation will wait for "
                         "a natural swapchain boundary: "
                      << error.what() << '\n';
        }
    }

    if (this->info.spatialScalingActive) {
        if (!spatialScalingColorSupported(this->colorPipeline))
            throw ls::error(
                "spatial scaling requires a validated SDR colour pipeline"
            );
        if (sameExtent(this->info.applicationExtent, this->info.extent))
            throw ls::error(
                "spatial scaling source and presentation extents are identical"
            );

        this->spatialFramePipelinePlacement =
            combinedSpatialFramePipelineOwnedByLayer()
            ? selectSpatialFramePipelinePlacement(
                this->info.applicationExtent, this->info.extent
            )
            : SpatialFramePipelinePlacement::PostFrameGeneration;

        this->spatialScaler.emplace(
            vk, this->info.applicationExtent, this->info.extent,
            this->colorPipeline.exchangeFormat,
            ls::effectiveScalingMethod(this->profile),
            this->profile.scaling_sharpness,
            scalingShaderDll
        );
        this->spatialScalingPasses.reserve(this->info.images.size());
        for (size_t i = 0; i < this->info.images.size(); ++i) {
            static_cast<void>(i);
            this->spatialScalingPasses.emplace_back(SpatialScalingPass{
                .commandBuffer = vk::CommandBuffer(vk),
                .readySemaphore = vk::Semaphore(vk),
                .completionFence = vk::Fence(vk),
            });
        }
        const auto activeScalingMethod = this->spatialScaler->activeMethod();
        const double effectiveScalingFactor = std::min(
            static_cast<double>(this->info.extent.width) /
                static_cast<double>(this->info.applicationExtent.width),
            static_cast<double>(this->info.extent.height) /
                static_cast<double>(this->info.applicationExtent.height)
        );
        std::cerr << "MAKO Renderer: spatial scaling active: source="
                  << this->info.applicationExtent.width << 'x'
                  << this->info.applicationExtent.height
                  << "; presentation=" << this->info.extent.width << 'x'
                  << this->info.extent.height
                  << "; factor=" << this->profile.scaling_factor
                  << "; effective_factor=" << effectiveScalingFactor
                  << "; requested_method="
                  << ls::scalingMethodName(
                      this->spatialScaler->requestedMethod()
                  )
                  << "; active_method="
                  << ls::scalingMethodName(activeScalingMethod)
                  << "; sharpness=" << this->profile.scaling_sharpness
                  << "; ls1_model_variant=";
        if (!ls::licensedScalingModelRequested(activeScalingMethod))
            std::cerr << "none";
        else
            std::cerr << this->spatialScaler->ls1ModelVariant();
        std::cerr << "; ls1_translator="
                  << (this->spatialScaler->ls1Translator().empty()
                      ? "none" : this->spatialScaler->ls1Translator())
                  << "; ls1_dll_sha256="
                  << (this->spatialScaler->ls1DllSha256().empty()
                      ? "none" : this->spatialScaler->ls1DllSha256())
                  << "; ls1_resource_layout_sha256="
                  << (this->spatialScaler->ls1ResourceLayoutSha256().empty()
                      ? "none"
                      : this->spatialScaler->ls1ResourceLayoutSha256())
                  << "; working_format="
                  << static_cast<int>(this->colorPipeline.exchangeFormat)
                  << "; role=" << layerRoleName
                  << "; pipeline="
                  << spatialFramePipelinePlacementName(
                        this->spatialFramePipelinePlacement
                     )
                  << "; placement_reason="
                  << spatialFramePipelinePlacementReason(
                        this->spatialFramePipelinePlacement
                     )
                  << '\n';
        if (!this->spatialScaler->fallbackReason().empty()) {
            std::cerr << "MAKO Renderer: LS1 scaling unavailable; using MAKO "
                         "fallback: "
                      << this->spatialScaler->fallbackReason() << '\n';
        }
    }

    bool applicationPackedHdr10Supported = false;
    bool backendPackedHdr10Supported = false;
    if (backend) {
        selectPackedHdr10Transport(
            vk, *backend, this->colorPipeline,
            applicationPackedHdr10Supported, backendPackedHdr10Supported
        );
    }

    const auto initialGenerationPolicy = generationSchedulerPolicy(
        this->profile, this->gamescopeRefreshHz
    );
    const bool initialFrameGenerationEnabled = effectiveFrameGenerationEnabled(
        this->profile, this->gamescopeRefreshHz
    );
    const bool initialDynamicCadenceRecoveryActive =
        this->privateOrderedTransport && initialGenerationPolicy &&
        initialGenerationPolicy->dynamicCadenceRecovery;
    const bool initialFrameGenerationResourcesAvailable =
        backend != nullptr && this->colorPipeline.generationSupported;
    if (presentDiagnosticsEnabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: operation=runtime-state-applied"
                  << " context=" << this->diagnosticsState.contextId
                  << " role=" << layerRoleName
                  << " state_revision=" << runtimeStateRevision
                  << " frame_generation_enabled="
                  << this->profile.frame_generation_enabled
                  << " frame_generation_refresh_threshold="
                  << this->profile.frame_generation_refresh_threshold
                  << " effective_frame_generation_enabled="
                  << initialFrameGenerationEnabled
                  << " adaptive=" << this->profile.adaptive
                  << " target_fps=" << this->profile.target_fps
                  << " multiplier=" << this->profile.multiplier
                  << " base_fps_cap=" << this->profile.base_fps_cap
                  << " adaptive_auto_base_fps_cap="
                  << this->profile.adaptive_auto_base_fps_cap
                  << " effective_base_fps_cap="
                  << effectiveBaseFpsCap(this->profile)
                  << " adaptive_max_multiplier="
                  << this->profile.adaptive_max_multiplier
                  << " stable_cadence="
                  << this->profile.adaptive_stable_cadence
                  << " dynamic_cadence_recovery="
                  << this->profile.dynamic_cadence_recovery
                  << " effective_dynamic_cadence_recovery="
                  << initialDynamicCadenceRecoveryActive
                  << " effective_flow_scale="
                  << ls::effectiveFlowScale(this->profile)
                  << " lighter_model="
                  << ls::effectivePerformanceMode(this->profile)
                  << " frame_generation_resources_available="
                  << initialFrameGenerationResourcesAvailable
                  << " generated_frame_capacity="
                  << (initialFrameGenerationResourcesAvailable
                        ? generatedFrameCapacity(this->profile) : 0)
                  << " hdr=" << this->colorPipeline.hdr
                  << '\n';
    }

    if (!backend) {
        this->colorPipeline.generationSupported = false;
        this->colorPipeline.reason =
            "the frame-generation backend is unavailable";
    }

    std::cerr << "MAKO Renderer: swapchain colour pipeline: format="
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
    if (this->gamescopeDetected && this->privateOrderedTransport) {
        std::cerr << "MAKO Renderer: Gamescope SDR presentation transport: "
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

    if (!this->colorPipeline.generationSupported) {
        std::cerr << "MAKO Renderer: frame generation disabled for this swapchain: "
                  << this->colorPipeline.reason << '\n';
        this->publishRuntimeStatus("swapchain-create");
        return;
    }
    try {
        const VkExtent2D generationExtent = frameGenerationExtent(
            this->spatialFramePipelinePlacement,
            this->info.applicationExtent, this->info.extent
        );
        std::vector<int> sourceFds(2);
        std::vector<int> destinationFds(generatedFrameCapacity(this->profile));
        const auto sourceImageUsage = frameGenerationSourceImageUsage(
            this->spatialFramePipelinePlacement,
            directSpatialFrameGenerationOutputSupported(
                vk, this->colorPipeline.exchangeFormat,
                this->spatialFramePipelinePlacement,
                this->spatialScaler.has_value()
            )
        );

        this->sourceImages.reserve(sourceFds.size());
        for (int& fd : sourceFds)
            this->sourceImages.emplace_back(vk,
                generationExtent, this->colorPipeline.exchangeFormat,
                sourceImageUsage,
                std::nullopt, &fd);

        this->destinationImages.reserve(destinationFds.size());
        for (int& fd : destinationFds)
            this->destinationImages.emplace_back(vk,
                generationExtent, this->colorPipeline.exchangeFormat,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                std::nullopt, &fd);

        int syncFd{};
        this->syncSemaphore.emplace(vk, 0, std::nullopt, &syncFd);

        try {
            this->ctx = ls::owned_ptr<ls::R<backend::Context>>(
                new ls::R<backend::Context>(backend->openContext(
                    { sourceFds.at(0), sourceFds.at(1) }, destinationFds, syncFd,
                    generationExtent.width, generationExtent.height,
                    this->colorPipeline.encoding,
                    1.0F / ls::effectiveFlowScale(this->profile),
                    ls::effectivePerformanceMode(this->profile)
                )),
                [backend](ls::R<backend::Context>& ctx) {
                    backend->closeContext(ctx);
                }
            );

            // The backend's private VkDevice must outlive this layer on loader
            // combinations where destroying it during layer unload is unsafe.
            // This is process-lifetime retention, not active GPU work; see the
            // backend API contract before changing the teardown policy.
            backend::makeLeaking();
        } catch (const std::exception& e) {
            throw ls::error("failed to create swapchain context", e);
        }

        this->ensureFrameGenerationExecutionResources(vk);
        if (this->spatialScaler) {
            this->configureDirectSpatialFrameGenerationOutputs(
                vk, *this->spatialScaler, this->sourceImages,
                "swapchain-create"
            );
        }

        this->configuredFixedGeneratedFrames = fixedGeneratedFrameCount(
            this->profile.multiplier, this->destinationImages.size()
        );

        if (!initialFrameGenerationEnabled)
            std::cerr << "MAKO Renderer: frame generation is off; retained private "
                         "resources permit a live enable\n";

        const auto configuredAcquireTimeout =
            generatedImageAcquireTimeoutNs();
        if (presentDiagnosticsEnabled()) {
            std::cerr << "MAKO Renderer: present diagnostics enabled; context="
                      << this->diagnosticsState.contextId
                      << "; slow operation threshold is "
                      << presentDiagnosticsThresholdMs() << " ms\n";
            if (this->privateOrderedTransport) {
                const auto slowAcquireThreshold =
                    OrderedAcquireRecovery::slowAcquireDuration(
                        this->gamescopeRefreshHz
                    );
                const auto firstRecoveryAcquireTimeout =
                    orderedRecoveryAcquireTimeout(
                        this->gamescopeRefreshHz,
                        configuredAcquireTimeout, 1
                    );
                const auto maximumRecoveryAcquireTimeout =
                    orderedRecoveryAcquireTimeout(
                        this->gamescopeRefreshHz,
                        configuredAcquireTimeout, 3
                    );
                const auto perImageAcquireTimeout =
                    orderedGeneratedImageAcquireTimeout(
                        this->gamescopeRefreshHz,
                        configuredAcquireTimeout
                    );
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=ordered-acquire-policy"
                          << " context=" << this->diagnosticsState.contextId
                          << " configured_timeout_ms=";
                if (configuredAcquireTimeout) {
                    std::cerr << static_cast<double>(
                        *configuredAcquireTimeout
                    ) / 1'000'000.0;
                } else {
                    std::cerr << "unbounded";
                }
                std::cerr << " slow_threshold_ms="
                          << std::chrono::duration<double, std::milli>(
                                 slowAcquireThreshold
                             ).count()
                          << " per_image_timeout_ms="
                          << (perImageAcquireTimeout ==
                                std::numeric_limits<uint64_t>::max()
                                ? 0.0
                                : static_cast<double>(
                                      perImageAcquireTimeout
                                  ) / 1'000'000.0)
                          << " per_image_timeout_unbounded="
                          << (perImageAcquireTimeout ==
                                std::numeric_limits<uint64_t>::max()
                                ? 1 : 0)
                          << " severe_threshold_ms="
                          << std::chrono::duration<double, std::milli>(
                                 OrderedAcquireRecovery::
                                     severeAcquireDuration(
                                         slowAcquireThreshold
                                     )
                             ).count()
                          << " budget_scope=application-present"
                          << " first_slow_action=zero-wait-protection"
                          << " guard_miss_action=native-relief-history-warmup"
                          << " recovery_probe_timeout_ms="
                          << static_cast<double>(
                                 firstRecoveryAcquireTimeout
                             ) / 1'000'000.0
                          << " recovery_probe_timeout_max_ms="
                          << static_cast<double>(
                                 maximumRecoveryAcquireTimeout
                             ) / 1'000'000.0
                          << " recovery_probe_failure=backoff"
                          << " post_probe_policy=native-only"
                          << " stabilization_ms="
                          << std::chrono::duration<double, std::milli>(
                                 OrderedAcquireRecovery::
                                     stabilizationDuration()
                             ).count()
                          << '\n';
            }
        }
        if (this->gamescopeDetected && !this->privateOrderedTransport) {
            std::cerr << "MAKO Renderer: Gamescope HDR generated-image admission is "
                         "nonblocking; native presentation is never held for "
                         "a synthetic destination\n";
        } else if (this->gamescopeDetected) {
            std::cerr << "MAKO Renderer: Gamescope SDR uses the fork's ordered "
                         "presentation path\n";
        }
        if (configuredAcquireTimeout) {
            std::cerr << "MAKO Renderer: generated-image acquire timeout requested at "
                      << static_cast<double>(*configuredAcquireTimeout) /
                            1'000'000.0
                      << " ms; timeout results and successful wall-time "
                         "deadline overruns enter transport recovery\n";
        }
        const bool schedulerEnabled = this->resetGenerationScheduler(
            DiagnosticsClock::now(),
            this->info.replacement ? "swapchain-recreation" : "startup"
        );
        if (schedulerEnabled) {
            const auto policy = generationSchedulerPolicy(
                this->profile, this->gamescopeRefreshHz
            ).value();
            std::cerr << "MAKO Renderer: target-driven frame generation enabled; mode="
                      << (this->profile.adaptive
                            ? "adaptive" : "fixed-refresh-compatibility")
                      << ", target=" << policy.targetFps
                      << " fps, maximum multiplier="
                      << policy.maximumMultiplier
                      << "x, stable cadence="
                      << (policy.stableCadence ? "enabled" : "disabled")
                      << ", dynamic cadence recovery="
                      << (policy.dynamicCadenceRecovery
                            ? "enabled" : "disabled")
                      << ", cadence probe interval="
                      << policy.dynamicCadenceProbeIntervalSeconds
                      << " s"
                      << '\n';
        } else if (!this->profile.adaptive &&
                this->profile.dynamic_cadence_recovery) {
            std::cerr << "MAKO Renderer: Dynamic Cadence Recovery is unavailable "
                         "for Fixed mode without a supported Gamescope refresh "
                         "signal and 2x-5x multiplier; exact Fixed policy retained\n";
        } else if (fixedCadenceCollapseRecoveryEligible(
                false, this->privateOrderedTransport, false, false,
                this->gamescopeRefreshHz,
                this->configuredFixedGeneratedFrames)) {
            std::cerr << "MAKO Renderer: event-triggered Fixed cadence-collapse "
                         "recovery enabled for ordered Gamescope presentation; "
                         "healthy qualification=1 s, collapse qualification=250 ms\n";
        }
    } catch (const std::exception& e) {
        // Swapchain creation belongs to the game. A failure in MAKO's optional
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
            "frame-generation initialization failed; native presentation retained";
        std::cerr << "MAKO Renderer: " << this->colorPipeline.reason
                  << ": " << e.what() << '\n';
        this->runtimeStatusState.error = e.what();
    }
    this->publishRuntimeStatus("swapchain-create");
}

void Swapchain::publishRuntimeStatus(const std::string_view reason) noexcept {
    const auto frameGenerationPhase = this->frameGenerationTransition.phase();
    const auto spatialPhase = this->spatialTransition.phase();
    const auto eitherPhase = [&](const PrivateResourceTransitionPhase phase) {
        return frameGenerationPhase == phase || spatialPhase == phase;
    };

    RuntimeApplicationPhase phase = RuntimeApplicationPhase::Active;
    if (this->runtimeStatusState.error ||
            eitherPhase(PrivateResourceTransitionPhase::Failed)) {
        phase = RuntimeApplicationPhase::Failed;
    } else if (this->runtimeStatusState.processRestartPending) {
        phase = RuntimeApplicationPhase::ProcessRestart;
    } else if (this->runtimeStatusState.swapchainRecreationPending) {
        phase = RuntimeApplicationPhase::SwapchainRecreation;
    } else if (eitherPhase(PrivateResourceTransitionPhase::Draining)) {
        phase = RuntimeApplicationPhase::Draining;
    } else if (eitherPhase(PrivateResourceTransitionPhase::Preparing)) {
        phase = RuntimeApplicationPhase::Preparing;
    } else if (eitherPhase(PrivateResourceTransitionPhase::Debouncing)) {
        phase = RuntimeApplicationPhase::Debouncing;
    }

    std::optional<double> nonSupersamplingFactorCeiling;
    if (this->info.variableSurface &&
            this->info.gamescopePresentationTarget &&
            this->info.applicationExtent.width > 0 &&
            this->info.applicationExtent.height > 0) {
        nonSupersamplingFactorCeiling = std::clamp(
            std::min(
                static_cast<double>(
                    this->info.gamescopePresentationTarget->width
                ) / this->info.applicationExtent.width,
                static_cast<double>(
                    this->info.gamescopePresentationTarget->height
                ) / this->info.applicationExtent.height
            ),
            static_cast<double>(ls::GameConfLimits::minimumScalingFactor),
            static_cast<double>(ls::GameConfLimits::maximumScalingFactor)
        );
    }
    const double effectiveSpatialFactor =
        this->info.applicationExtent.width > 0 &&
            this->info.applicationExtent.height > 0
        ? std::min(
            static_cast<double>(this->info.extent.width) /
                this->info.applicationExtent.width,
            static_cast<double>(this->info.extent.height) /
                this->info.applicationExtent.height
        )
        : 1.0;
    const bool supersamplingActive = this->spatialScaler &&
        this->profile.scaling_supersampling &&
        this->info.gamescopePresentationTarget &&
        (this->info.extent.width >
            this->info.gamescopePresentationTarget->width ||
         this->info.extent.height >
            this->info.gamescopePresentationTarget->height);
    this->runtimeStatusPublisher.publish(RuntimeStatusRecord{
        .phase = phase,
        .reason = std::string(reason),
        .stateRevision = this->runtimeStatusState.stateRevision,
        .requestedProfile = this->runtimeStatusState.requestedProfile,
        .appliedProfile = this->profile,
        .appliedGeneratedCapacity = this->destinationImages.size(),
        .frameGenerationActive = effectiveFrameGenerationEnabled(
            this->profile, this->gamescopeRefreshHz
        ) && !this->destinationImages.empty(),
        .frameGenerationPrivatePending =
            this->frameGenerationTransition.pendingRequest(),
        .spatialPrivatePending = this->spatialTransition.pendingRequest(),
        .swapchainRecreationPending =
            this->runtimeStatusState.swapchainRecreationPending,
        .processRestartPending =
            this->runtimeStatusState.processRestartPending,
        .spatialScalingActive = this->spatialScaler.has_value(),
        .spatialScalingActivationSupported =
            this->info.spatialScalingActivationSupported,
        .spatialScalingInactiveReason =
            this->info.spatialScalingActivationSupported
                ? std::nullopt
                : std::optional<std::string>{
                    "gamescope-wsi-surface-unproven"
                },
        .spatialSourceWidth = this->info.applicationExtent.width,
        .spatialSourceHeight = this->info.applicationExtent.height,
        .spatialPresentationWidth = this->info.extent.width,
        .spatialPresentationHeight = this->info.extent.height,
        .gamescopeTargetWidth = this->info.gamescopePresentationTarget
            ? this->info.gamescopePresentationTarget->width : 0,
        .gamescopeTargetHeight = this->info.gamescopePresentationTarget
            ? this->info.gamescopePresentationTarget->height : 0,
        .spatialRequestedMethod = this->spatialScaler
            ? this->spatialScaler->requestedMethod()
            : ls::effectiveScalingMethod(this->profile),
        .spatialActiveMethod = this->spatialScaler
            ? this->spatialScaler->activeMethod()
            : ls::ScalingMethod::Native,
        .spatialEffectiveFactor = effectiveSpatialFactor,
        .spatialPipeline = this->spatialScaler
            ? spatialFramePipelinePlacementName(
                this->spatialFramePipelinePlacement
            )
            : "inactive",
        .spatialSupersamplingActive = supersamplingActive,
        .spatialFallbackReason = this->spatialScaler &&
                !this->spatialScaler->fallbackReason().empty()
            ? std::optional<std::string>{
                this->spatialScaler->fallbackReason()
            }
            : std::nullopt,
        .nonSupersamplingFactorCeiling =
            nonSupersamplingFactorCeiling,
        .error = this->runtimeStatusState.error,
    });
}

bool Swapchain::resetGenerationScheduler(
        const DiagnosticsClock::time_point now,
        const std::string_view reason) {
    this->recoveryState.fixedCadenceCollapseRecovery.reset();
    const auto policy = generationSchedulerPolicy(
        this->profile, this->gamescopeRefreshHz
    );
    if (!policy || this->destinationImages.empty()) {
        this->adaptiveScheduler.reset();
        return false;
    }

    this->adaptiveScheduler.emplace(
        AdaptiveSchedulerConfig{
            .targetFps = policy->targetFps,
            .maximumMultiplier = policy->maximumMultiplier,
            .generatedFrameCapacity = this->destinationImages.size(),
            .stableCadence = policy->stableCadence,
            .automaticBaseFpsCap =
                this->profile.adaptive_auto_base_fps_cap,
            .nearTargetNativePreference =
                policy->nearTargetNativePreference,
            .dynamicCadenceRecovery = policy->dynamicCadenceRecovery,
            .dynamicCadenceProbeInterval =
                ls::dynamicCadenceProbeIntervalDuration(
                    policy->dynamicCadenceProbeIntervalSeconds
                ),
            .displayRefreshFps = this->gamescopeRefreshHz,
            .recoveryPolicy = this->privateOrderedTransport
                ? AdaptiveRecoveryPolicy::OrderedSdr
                : AdaptiveRecoveryPolicy::ConservativeHdr,
        },
        &present_diagnostics::adaptiveScheduler()
    );
    this->adaptiveScheduler->beginStabilization(now, reason);
    this->recoveryState.historyWarmupRemaining = 0;
    return true;
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
    std::vector<int> sourceFds(2);
    std::vector<int> destinationFds(
        generatedFrameCapacity(resourceProfile)
    );
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

    int syncFd{};
    resources.syncSemaphore.emplace(vk, 0, std::nullopt, &syncFd);
    resources.context = ls::owned_ptr<ls::R<backend::Context>>(
        new ls::R<backend::Context>(backendInstance.openContext(
            {sourceFds.at(0), sourceFds.at(1)}, destinationFds, syncFd,
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
            generatedFrameCapacity(resourceProfile);
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

    if (presentDiagnosticsEnabled()) {
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
            if (presentDiagnosticsEnabled()) {
                const auto& requested =
                    this->frameGenerationTransition.value().profile;
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=runtime-transition-prepared"
                          << " context=" << this->diagnosticsState.contextId
                          << " state_revision="
                          << this->frameGenerationTransition.stateRevision()
                          << " reason=frame-generation-resources"
                          << " requested_generated_capacity="
                          << generatedFrameCapacity(requested)
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
            if (presentDiagnosticsEnabled()) {
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
    if (presentDiagnosticsEnabled()) {
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
            if (presentDiagnosticsEnabled()) {
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
        if (generatedRenderDrainRequired && presentDiagnosticsEnabled()) {
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
        if (presentDiagnosticsEnabled()) {
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

ProfileUpdateDecision Swapchain::updateProfile(
        const ls::GameConf& nextProfile,
        const uint64_t runtimeStateRevision,
        const ls::GameConf* requestedProfile,
        const bool processRestartPending) {
    this->runtimeStatusState.requestedProfile = requestedProfile
        ? *requestedProfile : nextProfile;
    this->runtimeStatusState.stateRevision = runtimeStateRevision;
    this->runtimeStatusState.processRestartPending = processRestartPending;
    this->runtimeStatusState.error.reset();
    const bool resourcesAvailable = this->sourceImages.size() == 2 &&
        !this->destinationImages.empty() && this->syncSemaphore.has_value();
    const bool privateFrameGenerationRebuildAvailable =
        this->instance && resourcesAvailable &&
        this->colorPipeline.generationSupported;
    const bool spatialScalingEffectiveExtentUnchanged =
        variableSurfaceFactorChangePreservesEffectiveExtents(
            this->info.variableSurface,
            !sameExtent(
                this->info.applicationExtent,
                this->info.extent
            ),
            this->info.applicationExtent,
            this->info.extent,
            this->profile.scaling_factor,
            nextProfile.scaling_factor
        );
    const bool spatialSupersamplingEffectiveExtentUnchanged =
        variableSurfaceSupersamplingChangePreservesEffectiveExtents(
            this->info.variableSurface,
            !sameExtent(
                this->info.applicationExtent,
                this->info.extent
            ),
            this->info.applicationExtent,
            this->info.extent,
            nextProfile.scaling_factor,
            this->profile.scaling_supersampling,
            nextProfile.scaling_supersampling,
            this->info.gamescopePresentationTarget
        );
    auto plan = planProfileUpdate(
        this->profile, nextProfile, this->destinationImages.size(),
        resourcesAvailable, this->spatialScaler.has_value(),
        privateFrameGenerationRebuildAvailable,
        this->info.spatialScalingActivationSupported,
        spatialScalingEffectiveExtentUnchanged,
        spatialSupersamplingEffectiveExtentUnchanged
    );
    auto decision = plan.decision;
    if (decision.frameGenerationPrivateRebuild) {
        const FrameGenerationResourceRequest request{
            .profile = nextProfile,
        };
        if (this->frameGenerationTransition.pendingRequest() &&
                !(this->frameGenerationTransition.value() == request)) {
            this->preparedFrameGenerationResources.reset();
        }
        this->frameGenerationTransition.request(
            request, runtimeStateRevision, std::chrono::milliseconds(500)
        );
        if (presentDiagnosticsEnabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=runtime-transition-pending"
                      << " context=" << this->diagnosticsState.contextId
                      << " state_revision=" << runtimeStateRevision
                      << " reason=frame-generation-resources"
                      << " flow_scale_pending="
                      << (ls::effectiveFlowScale(this->profile) !=
                            ls::effectiveFlowScale(nextProfile))
                      << " lighter_model_pending="
                      << (ls::effectivePerformanceMode(this->profile) !=
                            ls::effectivePerformanceMode(nextProfile))
                      << " generated_capacity_pending="
                      << decision.generatedFrameCapacityExceeded
                      << " active_generated_capacity="
                      << this->destinationImages.size()
                      << " requested_generated_capacity="
                      << generatedFrameCapacity(nextProfile)
                      << " action=prepare-private-context\n";
        }
    } else if (ls::effectiveFlowScale(this->profile) ==
                ls::effectiveFlowScale(nextProfile) &&
            ls::effectivePerformanceMode(this->profile) ==
                ls::effectivePerformanceMode(nextProfile) &&
            generatedFrameCapacityForActivePolicy(nextProfile) <=
                this->destinationImages.size()) {
        this->frameGenerationTransition.cancel();
        this->preparedFrameGenerationResources.reset();
    }
    if (decision.spatialScalingLiveRebuild) {
        const SpatialResourceRequest request{
            .method = ls::effectiveScalingMethod(nextProfile),
            .sharpness = nextProfile.scaling_sharpness,
        };
        if (this->spatialTransition.pendingRequest() &&
                !(this->spatialTransition.value() == request)) {
            this->preparedSpatialScaler.reset();
        }
        this->spatialTransition.request(
            request, runtimeStateRevision,
            spatialScalerRebuildQuietPeriod(this->profile, nextProfile)
        );
        if (presentDiagnosticsEnabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=runtime-transition-pending"
                      << " context=" << this->diagnosticsState.contextId
                      << " state_revision=" << runtimeStateRevision
                      << " reason=spatial-scaler"
                      << " requested_method="
                      << ls::scalingMethodName(
                          ls::effectiveScalingMethod(nextProfile))
                      << " requested_sharpness="
                      << nextProfile.scaling_sharpness
                      << " action=rebuild-private-scaler\n";
        }
    } else if (ls::effectiveScalingMethod(nextProfile) ==
                ls::effectiveScalingMethod(this->profile) &&
            nextProfile.scaling_sharpness ==
                this->profile.scaling_sharpness) {
        this->spatialTransition.cancel();
        this->preparedSpatialScaler.reset();
    }
    const bool liveRecreationAvailable =
        guardedLiveProfileResourceRecreationAvailable(
            decision,
            this->instance && resourcesAvailable &&
                this->colorPipeline.generationSupported,
            this->presentRetirementEnabled(), this->gamescopeDetected,
            spatialScalingOwnedByLayer()
        );
    this->liveProfileResourceRecreation.update(
        this->profile,
        liveRecreationAvailable ? nextProfile : this->profile,
        runtimeStateRevision
    );
    decision.swapchainRecreationRequested = liveRecreationAvailable &&
        this->liveProfileResourceRecreation.armed();
    if (decision.swapchainRecreationRequested &&
            decision.action != ProfileUpdateAction::ApplyLive &&
            !decision.processRestartDeferred) {
        decision.action = ProfileUpdateAction::RequestSwapchainRecreation;
    }

    const auto logPendingRecreation = [&]() {
        if ((!decision.swapchainRecreationDeferred &&
             !decision.processRestartDeferred) ||
                !presentDiagnosticsEnabled()) {
            return;
        }
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=runtime-transition-pending"
                  << " context=" << this->diagnosticsState.contextId
                  << " role=" << layerRoleName
                  << " state_revision=" << runtimeStateRevision
                  << " reason=profile-resources"
                  << " spatial_scaling_pending="
                  << decision.spatialScalingChanged
                  << " frame_generation_backend_pending="
                  << decision.frameGenerationBackendChanged
                  << " flow_scale_pending="
                  << (ls::effectiveFlowScale(this->profile) !=
                        ls::effectiveFlowScale(nextProfile))
                  << " lighter_model_pending="
                  << (ls::effectivePerformanceMode(this->profile) !=
                        ls::effectivePerformanceMode(nextProfile))
                  << " generated_capacity_pending="
                  << decision.generatedFrameCapacityExceeded
                  << " available_generated_capacity="
                  << this->destinationImages.size()
                  << " requested_generated_capacity="
                  << generatedFrameCapacityForActivePolicy(nextProfile)
                  << " process_restart_required="
                  << decision.processRestartDeferred
                  << " action="
                  << (decision.swapchainRecreationRequested
                        ? "signal-out-of-date-after-retirement-fenced-present"
                        : decision.processRestartDeferred
                        ? "wait-for-process-restart"
                        : "wait-for-natural-swapchain-recreation")
                  << '\n';
    };
    logPendingRecreation();
    if (decision.spatialScalingDormantUpdate &&
            presentDiagnosticsEnabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=runtime-transition-applied"
                  << " context=" << this->diagnosticsState.contextId
                  << " role=" << layerRoleName
                  << " state_revision=" << runtimeStateRevision
                  << " reason=spatial-scaler"
                  << " transition=dormant-profile"
                  << " requested_method="
                  << ls::scalingMethodName(
                      ls::effectiveScalingMethod(nextProfile))
                  << " requested_sharpness="
                  << nextProfile.scaling_sharpness
                  << " scaler_active=0"
                  << " action=save-without-wsi-recreation\n";
    }
    if (decision.spatialScalingEffectiveExtentUnchanged &&
            presentDiagnosticsEnabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=runtime-transition-applied"
                  << " context=" << this->diagnosticsState.contextId
                  << " role=" << layerRoleName
                  << " state_revision=" << runtimeStateRevision
                  << " reason=spatial-factor"
                  << " transition=effective-extent-no-op"
                  << " requested_factor=" << nextProfile.scaling_factor
                  << " source=" << this->info.applicationExtent.width
                  << 'x' << this->info.applicationExtent.height
                  << " presentation=" << this->info.extent.width
                  << 'x' << this->info.extent.height
                  << " action=retain-swapchain-and-private-resources\n";
    }
    this->runtimeStatusState.swapchainRecreationPending =
        decision.swapchainRecreationDeferred;

    if (!this->colorPipeline.generationSupported || !this->instance ||
            !resourcesAvailable) {
        this->profile = std::move(plan.appliedProfile);
        this->publishRuntimeStatus("configuration-update");
        return decision;
    }

    if (decision.action == ProfileUpdateAction::NoRuntimeChange ||
            decision.action ==
                ProfileUpdateAction::DeferUntilSwapchainRecreation ||
            decision.action == ProfileUpdateAction::DeferUntilProcessRestart ||
            decision.action == ProfileUpdateAction::RequestSwapchainRecreation) {
        // Keep harmless metadata and dormant-policy values current even when
        // the active policy itself must wait for a documented boundary.
        this->profile = std::move(plan.appliedProfile);
        this->publishRuntimeStatus("configuration-update");
        return decision;
    }

    const bool hadGenerationScheduler = this->adaptiveScheduler.has_value();
    const bool generationWasEnabled = effectiveFrameGenerationEnabled(
        this->profile, this->gamescopeRefreshHz
    );
    const bool generationWillBeEnabled = effectiveFrameGenerationEnabled(
        plan.appliedProfile, this->gamescopeRefreshHz
    );
    const bool enabling = !generationWasEnabled && generationWillBeEnabled;
    const bool disabling = generationWasEnabled && !generationWillBeEnabled;
    this->profile = std::move(plan.appliedProfile);
    this->configuredFixedGeneratedFrames = fixedGeneratedFrameCount(
        this->profile.multiplier, this->destinationImages.size()
    );
    if (decision.generationModeChanged || decision.fixedMultiplierChanged ||
            decision.baseFpsCapChanged || enabling || disabling) {
        this->fixedRefreshBudget.reset();
        this->recoveryState.fixedCadenceCollapseRecovery.reset();
        this->diagnosticsState.fixedWindowStarted.reset();
        this->diagnosticsState.fixedRealFrames = 0;
        this->diagnosticsState.fixedGeneratedFrames = 0;
        this->diagnosticsState.fixedSkippedFrames = 0;
    }
    if (decision.baseFpsCapChanged || decision.generationPolicyChanged ||
            decision.generationModeChanged || enabling || disabling) {
        this->realFramePacer.reset();
        this->smoothCadenceBaseCap.reset();
        this->smoothCadencePacerHandoff.reset();
    }

    if (disabling) {
        this->recoveryState.historyWarmupRemaining = 0;
        this->recoveryState.orderedAcquireRecovery.reset();
        if (this->adaptiveScheduler)
            this->adaptiveScheduler->cancelHistoryWarmup();
    }

    if (enabling) {
        this->recoveryState.generatedImageAdmission.reset();
        this->recoveryState.orderedAcquireRecovery.reset();
        this->recoveryState.pipelineBusyRecovery.reset();
        this->recoveryState.historyWarmupRemaining =
            generationSchedulerPolicy(this->profile, this->gamescopeRefreshHz)
            ? 0
            : AdaptiveScheduler::historyWarmupFrameCount();
    }

    const bool schedulerPolicyAvailable = generationSchedulerPolicy(
        this->profile, this->gamescopeRefreshHz
    ).has_value();
    const bool fixedSchedulerPolicyChanged = !this->profile.adaptive &&
        decision.fixedMultiplierChanged;
    const auto profileUpdateNow = DiagnosticsClock::now();
    const bool resetSchedulerPolicy = schedulerPolicyAvailable &&
            (decision.generationPolicyChanged ||
             decision.generationModeChanged ||
             fixedSchedulerPolicyChanged ||
             decision.baseFpsCapChanged || enabling);
    if (resetSchedulerPolicy) {
        static_cast<void>(this->resetGenerationScheduler(
            profileUpdateNow, "configuration-update"
        ));
    } else if (hadGenerationScheduler && !schedulerPolicyAvailable) {
        this->adaptiveScheduler.reset();
        this->recoveryState.historyWarmupRemaining =
            AdaptiveScheduler::historyWarmupFrameCount();
    } else if (decision.dynamicCadenceProbeIntervalChanged &&
            this->adaptiveScheduler) {
        this->adaptiveScheduler->updateDynamicCadenceProbeInterval(
            profileUpdateNow,
            ls::dynamicCadenceProbeIntervalDuration(
                this->profile.dynamic_cadence_probe_interval_seconds
            )
        );
    }

    const auto updatedGenerationPolicy = generationSchedulerPolicy(
        this->profile, this->gamescopeRefreshHz
    );
    const bool updatedDynamicCadenceRecoveryActive =
        this->privateOrderedTransport && updatedGenerationPolicy &&
        updatedGenerationPolicy->dynamicCadenceRecovery;
    if (presentDiagnosticsEnabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: operation=runtime-state-applied"
                  << " context=" << this->diagnosticsState.contextId
                  << " role=" << layerRoleName
                  << " state_revision=" << runtimeStateRevision
                  << " transition=live"
                  << " frame_generation_enabled="
                  << this->profile.frame_generation_enabled
                  << " frame_generation_refresh_threshold="
                  << this->profile.frame_generation_refresh_threshold
                  << " effective_frame_generation_enabled="
                  << effectiveFrameGenerationEnabled(
                        this->profile, this->gamescopeRefreshHz
                     )
                  << " adaptive=" << this->profile.adaptive
                  << " target_fps=" << this->profile.target_fps
                  << " multiplier=" << this->profile.multiplier
                  << " base_fps_cap=" << this->profile.base_fps_cap
                  << " adaptive_auto_base_fps_cap="
                  << this->profile.adaptive_auto_base_fps_cap
                  << " effective_base_fps_cap="
                  << effectiveBaseFpsCap(this->profile)
                  << " adaptive_max_multiplier="
                  << this->profile.adaptive_max_multiplier
                  << " stable_cadence="
                  << this->profile.adaptive_stable_cadence
                  << " dynamic_cadence_recovery="
                  << this->profile.dynamic_cadence_recovery
                  << " dynamic_cadence_probe_interval_seconds="
                  << this->profile.dynamic_cadence_probe_interval_seconds
                  << " effective_dynamic_cadence_recovery="
                  << updatedDynamicCadenceRecoveryActive
                  << " effective_flow_scale="
                  << ls::effectiveFlowScale(this->profile)
                  << " lighter_model="
                  << ls::effectivePerformanceMode(this->profile)
                  << " frame_generation_resources_available="
                  << (this->sourceImages.size() == 2 &&
                        !this->destinationImages.empty() &&
                        this->syncSemaphore.has_value())
                  << " generated_frame_capacity="
                  << this->destinationImages.size()
                  << " hdr=" << this->colorPipeline.hdr
                  << '\n';
    }

    this->publishRuntimeStatus("configuration-update");
    return decision;
}

bool Swapchain::requestLiveProfileResourceRecreationAfterPresent(
        const VkResult lowerPresentResult) {
    if (lowerPresentResult != VK_SUCCESS &&
            lowerPresentResult != VK_SUBOPTIMAL_KHR) {
        return false;
    }
    if (!this->lastLowerPresentRetirementProtected)
        return false;
    const auto runtimeStateRevision =
        this->liveProfileResourceRecreation.signalAfterSuccessfulPresent();
    if (!runtimeStateRevision)
        return false;

    std::cerr << "MAKO Renderer: live profile resource change requested a "
                 "game-owned swapchain recreation after one "
                 "maintenance1-fenced lower present\n";
    if (presentDiagnosticsEnabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=runtime-transition-recreation-requested"
                  << " context=" << this->diagnosticsState.contextId
                  << " state_revision=" << *runtimeStateRevision
                  << " reason=profile-resources"
                  << " lower_present_result=" << lowerPresentResult
                  << " signal=VK_ERROR_OUT_OF_DATE_KHR"
                  << " delivery=one-shot-after-retirement-fence-attachment\n";
    }
    return true;
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

    if (presentDiagnosticsEnabled()) {
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

void Swapchain::updateGamescopeRefreshRate(
        const std::optional<uint32_t> refreshHz) {
    if (refreshHz == this->gamescopeRefreshHz)
        return;
    const bool generationWasEnabled = effectiveFrameGenerationEnabled(
        this->profile, this->gamescopeRefreshHz
    );
    this->gamescopeRefreshHz = refreshHz;
    this->realFramePacer.reset();
    this->smoothCadenceBaseCap.reset();
    this->smoothCadencePacerHandoff.reset();
    const bool generationIsEnabled = effectiveFrameGenerationEnabled(
        this->profile, this->gamescopeRefreshHz
    );
    const bool generationAvailabilityChanged =
        generationWasEnabled != generationIsEnabled;
    this->fixedRefreshBudget.reset();
    this->recoveryState.fixedCadenceCollapseRecovery.reset();
    if (generationAvailabilityChanged) {
        this->diagnosticsState.fixedWindowStarted.reset();
        this->diagnosticsState.fixedRealFrames = 0;
        this->diagnosticsState.fixedGeneratedFrames = 0;
        this->diagnosticsState.fixedSkippedFrames = 0;
        if (!generationIsEnabled) {
            this->recoveryState.historyWarmupRemaining = 0;
            this->recoveryState.orderedAcquireRecovery.reset();
            if (this->adaptiveScheduler)
                this->adaptiveScheduler->cancelHistoryWarmup();
        } else {
            this->recoveryState.generatedImageAdmission.reset();
            this->recoveryState.orderedAcquireRecovery.reset();
            this->recoveryState.pipelineBusyRecovery.reset();
            if (!this->resetGenerationScheduler(
                    DiagnosticsClock::now(), "refresh-rate-threshold")) {
                this->recoveryState.historyWarmupRemaining =
                    AdaptiveScheduler::historyWarmupFrameCount();
            }
        }
    } else if (generationIsEnabled &&
            ((this->profile.adaptive &&
              this->profile.adaptive_stable_cadence) ||
             (!this->profile.adaptive &&
              this->profile.dynamic_cadence_recovery))) {
        if (!this->resetGenerationScheduler(
                DiagnosticsClock::now(), "gamescope-refresh-change")) {
            this->recoveryState.historyWarmupRemaining =
                AdaptiveScheduler::historyWarmupFrameCount();
        }
    }
    if (presentDiagnosticsEnabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=gamescope-refresh-rate-applied"
                  << " context=" << this->diagnosticsState.contextId
                  << " refresh_hz=" << refreshHz.value_or(0)
                  << " frame_generation_refresh_threshold="
                  << this->profile.frame_generation_refresh_threshold
                  << " effective_frame_generation_enabled="
                  << generationIsEnabled << '\n';
    }
    this->publishRuntimeStatus("gamescope-refresh-rate");
}

void Swapchain::disableFrameGeneration() {
    if (!this->profile.frame_generation_enabled)
        return;

    this->profile.frame_generation_enabled = false;
    this->realFramePacer.reset();
    this->smoothCadenceBaseCap.reset();
    this->smoothCadencePacerHandoff.reset();
    this->recoveryState.historyWarmupRemaining = 0;
    this->recoveryState.orderedAcquireRecovery.reset();
    this->recoveryState.fixedCadenceCollapseRecovery.reset();
    if (this->adaptiveScheduler)
        this->adaptiveScheduler->cancelHistoryWarmup();
    this->publishRuntimeStatus("profile-unmatched");
}
