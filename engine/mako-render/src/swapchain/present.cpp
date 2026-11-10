/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "swapchain/swapchain.hpp"
#include "layer_role.hpp"
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
#include <locale>
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
    void logSourceIntervalSpacing(const uint64_t contextId, const size_t frame,
            const SourceIntervalOutputSpacing::Observation& observation) {
        using Transition = SourceIntervalOutputSpacing::Transition;
        if (observation.transition == Transition::None || !presentDiagnosticsEnabled())
            return;
        const char* reason = "ineligible";
        switch (observation.transition) {
        case Transition::Engaged: reason = "stable-source-cadence"; break;
        case Transition::Slowed: reason = "source-slowed"; break;
        case Transition::Faster: reason = "source-faster"; break;
        case Transition::Changed: reason = "unspaced-cadence-changed"; break;
        case Transition::Stalled: reason = "source-stall"; break;
        case Transition::Ineligible:
        case Transition::None: break;
        }
        const auto milliseconds = [](const auto duration) {
            return std::chrono::duration<double, std::milli>(duration).count();
        };
        std::ostringstream line;
        line.imbue(std::locale::classic());
        line << "MAKO Renderer: present diagnostics: operation=source-interval-spacing"
             << " context=" << contextId
             << " state=" << (observation.transition == Transition::Engaged
                ? "engaged" : "released")
             << " reason=" << reason
             << " source_interval_ms=" << milliseconds(observation.sourceInterval)
             << " retry_ms=" << milliseconds(observation.retryAfter)
             << " frame=" << frame << '\n';
        std::cerr << line.str();
    }
}

void Swapchain::PresentInvocation::waitForOutput(
        const DiagnosticsClock::time_point deadline) const {
    const auto waitStarted = startPresentDiagnostic();
    std::this_thread::sleep_until(deadline);
    // The old source-cap sleep preceded present diagnostics. Moving that
    // wait between outputs must not report intentional limiting as slow GPU
    // work or emit a slow-present record on every capped frame.
    this->started += finishPresentDiagnostic(waitStarted);
}

VkResult Swapchain::present(const vk::Vulkan& vk,
        const VkQueue queue, const VkSwapchainKHR swapchain,
        void* nextChain, const uint32_t imageIndex,
        const std::span<const VkSemaphore> waitSemaphores) {
    // Test recordings exist only in a launch armed with MAKO_RECORD_FRAMES.
    if (!frameGenerationLayer || !FrameRecorder::launchSettings().armed)
        return this->presentFrame(vk, queue, swapchain, nextChain, imageIndex, waitSemaphores);
    if (!this->frameRecorder)
        this->frameRecorder = std::make_unique<FrameRecorder>(FrameRecorder::launchSettings());
    // The application renders the top-left application extent of the image.
    const VkExtent2D recorded = this->info.applicationExtent.width && this->info.applicationExtent.height
        ? VkExtent2D{std::min(this->info.applicationExtent.width, this->info.extent.width),
              std::min(this->info.applicationExtent.height, this->info.extent.height)}
        : this->info.extent;
    const auto waits = this->frameRecorder->observe(vk, queue, this->info.images.at(imageIndex),
        this->info.format, this->info.colorSpace, recorded,
        (this->info.imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0, waitSemaphores);
    return this->presentFrame(vk, queue, swapchain, nextChain, imageIndex, waits);
}

VkResult Swapchain::presentFrame(const vk::Vulkan& vk,
        const VkQueue queue, const VkSwapchainKHR swapchain,
        void* nextChain, const uint32_t imageIndex,
        const std::span<const VkSemaphore> waitSemaphores) {
    const DiagnosticsContextScope diagnosticsContext(
        this->diagnosticsState.contextId
    );
    this->bridgeOutputBatchSize = 1;
    this->outputSourceDeadline.reset();
#if defined(VK_EXT_present_timing) && defined(VK_KHR_present_id2)
    if (this->info.ownedPresentTiming && !this->presentTimingInitialized &&
            this->info.presentMode == VK_PRESENT_MODE_FIFO_KHR) {
        this->presentTimingInitialized = true;
        const auto get = [&](const char* name) { return vk.fi().GetDeviceProcAddr(vk.dev(), name); };
        const bool enabled = this->presentTiming.initialize(vk.dev(), swapchain, {
            .setSize = reinterpret_cast<PFN_vkSetSwapchainPresentTimingQueueSizeEXT>(get("vkSetSwapchainPresentTimingQueueSizeEXT")),
            .properties = reinterpret_cast<PFN_vkGetSwapchainTimingPropertiesEXT>(get("vkGetSwapchainTimingPropertiesEXT")),
            .domains = reinterpret_cast<PFN_vkGetSwapchainTimeDomainPropertiesEXT>(get("vkGetSwapchainTimeDomainPropertiesEXT")),
            .past = reinterpret_cast<PFN_vkGetPastPresentationTimingEXT>(get("vkGetPastPresentationTimingEXT")),
            .calibrate = reinterpret_cast<PFN_vkGetCalibratedTimestampsKHR>(get("vkGetCalibratedTimestampsKHR")),
        });
        std::clog << "MAKO Renderer: presentation timing initialized; owner=renderer; "
                     "transport=VK_EXT_present_timing; active=" << enabled << '\n';
    }
    const bool hadPresentTiming = this->presentTiming.active();
    this->presentTiming.update(this->wsiPresentTimeline, this->frameState.realFrameIndex);
    if (hadPresentTiming && !this->presentTiming.active())
        std::clog << "MAKO Renderer: presentation timing fallback; owner=renderer; "
                     "transport=VK_EXT_present_timing; reason=feedback-or-clock-unavailable\n";
#endif
    this->applyPendingSpatialScaler(vk);
    if (hdrShaderPrecisionSupported(this->colorPipeline, this->hdrExposureDisabled) &&
            this->shaderHdrReducedPrecision != ls::hdrCompactBuffers(this->profile.hdr_quality)) {
        using SetHdrPrecision = VkBool32 (VKAPI_PTR *)(
            VkDevice, VkSwapchainKHR, VkBool32);
        const auto setHdrPrecision = reinterpret_cast<SetHdrPrecision>(
            vk.fi().GetDeviceProcAddr(vk.dev(), "makoSetSwapchainHdrPrecisionV1"));
        const bool accepted = setHdrPrecision &&
            setHdrPrecision(vk.dev(), swapchain, ls::hdrCompactBuffers(this->profile.hdr_quality));
        std::clog << "MAKO Renderer: HDR shader precision: reduced="
                  << ls::hdrCompactBuffers(this->profile.hdr_quality)
                  << "; request_accepted=" << accepted << '\n';
        this->shaderHdrReducedPrecision = ls::hdrCompactBuffers(this->profile.hdr_quality);
    }
    if (presentDiagnosticsEnabled()) {
        const auto requested = present_diagnostics::applicationPresentMode(
            this->info.incomingPresentMode, nextChain);
        if (this->diagnosticsState.applicationPresentMode != requested) {
            this->diagnosticsState.applicationPresentMode = requested;
            // Publish a complete record so the background health line cannot
            // split this startup/live-mode observation between flushed fields.
            try {
                std::ostringstream line;
                line << "MAKO Renderer: present diagnostics: operation=application-present-mode"
                      << " context=" << this->diagnosticsState.contextId
                      << " swapchain=" << swapchain
                      << " requested_present_mode=" << requested.mode
                      << " dynamic_override=" << requested.dynamic
                      << " effective_present_mode=" << static_cast<int64_t>(this->info.presentMode)
                      << " ordered_transport=" << this->privateOrderedTransport
                      << " bridge=" << static_cast<bool>(this->info.gamescopeScalingSurface)
                      << " action=diagnostic-only\n";
                std::cerr << line.str();
            } catch (...) {}
        }
    }
    // Match the immutable create-time choice. Ordered SDR filters Gamescope's
    // dynamic MAILBOX override so the private transport stays ordered. HDR
    // preserves it. A feedback transition cannot change the game-owned
    // VkSwapchainKHR's creation contract.
    // Application damage rectangles are expressed in the virtual source
    // coordinate space. The scaler rewrites the whole native WSI image, so a
    // lower incremental-present region would incorrectly leave most of that
    // image stale. Build a filtered copy of the present-chain prefix: Vulkan
    // input chains are const and may be shared or stored in read-only memory.
    const FilteredPresentPNextChain filteredPresentChain(
        nextChain, this->privateOrderedTransport,
        this->spatialScaler.has_value()
    );
    if (!filteredPresentChain.valid()) {
        throw ls::vulkan_error(
            VK_ERROR_UNKNOWN,
            "cannot safely filter an unknown VkPresentInfoKHR pNext prefix "
            "structure (sType=" + std::to_string(
                static_cast<int>(
                    filteredPresentChain.unsupportedStructureType()
                )
            ) + ")"
        );
    }
    const void* lowerNextChain = filteredPresentChain.head();
#if defined(VK_EXT_present_timing) && defined(VK_KHR_present_id2)
    this->presentTiming.observePresentMode(lowerNextChain);
#endif
    const bool gamescopeHdrTransport =
        this->gamescopeDetected && !this->privateOrderedTransport;

    const auto limiterArrival = DiagnosticsClock::now();
    this->applyGamescopeFocus(limiterArrival);
    if (this->profile.gpu_headroom_percent > 0 || this->gpuHeadroomResult.state != "off") {
        const bool headroomEnabled = this->profile.gpu_headroom_percent > 0 &&
            !this->steamMenuSuspended && !this->destinationImages.empty() &&
            !this->recoveryState.orderedAcquireRecovery.active() &&
            effectiveFrameGenerationEnabled(this->profile, this->gamescopeRefreshHz);
        const auto headroomLoad = headroomEnabled && this->gpuLoadMonitor
            ? this->gpuLoadMonitor->latest() : GpuHeadroomLoad{};
        // Sample first: a worker publication between the old arrival clock
        // and this load must not look like invalid future telemetry.
        const auto headroomNow = DiagnosticsClock::now();
        const double headroomFloor = std::max(20.0, this->profile.adaptive
            ? static_cast<double>(this->profile.target_fps) / static_cast<double>(
                std::max(size_t{1}, std::min(this->profile.adaptive_max_multiplier,
                    std::min(this->destinationImages.size(),
                        this->adaptiveOrderedWsiGeneratedCapacityLimit.value_or(this->destinationImages.size())) + 1)))
            : 20.0);
        const auto previousHeadroom = this->gpuHeadroomResult;
        const double previousHeadroomCeiling = this->gpuHeadroom.sourceCeilingFps();
        this->gpuHeadroomResult = this->gpuHeadroom.update(headroomNow,
            headroomEnabled ? this->profile.gpu_headroom_percent : 0, headroomLoad, headroomFloor);
        if (!headroomEnabled && this->profile.gpu_headroom_percent > 0)
            this->gpuHeadroomResult.state = "inactive";
        if (this->profile.adaptive && this->adaptiveScheduler && previousHeadroom.capFps > 0 &&
                this->gpuHeadroomResult.capFps == 0 &&
                (this->gpuHeadroomResult.state == "within-budget" ||
                 this->gpuHeadroomResult.state == "telemetry-unavailable"))
            this->adaptiveScheduler->requestHeadroomRecovery(headroomNow, previousHeadroomCeiling);
        if (previousHeadroom.state != this->gpuHeadroomResult.state)
            this->publishRuntimeStatus("gpu-headroom");
        if ((previousHeadroom.capFps > 0) != (this->gpuHeadroomResult.capFps > 0))
            this->realFramePacer.reset();
        if (presentDiagnosticsEnabled() && this->profile.gpu_headroom_percent > 0 &&
                (!this->lastGpuHeadroomDiagnostic ||
                 limiterArrival - *this->lastGpuHeadroomDiagnostic >= std::chrono::seconds(2))) {
            this->lastGpuHeadroomDiagnostic = limiterArrival;
            std::ostringstream line;
            line.imbue(std::locale::classic());
            line << "MAKO Renderer: present diagnostics: operation=gpu-headroom"
                 << " context=" << this->diagnosticsState.contextId
                 << " state=" << this->gpuHeadroomResult.state
                 << " reserve_percent=" << this->profile.gpu_headroom_percent
                 << " target_utilization_percent=" << 100 - this->profile.gpu_headroom_percent
                 << " gpu_scope=application-device gpu_metrics_scope=whole-device"
                 << " measured_utilization_percent=";
            if (headroomLoad.percent) line << *headroomLoad.percent;
            else line << "unknown";
            line << " source_cap_fps=" << this->gpuHeadroomResult.capFps
                 << " source_floor_fps=" << headroomFloor << '\n';
            std::cerr << line.str();
        }
    }
    AdaptiveSchedulerSnapshot schedulerSnapshot;
    std::optional<size_t> handoffGenerationLimit;
    bool cadenceBaseCapEligible = false;
    bool automaticBaseCapSuppressed = false;
    if (this->adaptiveScheduler && !this->steamMenuSuspended) {
        schedulerSnapshot = this->adaptiveScheduler->snapshot();
        automaticBaseCapSuppressed =
            effectiveBaseFpsCap(
                this->profile, schedulerSnapshot,
                this->gamescopePresentationFeedback, this->gamescopeRefreshHz
            ) <= 0.0 &&
            effectiveBaseFpsCap(this->profile, this->gamescopeRefreshHz) > 0.0;
        cadenceBaseCapEligible = smoothCadenceBaseCapEligible(
            this->profile,
            this->privateOrderedTransport,
            this->recoveryState.orderedAcquireRecovery.active(),
            this->gamescopeRefreshHz,
            this->gamescopePresentationFeedback
        ) && !automaticBaseCapSuppressed && this->gpuHeadroomResult.capFps <= 0;
        handoffGenerationLimit = smoothCadencePacerHandoffGenerationLimit(
            this->profile,
            this->privateOrderedTransport,
            this->recoveryState.orderedAcquireRecovery.active(),
            this->gamescopeRefreshHz,
            schedulerSnapshot,
            this->gamescopePresentationFeedback,
            this->smoothCadencePacerHandoff.activeGenerationLimit()
        );
    }
    const auto cadenceBaseCap = this->smoothCadenceBaseCap.update(
        limiterArrival,
        cadenceBaseCapEligible,
        this->profile.target_fps,
        {
            .validatedGenerationLimit =
                schedulerSnapshot.validatedGenerationLimit,
            .stableCadenceLimit = schedulerSnapshot.stableCadenceLimit,
            .smoothedBaseFps = schedulerSnapshot.smoothedBaseFps,
            .rampEvaluationActive = schedulerSnapshot.rampEvaluationActive,
            .efficiencyProbeGenerationLimit =
                schedulerSnapshot.efficiencyProbeGenerationLimit,
            .rearmRequired = schedulerSnapshot.rearmRequired,
            .discontinuityRecoveryActive =
                schedulerSnapshot.discontinuityRecoveryActive,
        }
    );
    if (cadenceBaseCap.changed) {
        this->realFramePacer.reset();
        present_diagnostics::adaptiveScheduler().stableCadence(
            cadenceBaseCap.framesPerSecond
                ? "adaptive-smooth-cadence-base-cap"
                : "adaptive-smooth-cadence-base-cap-restored",
            cadenceBaseCap.multiplier > 0
                ? cadenceBaseCap.multiplier - 1
                : 0,
            schedulerSnapshot.smoothedBaseFps,
            schedulerSnapshot.smoothedBaseFps,
            cadenceBaseCap.framesPerSecond
                ? "steady-integer-ladder-qualified"
                : "scheduler-or-transport-guard-restored"
        );
    }
    const bool plannedVrrProbePause = this->gpuHeadroomResult.capFps <= 0 &&
        !handoffGenerationLimit &&
        smoothCadencePacerHandoffPlannedProbe(
            this->profile,
            this->privateOrderedTransport,
            this->recoveryState.orderedAcquireRecovery.active(),
            this->gamescopeRefreshHz,
            schedulerSnapshot,
            this->gamescopePresentationFeedback,
            this->smoothCadencePacerHandoff.activeGenerationLimit()
        );
    if (this->gpuHeadroomResult.capFps > 0)
        handoffGenerationLimit.reset();
    // Zink's worker-thread presents expose a full FIFO queue as waits on
    // MAKO's own generated-image acquires. Other engines never read them.
    const auto presentationWait = this->info.applicationAcquireWindow
        ? std::exchange(this->lastGeneratedAcquireWait, std::nullopt)
        : std::nullopt;
    const auto handoff = this->smoothCadencePacerHandoff.update(
        limiterArrival, handoffGenerationLimit, plannedVrrProbePause,
        this->gamescopeRefreshHz, presentationWait
    );
    if (handoff.changed) {
        this->realFramePacer.reset();
        present_diagnostics::adaptiveScheduler().stableCadence(
            handoff.active
                ? "adaptive-smooth-cadence-pacer-handoff"
                : "adaptive-smooth-cadence-pacer-restored",
            handoff.generationLimit.value_or(
                handoff.previousGenerationLimit.value_or(0)
            ),
            schedulerSnapshot.smoothedBaseFps,
            schedulerSnapshot.smoothedBaseFps,
            handoff.active
                ? (!this->profile.adaptive_auto_base_fps_cap
                    ? "ordered-vrr-accepted-integer"
                    : (handoff.generationLimit == 1
                        ? "ordered-fifo-target-match"
                        : "ordered-vrr-full-rung"))
                : (handoff.sourceOverrun
                    ? "fifo-source-overrun"
                    : (handoff.presentationBacklog
                        ? "fifo-presentation-backlog"
                        : (plannedVrrProbePause
                            ? "planned-vrr-efficiency-probe"
                            : "guard-restored-long-retry")))
        );
    }
    double baseFpsCap = this->steamMenuSuspended || handoff.active
        ? 0.0
        : cadenceBaseCap.framesPerSecond.value_or(
            effectiveBaseFpsCap(
                this->profile, schedulerSnapshot,
                this->gamescopePresentationFeedback, this->gamescopeRefreshHz
            )
        );
    if (this->info.applicationAcquireWindow && !this->steamMenuSuspended) {
        const double zinkCap = baseFpsCap > 0.0
            ? 0.0 : zinkRefreshLimitedBaseFpsCap(this->profile, this->gamescopeRefreshHz);
        if (zinkCap > 0.0)
            baseFpsCap = zinkCap;
        if (std::exchange(this->zinkRefreshLimitedCap, zinkCap) != zinkCap &&
                presentDiagnosticsEnabled()) {
            std::ostringstream line;
            line.imbue(std::locale::classic());
            line << "MAKO Renderer: present diagnostics: operation=zink-refresh-limited-cap"
                 << " context=" << this->diagnosticsState.contextId
                 << " cap_fps=" << zinkCap
                 << " refresh_hz=" << this->gamescopeRefreshHz.value_or(0)
                 << " multiplier=" << this->profile.multiplier << '\n';
            std::cerr << line.str();
        }
    }
    if (this->gpuHeadroomResult.capFps > 0)
        baseFpsCap = baseFpsCap > 0 ? std::min(baseFpsCap, this->gpuHeadroomResult.capFps)
            : this->gpuHeadroomResult.capFps;
    auto limiterDeadline = this->realFramePacer.schedule(
        limiterArrival, baseFpsCap, this->gpuHeadroomResult.capFps > 0
    );
    if (this->info.applicationAcquireWindow) {
        const auto drain = this->cappedFifoBacklogDrain.observe(
            limiterArrival, baseFpsCap, this->gamescopeRefreshHz,
            presentationWait
        );
        if (drain.hold > DiagnosticsClock::duration::zero())
            limiterDeadline = this->realFramePacer.hold(limiterDeadline, drain.hold);
        if ((drain.hold > DiagnosticsClock::duration::zero() || drain.suspended) &&
                presentDiagnosticsEnabled()) {
            std::ostringstream line;
            line.imbue(std::locale::classic());
            line << "MAKO Renderer: present diagnostics: operation=fifo-backlog-drain"
                 << " context=" << this->diagnosticsState.contextId
                 << " action=" << (drain.suspended ? "suspend" : "hold")
                 << " hold_ms=" << std::chrono::duration<double, std::milli>(drain.hold).count()
                 << " holds=" << drain.holds
                 << " waited_frames=" << drain.window.waited
                 << " frames=" << drain.window.frames
                 << " source_cap_fps=" << baseFpsCap
                 << " refresh_hz=" << this->gamescopeRefreshHz.value_or(0) << '\n';
            std::cerr << line.str();
        }
    }
    const bool ownedTimingControlsOutput = this->presentTiming.controlsOutput(
        this->gamescopeRefreshHz.value_or(0));
    auto sourceRefreshHz = this->gamescopeRefreshHz;
#if defined(VK_EXT_present_timing) && defined(VK_KHR_present_id2)
    if (!sourceRefreshHz && ownedTimingControlsOutput) {
        const double refresh = this->presentTiming.refreshRate(0);
        if (OrderedPresentTimeline::validRate(refresh))
            sourceRefreshHz = static_cast<uint32_t>(std::round(refresh));
    }
#endif
    // These transports already own an output clock. Preparing inside a
    // target-matched automatic cap must not add another CPU output clock.
    const bool compositorOutputTiming = this->info.gamescopeScalingSurface ||
        displayTimingControlsOutput(this->wsiPresentTimingQuery != nullptr,
            this->privateOrderedTransport, this->gamescopeRefreshHz.value_or(0)) ||
        ownedTimingControlsOutput;
    const bool sourceDeadlinePreparation = limiterDeadline > limiterArrival &&
        baseCapPacesOutputs(
            this->profile, baseFpsCap,
            (this->privateOrderedTransport || ownedTimingControlsOutput) &&
                !hasPresentTiming(lowerNextChain) &&
                !this->recoveryState.orderedAcquireRecovery.active(),
            sourceRefreshHz, compositorOutputTiming) &&
        orderedGeneratedBatchCanPrepareBeforeCap(
            this->info.requestedMinImageCount, this->info.images.size(),
            generatedFrameCapacityForActivePolicy(this->profile),
            this->recoveryState.generatedImageAdmission.underPressure());
    // Bridge and owned Vulkan timing already space outputs in the compositor.
    // Prepare inside the cap interval without adding a second output clock.
    const bool sourceDeadlinePacing = sourceDeadlinePreparation &&
        !compositorOutputTiming;
    if (sourceDeadlinePreparation && compositorOutputTiming)
        this->outputSourceDeadline = limiterDeadline;
    if (!sourceDeadlinePacing)
        this->realFramePacer.resetOutputs();
    if (!sourceDeadlinePreparation && limiterDeadline > limiterArrival)
        std::this_thread::sleep_until(limiterDeadline);
    // Output deadlines may put the last image before the cap boundary. Keep
    // the existing application-return deadline on every exit, including
    // fallback and exception paths, without adding another frame of latency.
    struct SourceCapCompletion {
        std::optional<DiagnosticsClock::time_point> deadline;
        ~SourceCapCompletion() {
            if (deadline)
                std::this_thread::sleep_until(*deadline);
        }
    } sourceCapCompletion{sourceDeadlinePreparation
        ? std::optional{limiterDeadline} : std::nullopt};

    const auto presentNow = DiagnosticsClock::now();
    // Observe the reserved real-frame cadence, while resource/recovery clocks
    // continue using wall time. Capped work uses the existing limiter's
    // slack; control returns to the game no earlier than that same deadline.
    const auto cadenceNow = sourceDeadlinePreparation
        ? std::max(limiterDeadline, presentNow) : presentNow;
    PresentInvocation invocation{
        .vk = vk,
        .queue = queue,
        .swapchain = swapchain,
        .nextChain = lowerNextChain,
        .imageIndex = imageIndex,
        .waitSemaphores = waitSemaphores,
        .cadenceStarted = cadenceNow,
        .started = startPresentDiagnostic(),
        .originalPresentDeadline = sourceDeadlinePacing
            ? std::optional{limiterDeadline} : std::nullopt,
    };
    this->recordPresentCadence(cadenceNow);
    // Observe every real present, including native ones, so the spacing
    // baseline and its slowdown guard see the application's own cadence.
    logSourceIntervalSpacing(this->diagnosticsState.contextId,
        this->frameState.realFrameIndex,
        this->sourceIntervalSpacing.observe(cadenceNow,
            !sourceDeadlinePreparation && !this->steamMenuSuspended &&
            !this->recoveryState.orderedAcquireRecovery.active() &&
            sourceIntervalSpacingApplies(this->profile,
                this->privateOrderedTransport && !hasPresentTiming(lowerNextChain) &&
                    this->info.presentMode == VK_PRESENT_MODE_FIFO_KHR &&
                    !this->info.gamescopeScalingSurface,
                this->info.gamescopeSession, this->gamescopeRefreshHz,
                compositorOutputTiming)));

    if (std::exchange(this->replacementWsiPrimePending, false)) {
        if (presentDiagnosticsEnabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=replacement-wsi-prime"
                      << " context=" << this->diagnosticsState.contextId
                      << " reason=null-old-swapchain"
                      << " spatial_scaling_active=1"
                      << " wait_semaphores=" << waitSemaphores.size()
                      << " frame=" << this->frameState.realFrameIndex
                      << " sequence=" << this->frameState.sequenceIndex
                      << " action=direct-application-present-before-spatial-work\n";
        }
        return this->presentDirectApplicationFrame(
            invocation, "replacement-wsi-prime"
        );
    }

    if (!this->applyPendingColorPipeline(vk))
        return this->presentNativeFrame(invocation);
    if (!this->applyPendingFrameGenerationResources(vk))
        return this->presentNativeFrame(invocation);

    // A post-FG scaler is referenced by generated-image command buffers.
    // Once its replacement reaches the drain phase, stop scheduling new
    // generated work until the previous render fence and the real-image
    // spatial passes have retired. The old scaler continues presenting the
    // current real frame, preserving image continuity without a device-wide
    // idle or a game-owned swapchain recreation.
    if (this->spatialTransition.draining() &&
            spatialScalerTransitionRequiresGeneratedRenderDrain(
                this->spatialFramePipelinePlacement)) {
        if (presentDiagnosticsEnabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=runtime-transition-draining"
                      << " context=" << this->diagnosticsState.contextId
                      << " reason=spatial-scaler"
                      << " generated_render_work_in_flight="
                      << this->frameState.renderFenceInFlight
                      << " frame=" << this->frameState.realFrameIndex
                      << " sequence=" << this->frameState.sequenceIndex
                      << " action=real-frame-only\n";
        }
        return this->presentNativeFrame(invocation);
    }

    // A test recording needs every real frame at the game's own rate.
    if (this->frameRecorder && this->frameRecorder->holdsFrameGeneration())
        return this->presentNativeFrame(invocation);
    // Frame generation is live-disabled; hand the game's own image directly
    // to the driver without copies, model scheduling or generated images. A
    // scaling-engine process may still attach its preallocated WSI-retirement
    // fence so natural resolution changes remain safe.
    if (!effectiveFrameGenerationEnabled(
            this->profile, this->gamescopeRefreshHz) ||
            !this->colorPipeline.generationSupported) {
        return this->presentNativeFrame(invocation);
    }
    // No scheduler observations, multiplier probes, generated acquisitions or
    // interpolation while Gamescope confirms Steam owns input. Keep spatial
    // reconstruction and the real frame, and retain all GPU ownership guards.
    if (this->steamMenuSuspended)
        return this->presentNativeFrame(invocation);
    const auto replacementStabilization =
        this->recoveryState.replacementBackendStabilization.beforeFrame(
            presentNow
        );
    if (replacementStabilization.bypassBackend) {
        if (replacementStabilization.diagnostic &&
                presentDiagnosticsEnabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=replacement-backend-stabilization"
                      << " context=" << this->diagnosticsState.contextId
                      << " duration_ms="
                      << std::chrono::duration<double, std::milli>(
                             ReplacementBackendStabilization::duration
                         ).count()
                      << " spatial_scaling_active="
                      << this->spatialScaler.has_value()
                      << " action=scaled-real-frame-only\n";
        }
        return this->presentNativeFrame(invocation);
    }
    if (replacementStabilization.resumed) {
        this->ensureHistoryWarmup();
        if (presentDiagnosticsEnabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=replacement-backend-stabilization-complete"
                      << " context=" << this->diagnosticsState.contextId
                      << " history_warmup_frames="
                      << AdaptiveScheduler::historyWarmupFrameCount()
                      << " action=resume-private-backend\n";
        }
    }
    if (!this->recoverBackendIfReady(vk))
        return this->presentNativeFrame(invocation);

    bool orderedAcquireRecoveryProbe = false;
    bool boundedOrderedAcquireProbe = false;
    if (this->privateOrderedTransport) {
        const auto recovery =
            this->recoveryState.orderedAcquireRecovery.beforePresent(
                presentNow
            );
        if (recovery.bypassGeneration) {
            // Keep Adaptive's cadence clock current while freezing every
            // multiplier evaluation. No backend work or synthetic swapchain
            // acquire is attempted until the direct-failure retry deadline.
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
        boundedOrderedAcquireProbe = recovery.boundedAcquireProbe;
        if (recovery.beginHistoryWarmup) {
            this->ensureHistoryWarmup();
            this->fixedRefreshBudget.reset();
            if (presentDiagnosticsEnabled()) {
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=ordered-acquire-retry"
                          << " context=" << this->diagnosticsState.contextId
                          << " phase=history-warmup"
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
                          << " requested_generated=0"
                          << " admitted_generated=0"
                          << " presented_generated=0"
                          << " frame=" << this->frameState.realFrameIndex
                          << " sequence=" << this->frameState.sequenceIndex
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
        cadenceNow, orderedAcquireRecoveryProbe
    );
    plan.boundedOrderedAcquireProbe = boundedOrderedAcquireProbe;

    // The Gamescope HDR bridge remains native-first. Normal fixed-refresh
    // Adaptive ordered SDR retains the 3.3 bounded sequential acquisition.
    // Requested VRR uses the same finite application-present ceiling because
    // its lower-image release is not fixed to an output-period ladder; a
    // timeout moves later frames to zero-wait pressure preflight. Fixed and
    // undersized-pool contracts remain unchanged.
    if (!this->generationPipelineReady(
            vk, gamescopeHdrTransport, plan, presentNow)) {
        return this->presentNativeFrame(invocation);
    }

    if (plan.historyWarmupActive && !this->adaptiveScheduler)
        this->diagnosticsState.fixedSkippedFrames +=
            plan.requestedGeneratedFrames.size();

    // Resolve prior work before acquiring any generated images or reusing
    // their acquire semaphores. A fence-budget miss must leave no new image
    // held by this plan when it falls back to native presentation.
    const auto renderFenceStarted = startPresentDiagnostic();
    const bool renderFenceReady = this->prepareRenderFence(vk);
    plan.renderFenceWaitDuration = finishPresentDiagnostic(
        renderFenceStarted
    );
    if (!renderFenceReady) {
        this->handleRenderFenceBudgetMiss(plan);
        return this->presentNativeFrame(invocation);
    }

    if (this->privateOrderedTransport) {
        plan.configuredAcquireTimeout = orderedGeneratedBatchAcquireBudget(
            this->info.requestedMinImageCount, this->info.images.size(),
            plan.requestedGeneratedFrames.size(), plan.configuredAcquireTimeout
        );
    }
    const bool headroomTightOrderedBatch =
        this->privateOrderedTransport && !orderedAcquireRecoveryProbe &&
        orderedGeneratedBatchNeedsNonblockingAdmission(
            this->info.requestedMinImageCount,
            this->info.images.size(),
            plan.requestedGeneratedFrames.size()
        );
    plan.adaptiveOrderedDeliveryPolicy =
        selectAdaptiveOrderedDeliveryPolicy(
            this->profile.adaptive, this->privateOrderedTransport,
            orderedAcquireRecoveryProbe, gamescopeHdrTransport,
            headroomTightOrderedBatch,
            this->gamescopePresentationFeedback,
            plan.requestedGeneratedFrames.size()
        );
    if (plan.adaptiveOrderedDeliveryPolicy ==
            AdaptiveOrderedDeliveryPolicy::VariableRefreshBounded) {
        plan.configuredAcquireTimeout =
            adaptiveVariableRefreshDeliveryAcquireBudget(
                plan.adaptiveOrderedDeliveryPolicy,
                plan.configuredAcquireTimeout
            );
    }
    const bool adaptivePressureRetry =
        adaptiveOrderedDeliveryNeedsPressurePreflight(
            plan.adaptiveOrderedDeliveryPolicy,
            this->recoveryState.generatedImageAdmission.underPressure(),
            plan.requestedGeneratedFrames.size()
        );
    const bool nativeFirstOrderedBatch = headroomTightOrderedBatch;
    if (nativeFirstOrderedBatch &&
            !this->diagnosticsState.orderedGeneratedAdmissionPolicyLogged &&
            presentDiagnosticsEnabled()) {
        this->diagnosticsState.orderedGeneratedAdmissionPolicyLogged = true;
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=ordered-generated-admission-policy"
                  << " context=" << this->diagnosticsState.contextId
                  << " requested_min_images="
                  << this->info.requestedMinImageCount
                  << " images=" << this->info.images.size()
                  << " planned=" << plan.requestedGeneratedFrames.size()
                  << " acquire_timeout_ns=0"
                  << " action=native-first\n";
    }
    if (gamescopeHdrTransport || nativeFirstOrderedBatch ||
            adaptivePressureRetry) {
        this->preacquireGeneratedImages(
            invocation, plan, true, !adaptivePressureRetry, 0,
            !adaptivePressureRetry
        );
    }
    size_t scheduledGeneratedFrameCount = plan.generatedImagesPreacquired
        ? plan.admittedGeneratedFrameCount
        : plan.requestedGeneratedFrames.size();
    plan.scheduledGeneratedFrames = scheduleAdmittedGeneratedFrames(
        plan.requestedGeneratedFrames, scheduledGeneratedFrameCount
    );
    bool bypassGeneratedFrames = plan.historyWarmupActive ||
        plan.scheduledGeneratedFrames.empty();

    if (orderedAcquireProbeEligible(orderedAcquireRecoveryProbe,
            plan.historyWarmupActive, plan.requestedGeneratedFrames.size())) {
        // After a genuine native drain, one single-image probe uses the
        // configured acquire contract or one confirmed display period. Failure
        // is terminal for this attempt and returns to backoff instead of
        // leaving recovery permanently probe-pending.
        const uint64_t recoveryAcquireTimeout = boundedOrderedAcquireProbe
            ? orderedRecoveryAcquireTimeout(
                this->gamescopeRefreshHz, plan.configuredAcquireTimeout
            )
            : 0;
        this->preacquireGeneratedImages(
            invocation, plan, false, false, recoveryAcquireTimeout
        );
        if (plan.admittedGeneratedFrameCount == 0) {
            const auto probeFinishedAt = DiagnosticsClock::now();
            const auto miss = this->recoveryState.orderedAcquireRecovery
                .reportNonblockingProbeUnavailable(probeFinishedAt);
            if (miss.quarantined) {
                this->fixedRefreshBudget.reset();
                if (presentDiagnosticsEnabled()) {
                    std::cerr << "MAKO Renderer: present diagnostics: "
                                 "operation=ordered-acquire-quarantine"
                              << " context="
                              << this->diagnosticsState.contextId
                              << " phase=native-drain"
                              << " reason=bounded-recovery-probe-timeout"
                              << " acquire_timeout_ns="
                              << recoveryAcquireTimeout
                              << " acquire_ms="
                              << std::chrono::duration<double, std::milli>(
                                     plan.preacquireDuration
                                 ).count()
                              << " consecutive_failures="
                              << miss.consecutiveFailures
                              << " retry_ms="
                              << std::chrono::duration<double, std::milli>(
                                     miss.retryDelay
                                 ).count()
                              << " bypassed_frames="
                              << miss.bypassedFrames
                              << " recovery_ms="
                              << std::chrono::duration<double, std::milli>(
                                     miss.recoveryDuration
                                 ).count()
                              << " requested_generated="
                              << plan.requestedGeneratedFrames.size()
                              << " admitted_generated=0"
                              << " presented_generated=0"
                              << " frame="
                              << this->frameState.realFrameIndex
                              << " sequence="
                              << this->frameState.sequenceIndex
                              << " action=native-drain\n";
                }
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
            this->instance->scheduleFrames(
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
            if (!preacquiredImagesRequireRetirement(
                    plan.generatedImagesPreacquired,
                    plan.admittedGeneratedFrameCount)) {
                return this->presentNativeFrame(invocation);
            }
            if (invocation.originalPresentDeadline)
                invocation.waitForOutput(*invocation.originalPresentDeadline);
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
    if (sourceDeadlinePacing && !plan.historyWarmupActive &&
            (this->profile.adaptive || plan.scheduledGeneratedFrames.size() ==
                this->configuredFixedGeneratedFrames) &&
            plan.scheduledGeneratedFrames.size() ==
                plan.requestedGeneratedFrames.size()) {
        invocation.pacedOutputs = this->realFramePacer.scheduleOutputs(
            DiagnosticsClock::now(), limiterDeadline,
            this->profile.adaptive
                ? std::min<double>(this->profile.target_fps,
                    this->gamescopeRefreshHz.value_or(this->profile.target_fps))
                : baseFpsCap * this->profile.multiplier,
            plan.scheduledGeneratedFrames.size() + 1,
            this->adaptiveScheduler &&
                this->adaptiveScheduler->snapshot().targetOutputClockActive);
        if (invocation.pacedOutputs)
            invocation.originalPresentDeadline = invocation.pacedOutputs->at(
                invocation.pacedOutputs->count - 1);
    } else {
        this->realFramePacer.resetOutputs();
        // Desktop FIFO: each frame's own count matches the interpolation
        // timestamps its plan already spaced across this source interval.
        if (!plan.historyWarmupActive && plan.scheduledGeneratedFrames.size() ==
                plan.requestedGeneratedFrames.size()) {
            invocation.pacedOutputs = this->sourceIntervalSpacing.schedule(
                DiagnosticsClock::now(), plan.scheduledGeneratedFrames.size() + 1);
            if (invocation.pacedOutputs)
                invocation.originalPresentDeadline = invocation.pacedOutputs->at(
                    invocation.pacedOutputs->count - 1);
        }
    }
    if (bypassGeneratedFrames)
        return this->presentHistoryOnly(invocation, plan);
    this->bridgeOutputBatchSize = plan.scheduledGeneratedFrames.size() + 1;
    return this->presentGeneratedFrames(
        invocation, plan, gamescopeHdrTransport
    );
}
