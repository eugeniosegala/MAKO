/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "swapchain/swapchain.hpp"
#include "swapchain/create_policy.hpp"
#include "swapchain/retirement.hpp"
#include "layer_role.hpp"
#include "present_diagnostics.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/file_descriptors.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
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
    uint64_t allocateDiagnosticsContextId() {
        const auto contextId = present_diagnostics::allocateContextId();
        if constexpr (spatialScalingLayer)
            return contextId | uint64_t{0x80000000};
        return contextId;
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
        orderedFrameGenerationTransport,
        profile.swapchain_image_count_compatibility
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

    this->replacementWsiPrimePending =
        nullOldReplacementRequiresDirectPresentPrime(
            this->info.replacement,
            this->info.applicationOldSwapchainProvided,
            this->spatialScaler.has_value()
        );

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
    if (present_diagnostics::enabled()) {
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
                        ? generatedFrameCapacityForProfile(this->profile) : 0)
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
            generatedFrameCapacityForProfile(this->profile);
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
        std::vector<int> sourceFds(2, -1);
        ls::FileDescriptorScope sourceScope{sourceFds};
        std::vector<int> destinationFds(generatedFrameCapacityForProfile(this->profile), -1);
        ls::FileDescriptorScope destinationScope{destinationFds};
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

        int syncFd{-1};
        ls::FileDescriptorScope syncScope{{&syncFd, 1}};
        this->syncSemaphore.emplace(vk, 0, std::nullopt, &syncFd);

        try {
            this->ctx = ls::owned_ptr<ls::R<backend::Context>>(
                new ls::R<backend::Context>(backend->openContext(
                    (sourceScope.release(), std::pair{sourceFds[0], sourceFds[1]}),
                    (destinationScope.release(), destinationFds),
                    (syncScope.release(), syncFd),
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
        if (present_diagnostics::enabled()) {
            std::cerr << "MAKO Renderer: present diagnostics enabled; context="
                      << this->diagnosticsState.contextId
                      << "; slow operation threshold is "
                      << present_diagnostics::thresholdMilliseconds() << " ms\n";
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
                          << " recovery_retry_ceiling_ms="
                          << std::chrono::duration<double, std::milli>(
                                 OrderedAcquireRecovery::maximumRetryDelay()
                             ).count()
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
        const bool frameGenerationActive = effectiveFrameGenerationEnabled(
            this->profile, this->gamescopeRefreshHz
        ) && this->colorPipeline.generationSupported;
        this->recoveryState.replacementBackendStabilization.begin(
            this->info.replacement, frameGenerationActive,
            DiagnosticsClock::now()
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
