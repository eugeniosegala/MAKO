/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "swapchain/swapchain.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include <vulkan/vulkan_core.h>

using namespace mako;
using namespace mako::layer;

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
    const auto& requestedSpatialProfile =
        this->runtimeStatusState.requestedProfile;
    const bool scalingRequested = requestedSpatialProfile.scaling_enabled;
    const char* const spatialInactiveReason =
        spatialScalingRuntimeInactiveReason(
            this->spatialScaler.has_value(),
            scalingRequested,
            requestedSpatialProfile.scaling_factor,
            this->info.spatialScalingInactiveReason
        );
    constexpr double factorComparisonTolerance = 0.005;
    const bool displayCeilingOwnsConstraint =
        !requestedSpatialProfile.scaling_supersampling &&
        nonSupersamplingFactorCeiling &&
        requestedSpatialProfile.scaling_factor >
            *nonSupersamplingFactorCeiling + factorComparisonTolerance;
    const bool memoryConstraintApplies = this->spatialScaler &&
        scalingRequested &&
        this->info.spatialScalingMemoryConstrained &&
        requestedSpatialProfile.scaling_factor >
            effectiveSpatialFactor + factorComparisonTolerance &&
        !displayCeilingOwnsConstraint;
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
            spatialInactiveReason
                ? std::optional<std::string>{spatialInactiveReason}
                : std::nullopt,
        .spatialScalingConstraintReason =
            memoryConstraintApplies
            ? std::optional<std::string>{
                spatialScalingInactiveReasonName(
                    SpatialScalingInactiveReason::
                        VariableSurfaceMemoryBudget
                )
            }
            : std::nullopt,
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
