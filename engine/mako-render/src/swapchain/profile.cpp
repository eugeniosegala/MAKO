/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "swapchain/swapchain.hpp"
#include "swapchain/create_policy.hpp"
#include "swapchain/retirement.hpp"
#include "present_diagnostics.hpp"
#include "layer_role.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>

#include <vulkan/vulkan_core.h>

using namespace mako;
using namespace mako::layer;

namespace {
    using DiagnosticsClock = present_diagnostics::Clock;
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

    const size_t schedulerGeneratedFrameCapacity =
        this->profile.adaptive &&
            this->adaptiveOrderedWsiGeneratedCapacityLimit
        ? std::min(
            this->destinationImages.size(),
            *this->adaptiveOrderedWsiGeneratedCapacityLimit
        )
        : this->destinationImages.size();

    this->adaptiveScheduler.emplace(
        AdaptiveSchedulerConfig{
            .targetFps = policy->targetFps,
            .maximumMultiplier = policy->maximumMultiplier,
            .generatedFrameCapacity = schedulerGeneratedFrameCapacity,
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
    const size_t requestedGeneratedFrameCapacity =
        generatedFrameCapacityForActivePolicy(nextProfile);
    const size_t existingSwapchainGeneratedFrameCapacity =
        generatedFrameCapacitySupportedByExistingSwapchain(
            this->info.requestedMinImageCount, this->info.images.size()
        );
    const bool generatedFrameCapacityGrowthFitsExistingSwapchain =
        requestedGeneratedFrameCapacity <= this->destinationImages.size() ||
        requestedGeneratedFrameCapacity <=
            existingSwapchainGeneratedFrameCapacity;
    const bool privateFrameGenerationRebuildAvailable =
        this->instance && resourcesAvailable &&
        this->colorPipeline.generationSupported &&
        generatedFrameCapacityGrowthFitsExistingSwapchain;
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
        if (present_diagnostics::enabled()) {
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
                      << generatedFrameCapacityForProfile(nextProfile)
                      << " action=prepare-private-context\n";
        }
    } else if (ls::effectiveFlowScale(this->profile) ==
                ls::effectiveFlowScale(nextProfile) &&
            ls::effectivePerformanceMode(this->profile) ==
                ls::effectivePerformanceMode(nextProfile) &&
            requestedGeneratedFrameCapacity <=
                this->destinationImages.size()) {
        this->frameGenerationTransition.cancel();
        this->preparedFrameGenerationResources.reset();
    } else if (!generatedFrameCapacityGrowthFitsExistingSwapchain) {
        // A newer request that exceeds the WSI queue invalidates any older
        // debounced private candidate. The requested profile remains pending
        // at Root and will construct at the next game-owned recreation.
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
        if (present_diagnostics::enabled()) {
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
                !present_diagnostics::enabled()) {
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
                  << " available_wsi_generated_capacity="
                  << existingSwapchainGeneratedFrameCapacity
                  << " requested_generated_capacity="
                  << requestedGeneratedFrameCapacity
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
            present_diagnostics::enabled()) {
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
            present_diagnostics::enabled()) {
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
    if (present_diagnostics::enabled()) {
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
    if (present_diagnostics::enabled()) {
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
    if (present_diagnostics::enabled()) {
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
