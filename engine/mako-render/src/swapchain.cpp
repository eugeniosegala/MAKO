/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "swapchain.hpp"
#include "adaptive_scheduler.hpp"
#include "mako-backend/mako.hpp"
#include "mako-common/configuration/config.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/image.hpp"
#include "mako-common/vulkan/vulkan.hpp"
#include "present_diagnostics.hpp"

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

    uint64_t allocateDiagnosticsContextId() {
        return present_diagnostics::allocateContextId();
    }

    bool presentDiagnosticsEnabled() {
        return present_diagnostics::enabled();
    }

    double presentDiagnosticsThresholdMs() {
        return present_diagnostics::thresholdMilliseconds();
    }

    size_t generatedFrameCapacity(const ls::GameConf& profile) {
        return generatedFrameCapacityForProfile(profile);
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
        const PresentationEnvironmentPolicy& presentationEnvironment) {
    const auto colorPipeline = classifySwapchainColor(
        createInfo.imageFormat, createInfo.imageColorSpace,
        gamescopeHdrActive
    );
    if (!colorPipeline.generationSupported ||
            (presentationEnvironment.hdrExposureDisabled && colorPipeline.hdr))
        return false;

    createInfo.imageUsage |=
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    switch (profile.pacing) {
        case ls::Pacing::None:
            // Reserve the selected Fixed or Adaptive capacity even while live
            // generation is off. The game owns this swapchain, so retaining
            // its capacity is what lets configuration reload turn generation
            // back on without forcing a game restart or swapchain rebuild.
            createInfo.minImageCount += generatedFrameCapacity(profile) + 1;
            if (maxImages && createInfo.minImageCount > maxImages)
                createInfo.minImageCount = maxImages;

            const bool hdrCapableSwapchain =
                !presentationEnvironment.hdrExposureDisabled &&
                (colorPipeline.hdr ||
                    colorPipeline.encoding == backend::FrameEncoding::SdrHighPrecision);
            const auto transport = selectPresentationTransport(
                gamescopeDetected, hdrCapableSwapchain,
                presentationEnvironment
            );

            // This is a create-time transport choice, not the current HDR on/off
            // state. Plain SDR keeps the fork's known-good FIFO ordering. An
            // HDR-capable Gamescope swapchain preserves Gamescope's lower WSI
            // contract even while live HDR feedback says inactive, because
            // that feedback may turn on later without a swapchain replacement.
            // Forcing this HDR bridge to FIFO caused every generated/original
            // lower present to block for 30-40 ms.
            if (transport == PresentationTransport::OrderedSdr) {
                createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
                return true;
            }
            return false;
    }

    return false;
}

Swapchain::Swapchain(const vk::Vulkan& vk, backend::Instance& backend,
            ls::GameConf profile, SwapchainInfo info,
            const std::optional<bool> gamescopeHdrActive,
            const bool gamescopeDetected,
            const bool hdrExposureDisabled,
            const std::optional<uint32_t> gamescopeRefreshHz,
            const uint64_t runtimeStateRevision) :
        instance(backend),
        gamescopeDetected(gamescopeDetected),
        privateOrderedTransport(info.privateOrderedTransport),
        gamescopeRefreshHz(gamescopeRefreshHz),
        colorPipeline(initialColorPipeline(
            info.format, info.colorSpace, gamescopeHdrActive, gamescopeDetected,
            hdrExposureDisabled
        )),
        profile(std::move(profile)), info(std::move(info)) {
    this->diagnosticsState.contextId = allocateDiagnosticsContextId();
    const DiagnosticsContextScope diagnosticsContext(
        this->diagnosticsState.contextId
    );
    const VkExtent2D extent = this->info.extent;

    bool applicationPackedHdr10Supported = false;
    bool backendPackedHdr10Supported = false;
    selectPackedHdr10Transport(
        vk, backend, this->colorPipeline,
        applicationPackedHdr10Supported, backendPackedHdr10Supported
    );

    const auto initialGenerationPolicy = generationSchedulerPolicy(
        this->profile, this->gamescopeRefreshHz
    );
    const bool initialFrameGenerationEnabled = effectiveFrameGenerationEnabled(
        this->profile, this->gamescopeRefreshHz
    );
    const bool initialDynamicCadenceRecoveryActive =
        this->privateOrderedTransport && initialGenerationPolicy &&
        initialGenerationPolicy->dynamicCadenceRecovery;
    if (presentDiagnosticsEnabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: operation=runtime-state-applied"
                  << " context=" << this->diagnosticsState.contextId
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
                  << " hdr=" << this->colorPipeline.hdr
                  << '\n';
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
    if (this->colorPipeline.generationSupported &&
            !privateGenerationResourcesRequired(this->profile)) {
        std::cerr << "MAKO Renderer: frame generation is off in Ultra Performance; "
                     "private generation resources omitted\n";
        return;
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
        return;
    }
    try {
        std::vector<int> sourceFds(2);
        std::vector<int> destinationFds(generatedFrameCapacity(this->profile));

        this->sourceImages.reserve(sourceFds.size());
        for (int& fd : sourceFds)
            this->sourceImages.emplace_back(vk,
                extent, this->colorPipeline.exchangeFormat,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                std::nullopt, &fd);

        this->destinationImages.reserve(destinationFds.size());
        for (int& fd : destinationFds)
            this->destinationImages.emplace_back(vk,
                extent, this->colorPipeline.exchangeFormat,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                std::nullopt, &fd);

        int syncFd{};
        this->syncSemaphore.emplace(vk, 0, std::nullopt, &syncFd);

        try {
            this->ctx = ls::owned_ptr<ls::R<backend::Context>>(
                new ls::R<backend::Context>(backend.openContext(
                    { sourceFds.at(0), sourceFds.at(1) }, destinationFds, syncFd,
                    extent.width, extent.height,
                    this->colorPipeline.encoding,
                    1.0F / ls::effectiveFlowScale(this->profile),
                    ls::effectivePerformanceMode(this->profile)
                )),
                [backend = &backend](ls::R<backend::Context>& ctx) {
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

        this->renderCommandBuffer.emplace(vk);
        this->renderFence.emplace(vk);
        for (size_t i = 0; i < this->destinationImages.size(); i++) {
            this->passes.emplace_back(RenderPass {
                .commandBuffer = vk::CommandBuffer(vk),
                .acquireSemaphore = vk::Semaphore(vk)
            });
        }

        this->configuredFixedGeneratedFrames = fixedGeneratedFrameCount(
            this->profile.multiplier, this->destinationImages.size()
        );

        if (!initialFrameGenerationEnabled)
            std::cerr << "MAKO Renderer: frame generation is off; retained private "
                         "resources permit a live enable\n";

        const size_t frames = std::max(
            this->info.images.size(), this->destinationImages.size() + 2
        );
        for (size_t i = 0; i < frames; i++) {
            this->postCopySemaphores.emplace_back(
                vk::Semaphore(vk),
                vk::Semaphore(vk)
            );
        }

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
            DiagnosticsClock::now(), "startup"
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
                         "signal and 2x-4x multiplier; exact Fixed policy retained\n";
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
    }
}

bool Swapchain::resetGenerationScheduler(
        const DiagnosticsClock::time_point now,
        const std::string_view reason) {
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

void Swapchain::rebuildPrivateResources(const vk::Vulkan& vk,
        SwapchainColorPipeline pipeline) {
    const auto retainPassthrough = [&]() {
        this->ctx = {};
        this->sourceImages.clear();
        this->destinationImages.clear();
        this->syncSemaphore = {};
        this->renderCommandBuffer = {};
        this->renderFence = {};
        this->passes.clear();
        this->postCopySemaphores.clear();
        this->adaptiveScheduler.reset();
        this->colorPipeline = std::move(pipeline);
        this->recoveryState.historyWarmupRemaining = 0;
        this->recoveryState.orderedAcquireRecovery.reset();
    };
    if (!privateGenerationResourcesRequired(this->profile)) {
        retainPassthrough();
        return;
    }

    auto& backendInstance = this->instance.get();
    bool applicationPackedHdr10Supported = false;
    bool backendPackedHdr10Supported = false;
    selectPackedHdr10Transport(
        vk, backendInstance, pipeline,
        applicationPackedHdr10Supported, backendPackedHdr10Supported
    );

    if (!pipeline.generationSupported) {
        retainPassthrough();
        return;
    }

    const VkExtent2D extent = this->info.extent;
    std::vector<int> sourceFds(2);
    std::vector<int> destinationFds(generatedFrameCapacity(this->profile));
    std::vector<vk::Image> newSourceImages;
    std::vector<vk::Image> newDestinationImages;
    newSourceImages.reserve(sourceFds.size());
    newDestinationImages.reserve(destinationFds.size());
    for (int& fd : sourceFds) {
        newSourceImages.emplace_back(vk,
            extent, pipeline.exchangeFormat,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &fd);
    }
    for (int& fd : destinationFds) {
        newDestinationImages.emplace_back(vk,
            extent, pipeline.exchangeFormat,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            std::nullopt, &fd);
    }

    ls::lazy<vk::TimelineSemaphore> newSyncSemaphore;
    int syncFd{};
    newSyncSemaphore.emplace(vk, 0, std::nullopt, &syncFd);

    ls::lazy<vk::CommandBuffer> newRenderCommandBuffer;
    ls::lazy<vk::Fence> newRenderFence;
    newRenderCommandBuffer.emplace(vk);
    newRenderFence.emplace(vk);
    std::vector<RenderPass> newPasses;
    newPasses.reserve(newDestinationImages.size());
    for (size_t i = 0; i < newDestinationImages.size(); ++i) {
        static_cast<void>(i);
        newPasses.emplace_back(RenderPass{
            .commandBuffer = vk::CommandBuffer(vk),
            .acquireSemaphore = vk::Semaphore(vk),
        });
    }
    std::vector<std::pair<vk::Semaphore, vk::Semaphore>> newPostCopySemaphores;
    const size_t semaphoreFrames = std::max(
        this->info.images.size(), newDestinationImages.size() + 2
    );
    newPostCopySemaphores.reserve(semaphoreFrames);
    for (size_t i = 0; i < semaphoreFrames; ++i) {
        static_cast<void>(i);
        newPostCopySemaphores.emplace_back(
            vk::Semaphore(vk), vk::Semaphore(vk)
        );
    }

    ls::owned_ptr<ls::R<backend::Context>> newContext(
        new ls::R<backend::Context>(backendInstance.openContext(
            {sourceFds.at(0), sourceFds.at(1)}, destinationFds, syncFd,
            extent.width, extent.height, pipeline.encoding,
            1.0F / ls::effectiveFlowScale(this->profile),
            ls::effectivePerformanceMode(this->profile)
        )),
        [backend = &backendInstance](ls::R<backend::Context>& context) {
            backend->closeContext(context);
        }
    );
    // Match initial context construction: the private backend device follows
    // the documented process-lifetime loader workaround.
    backend::makeLeaking();

    // Everything above is constructed before the active resources are
    // touched. Moving the context first retires the old backend imports while
    // their Vulkan images are still alive.
    this->ctx = std::move(newContext);
    this->sourceImages = std::move(newSourceImages);
    this->destinationImages = std::move(newDestinationImages);
    this->syncSemaphore = std::move(newSyncSemaphore);
    this->renderCommandBuffer = std::move(newRenderCommandBuffer);
    this->renderFence = std::move(newRenderFence);
    this->passes = std::move(newPasses);
    this->postCopySemaphores = std::move(newPostCopySemaphores);
    this->colorPipeline = std::move(pipeline);

    this->frameState.sequenceIndex = 1;
    this->frameState.backendFrameIndex = 0;
    this->frameState.renderFenceInFlight = false;
    this->recoveryState.backendPending = false;
    this->recoveryState.generatedImageAdmission.reset();
    this->recoveryState.orderedAcquireRecovery.reset();
    this->recoveryState.pipelineBusyRecovery.reset();
    this->fixedRefreshBudget.reset();
    this->configuredFixedGeneratedFrames = fixedGeneratedFrameCount(
        this->profile.multiplier, this->destinationImages.size()
    );
    this->diagnosticsState.fixedWindowStarted.reset();
    this->diagnosticsState.fixedRealFrames = 0;
    this->diagnosticsState.fixedGeneratedFrames = 0;
    this->diagnosticsState.fixedSkippedFrames = 0;

    if (!this->resetGenerationScheduler(
            DiagnosticsClock::now(), "hdr-private-transition")) {
        this->recoveryState.historyWarmupRemaining =
            AdaptiveScheduler::historyWarmupFrameCount();
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
}

bool Swapchain::applyPendingColorPipeline(const vk::Vulkan& vk) {
    if (!this->colorTransitionState.pendingGamescopeHdrActive)
        return true;

    const auto now = DiagnosticsClock::now();
    if (this->colorTransitionState.retryAt && now < *this->colorTransitionState.retryAt)
        return false;

    const bool resourcesAvailable = this->sourceImages.size() == 2 &&
        !this->destinationImages.empty() && this->syncSemaphore.has_value();
    if (resourcesAvailable) {
        try {
            if (!this->instance.get().contextReady(this->ctx.get()))
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
        this->rebuildPrivateResources(vk, std::move(desiredPipeline));
    } catch (const std::exception& error) {
        std::cerr << "MAKO Renderer: private colour transition failed; real-frame "
                     "passthrough retained and retry scheduled: "
                  << error.what() << '\n';
        this->colorTransitionState.retryAt = now + std::chrono::seconds(5);
        return false;
    }

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

ProfileUpdateDecision Swapchain::updateProfile(
        const ls::GameConf& nextProfile,
        const uint64_t runtimeStateRevision) {
    // A colour-policy passthrough context has no generation state to mutate.
    // Preserve the full requested profile so a later supported HDR transition
    // builds its private resources from the latest settings, matching the
    // established passthrough contract. Root independently retains and reports
    // any process-static backend difference.
    if (!this->colorPipeline.generationSupported) {
        this->profile = nextProfile;
        return {};
    }

    const bool resourcesAvailable = this->sourceImages.size() == 2 &&
        !this->destinationImages.empty() && this->syncSemaphore.has_value();
    auto plan = planProfileUpdate(
        this->profile, nextProfile, this->destinationImages.size(), resourcesAvailable
    );
    const auto decision = plan.decision;

    const auto logPendingProfileTransition = [&](const bool processRestart) {
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=runtime-transition-pending"
                  << " context=" << this->diagnosticsState.contextId
                  << " state_revision=" << runtimeStateRevision
                  << " reason="
                  << (processRestart
                        ? "profile-process-resources"
                        : "profile-swapchain-resources")
                  << " action="
                  << (processRestart
                        ? "wait-for-process-restart"
                        : "wait-for-natural-swapchain-recreation")
                  << '\n';
    };
    if (presentDiagnosticsEnabled()) {
        if (decision.swapchainRecreationDeferred)
            logPendingProfileTransition(false);
        if (decision.processRestartDeferred)
            logPendingProfileTransition(true);
    }

    if (decision.action == ProfileUpdateAction::NoRuntimeChange ||
            decision.action ==
                ProfileUpdateAction::DeferUntilSwapchainRecreation ||
            decision.action == ProfileUpdateAction::DeferUntilProcessRestart) {
        // Keep harmless metadata and dormant-policy values current even when
        // the active policy itself must wait for a documented boundary.
        this->profile = std::move(plan.appliedProfile);
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
        this->diagnosticsState.fixedWindowStarted.reset();
        this->diagnosticsState.fixedRealFrames = 0;
        this->diagnosticsState.fixedGeneratedFrames = 0;
        this->diagnosticsState.fixedSkippedFrames = 0;
    }
    if (decision.baseFpsCapChanged || decision.generationPolicyChanged ||
            decision.generationModeChanged || enabling || disabling) {
        this->realFramePacer.reset();
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
                  << " hdr=" << this->colorPipeline.hdr
                  << '\n';
    }

    return decision;
}

bool Swapchain::updateGamescopeHdrState(
        const bool active, const uint64_t runtimeStateRevision) {
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
    this->smoothCadencePacerHandoff.reset();
    const bool generationIsEnabled = effectiveFrameGenerationEnabled(
        this->profile, this->gamescopeRefreshHz
    );
    const bool generationAvailabilityChanged =
        generationWasEnabled != generationIsEnabled;
    this->fixedRefreshBudget.reset();
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
}

void Swapchain::disableFrameGeneration() {
    if (!this->profile.frame_generation_enabled)
        return;

    this->profile.frame_generation_enabled = false;
    this->realFramePacer.reset();
    this->smoothCadencePacerHandoff.reset();
    this->recoveryState.historyWarmupRemaining = 0;
    this->recoveryState.orderedAcquireRecovery.reset();
    if (this->adaptiveScheduler)
        this->adaptiveScheduler->cancelHistoryWarmup();
}
