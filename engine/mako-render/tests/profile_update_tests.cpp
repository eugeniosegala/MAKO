/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "profile_update.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace mako::layer;

namespace {
    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    ls::GameConf adaptiveProfile() {
        return {
            .name = "Adaptive",
            .active_in = {"game"},
            .multiplier = 2,
            .frame_generation_enabled = true,
            .base_fps_cap = 0,
            .adaptive = true,
            .adaptive_auto_base_fps_cap = false,
            .target_fps = 90,
            .adaptive_max_multiplier = 3,
            .adaptive_stable_cadence = false,
            .dynamic_cadence_recovery = false,
            .dynamic_cadence_probe_interval_seconds = 2.0F,
            .flow_scale = 1.0F,
            .performance_mode = false,
            .pacing = ls::Pacing::None,
        };
    }
}

int main() {
    const auto current = adaptiveProfile();
    auto staticRequest = current;
    staticRequest.gpu = "1002:744c";
    staticRequest.ultra_performance = true;
    staticRequest.flow_scale = ls::GameConfDefaults::ultraPerformanceFlowScale;
    staticRequest.performance_mode = true;
    staticRequest.target_fps = 120;
    const auto staticProjection = projectProcessStaticProfileForLiveUpdate(
        current, staticRequest, true, current.scaling_enabled
    );
    expect(staticProjection.restartRequired() &&
            staticProjection.gpuSelectionPending &&
            staticProjection.ultraPerformancePending &&
            staticProjection.runtimeProfile.gpu == current.gpu &&
            staticProjection.runtimeProfile.ultra_performance ==
                current.ultra_performance &&
            staticProjection.runtimeProfile.flow_scale == current.flow_scale &&
            staticProjection.runtimeProfile.performance_mode ==
                current.performance_mode &&
            staticProjection.runtimeProfile.target_fps == 120,
        "Process-static projection must retain construction fields and apply live policy");
    auto scalingEngineRequest = current;
    scalingEngineRequest.scaling_enabled = !current.scaling_enabled;
    scalingEngineRequest.scaling_method = ls::ScalingMethod::Native;
    const auto scalingEngineProjection =
        projectProcessStaticProfileForLiveUpdate(
            current, scalingEngineRequest, true, current.scaling_enabled
        );
    expect(scalingEngineProjection.restartRequired() &&
            scalingEngineProjection.scalingEnginePending &&
            scalingEngineProjection.runtimeProfile.scaling_enabled ==
                current.scaling_enabled &&
            scalingEngineProjection.runtimeProfile.scaling_method ==
                ls::ScalingMethod::Native,
        "Scaling Engine provisioning must wait for restart while method selection remains live");
    auto interopRequest = current;
    interopRequest.frame_generation_enabled = true;
    auto scalingOnlyCurrent = current;
    scalingOnlyCurrent.frame_generation_enabled = false;
    const auto interopProjection = projectProcessStaticProfileForLiveUpdate(
        scalingOnlyCurrent, interopRequest, false,
        scalingOnlyCurrent.scaling_enabled
    );
    expect(interopProjection.frameGenerationInteropPending &&
            !interopProjection.runtimeProfile.frame_generation_enabled,
        "Process-static projection must retain scaling-only device interop");
    auto ultraReloadProfile = current;
    ultraReloadProfile.ultra_performance = true;
    expect(privateGenerationResourcesRequired(current),
        "Normal mode must retain resources for live generation enable");
    auto normalDisabled = current;
    normalDisabled.frame_generation_enabled = false;
    expect(privateGenerationResourcesRequired(normalDisabled),
        "Normal live switching lost its retained generation resources");
    ultraReloadProfile.frame_generation_enabled = false;
    expect(!privateGenerationResourcesRequired(ultraReloadProfile),
        "Ultra Performance retained private resources with generation frozen off");

    auto backendProfile = current;
    backendProfile.gpu = "1002:164e";
    auto requestedBackendProfile = current;
    requestedBackendProfile.gpu = "1002:15bf";
    const auto existingBackendProfile = profileForExistingBackend(
        requestedBackendProfile, backendProfile
    );
    expect(existingBackendProfile.gpu == backendProfile.gpu,
        "A swapchain recreation lost the process-wide backend GPU identity");

    const ls::GlobalConf backendGlobal{
        .dll = "/first/Lossless.dll",
        .allow_fp16 = true,
    };
    auto requestedGlobal = backendGlobal;
    requestedGlobal.allow_fp16 = false;
    expect(backendGlobalChangePending(backendGlobal, requestedGlobal),
        "A process-static global change was not retained as pending");
    expect(!backendGlobalChangePending(backendGlobal, backendGlobal) &&
            !backendGlobalChangePending(std::nullopt, requestedGlobal),
        "An applied or not-yet-constructed backend was marked pending");
    expect(backendProfileChangePending(
            backendProfile, requestedBackendProfile),
        "A process-static profile change was not retained as pending");
    expect(!backendProfileChangePending(backendProfile, backendProfile) &&
            !backendProfileChangePending(
                std::nullopt, requestedBackendProfile),
        "An applied or not-yet-constructed profile was marked pending");

    expect(effectiveFrameGenerationEnabled(current, std::nullopt) &&
            effectiveFrameGenerationEnabled(current, 60),
        "An unset refresh threshold must preserve configured frame generation");
    auto refreshGuarded = current;
    refreshGuarded.frame_generation_refresh_threshold = 60;
    expect(effectiveFrameGenerationEnabled(refreshGuarded, std::nullopt),
        "Missing Gamescope refresh feedback must fail open");
    expect(!effectiveFrameGenerationEnabled(refreshGuarded, 40) &&
            !effectiveFrameGenerationEnabled(refreshGuarded, 60) &&
            effectiveFrameGenerationEnabled(refreshGuarded, 61),
        "The display guard must pause at or below its configured threshold");
    refreshGuarded.frame_generation_refresh_threshold = 130;
    expect(!effectiveFrameGenerationEnabled(refreshGuarded, 120) &&
            !effectiveFrameGenerationEnabled(refreshGuarded, 130) &&
            effectiveFrameGenerationEnabled(refreshGuarded, 165),
        "A custom threshold must support high-refresh display transitions");
    auto manuallyDisabledRefreshGuard = refreshGuarded;
    manuallyDisabledRefreshGuard.frame_generation_enabled = false;
    expect(!effectiveFrameGenerationEnabled(
            manuallyDisabledRefreshGuard, std::nullopt),
        "Missing refresh feedback must not override the live off switch");
    auto decision = classifyProfileUpdate(current, refreshGuarded, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive &&
            decision.refreshRateThresholdChanged,
        "Refresh threshold changes must apply live");

    const auto adaptiveSchedulerPolicy = generationSchedulerPolicy(current, 60);
    expect(adaptiveSchedulerPolicy &&
            adaptiveSchedulerPolicy->targetFps == current.target_fps &&
            adaptiveSchedulerPolicy->maximumMultiplier ==
                current.adaptive_max_multiplier &&
            adaptiveSchedulerPolicy->nearTargetNativePreference,
        "Adaptive must retain its configured target when refresh is available");

    auto steadyPacing = current;
    steadyPacing.target_fps = 120;
    steadyPacing.adaptive_auto_base_fps_cap = true;
    steadyPacing.adaptive_stable_cadence = true;
    AdaptiveSchedulerSnapshot acceptedTwoX{
        .phase = AdaptiveSchedulerPhase::StableCadence,
        .generationLimit = 1,
        .validatedGenerationLimit = 1,
        .stableCadenceLimit = 1,
        .stableCadenceEvaluationActive = false,
        .smoothedBaseFps = 60.0,
    };
    expect(smoothCadencePacerHandoffActive(
            steadyPacing, true, false, 120, acceptedTwoX),
        "accepted target-matched Steady 2x did not hand pacing to ordered FIFO");
    expect(!smoothCadencePacerHandoffActive(
            steadyPacing, true, false, 90, acceptedTwoX),
        "target-mismatched refresh incorrectly bypassed the Steady base cap");
    acceptedTwoX.stableCadenceEvaluationActive = true;
    expect(!smoothCadencePacerHandoffActive(
            steadyPacing, true, false, 120, acceptedTwoX),
        "unproven Smooth Cadence evaluation bypassed the Steady base cap");
    acceptedTwoX.stableCadenceEvaluationActive = false;
    expect(!smoothCadencePacerHandoffActive(
            steadyPacing, true, true, 120, acceptedTwoX),
        "ordered-acquire recovery did not restore the Steady base cap");
    expect(!smoothCadencePacerHandoffActive(
            steadyPacing, false, false, 120, acceptedTwoX),
        "non-ordered transport received an ordered-FIFO pacing handoff");
    acceptedTwoX.smoothedBaseFps = 62.0;
    expect(!smoothCadencePacerHandoffActive(
            steadyPacing, true, false, 120, acceptedTwoX),
        "Steady pacing handoff remained active outside its target window");
    acceptedTwoX.smoothedBaseFps = 60.0;
    steadyPacing.adaptive_auto_base_fps_cap = false;
    expect(!smoothCadencePacerHandoffActive(
            steadyPacing, true, false, 120, acceptedTwoX),
        "Fractional Adaptive incorrectly bypassed a nonexistent Steady cap");

    auto next = current;
    next.target_fps = 120;
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive,
        "Adaptive target changes must apply without rebuilding GPU resources");
    expect(decision.generationPolicyChanged,
        "Adaptive target changes must reset scheduler policy");

    next = current;
    next.adaptive_stable_cadence = true;
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive,
        "Stable cadence must be a live policy update");

    next = current;
    next.dynamic_cadence_recovery = true;
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive &&
            decision.generationPolicyChanged,
        "Dynamic cadence recovery must be a live policy update");
    expect(dynamicCadenceRecoveryEnabled(next),
        "An uncapped Adaptive profile must allow dynamic cadence recovery");
    next.base_fps_cap = 30;
    expect(!dynamicCadenceRecoveryEnabled(next),
        "A base FPS cap must suppress native-cadence probes");
    next.base_fps_cap = 0;
    next.adaptive_auto_base_fps_cap = true;
    expect(!dynamicCadenceRecoveryEnabled(next),
        "Adaptive auto-cap must suppress native-cadence probes");
    next.adaptive_auto_base_fps_cap = false;
    next.adaptive = false;
    expect(dynamicCadenceRecoveryEnabled(next),
        "Fixed mode must allow global cadence recovery");
    const auto fixedRecoveryPolicy = generationSchedulerPolicy(next, 60);
    expect(fixedRecoveryPolicy &&
            fixedRecoveryPolicy->targetFps == 60 &&
            fixedRecoveryPolicy->maximumMultiplier == 2 &&
            !fixedRecoveryPolicy->stableCadence &&
            !fixedRecoveryPolicy->nearTargetNativePreference &&
            fixedRecoveryPolicy->dynamicCadenceRecovery,
        "Fixed recovery must follow refresh with its multiplier as a ceiling "
        "without enabling Adaptive-only native preference");
    expect(!generationSchedulerPolicy(next, std::nullopt),
        "Fixed recovery must not borrow Adaptive's hidden target");
    expect(generationSchedulerPolicy(next, 90)->targetFps == 90,
        "Fixed recovery must recalibrate when confirmed refresh changes");
    expect(generationSchedulerPolicy(next, 90)
                ->dynamicCadenceProbeIntervalSeconds == 2.0F,
        "Fixed recovery must retain its configured probe interval");
    expect(!generationSchedulerPolicy(next, 0),
        "Fixed recovery must reject an unsupported refresh target");
    next.multiplier = 5;
    expect(!generationSchedulerPolicy(next, 60),
        "Fixed recovery must fail closed beyond scheduler plan capacity");

    auto fixedWithoutRecovery = current;
    fixedWithoutRecovery.adaptive = false;
    auto fixedWithRecovery = fixedWithoutRecovery;
    fixedWithRecovery.dynamic_cadence_recovery = true;
    decision = classifyProfileUpdate(
        fixedWithoutRecovery, fixedWithRecovery, 3, true
    );
    expect(decision.action == ProfileUpdateAction::ApplyLive &&
            decision.generationPolicyChanged,
        "Fixed recovery must be a live generation-policy change");

    auto fasterRecovery = next;
    fasterRecovery.multiplier = 2;
    fasterRecovery.dynamic_cadence_probe_interval_seconds = 0.25F;
    next.multiplier = 2;
    decision = classifyProfileUpdate(next, fasterRecovery, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive &&
            decision.dynamicCadenceProbeIntervalChanged &&
            !decision.generationPolicyChanged,
        "Probe-interval changes must apply live without resetting policy");

    auto dormantRecoveryInterval = current;
    dormantRecoveryInterval.dynamic_cadence_probe_interval_seconds = 3;
    decision = classifyProfileUpdate(current, dormantRecoveryInterval, 3, true);
    expect(decision.action == ProfileUpdateAction::NoRuntimeChange &&
            !decision.dynamicCadenceProbeIntervalChanged,
        "A dormant probe interval must remain saved without disturbing runtime");

    next = current;
    next.adaptive_max_multiplier = 4;
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive,
        "Adaptive 4x must use the capacity already reserved by Adaptive");
    decision = classifyProfileUpdate(current, next, 2, true);
    expect(decision.action == ProfileUpdateAction::DeferUntilSwapchainRecreation &&
            decision.generatedFrameCapacityExceeded &&
            liveProfileResourceRecreationAvailable(decision, true),
        "Adaptive capacity growth must request recreation when images are unavailable");
    expect(!liveProfileResourceRecreationAvailable(decision, false),
        "Adaptive capacity growth must require retained frame-generation resources");

    next = current;
    next.base_fps_cap = 60;
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive,
        "Base FPS cap changes must not rebuild GPU resources");
    expect(decision.baseFpsCapChanged,
        "Base FPS cap changes must reset presentation timing");

    next = current;
    next.adaptive_auto_base_fps_cap = true;
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive,
        "Adaptive auto-cap changes must apply without rebuilding resources");
    expect(decision.baseFpsCapChanged,
        "Adaptive auto-cap changes must reset presentation timing");
    expect(effectiveBaseFpsCap(next) == 45.0,
        "Adaptive auto-cap did not derive half of the 90 FPS target");

    auto overlappingCaps = next;
    overlappingCaps.base_fps_cap = 30;
    expect(effectiveBaseFpsCap(overlappingCaps) == 45.0,
        "Adaptive auto-cap must take priority over the saved manual cap");

    auto resumedManualCap = overlappingCaps;
    resumedManualCap.adaptive_auto_base_fps_cap = false;
    expect(effectiveBaseFpsCap(resumedManualCap) == 30.0,
        "Disabling Adaptive auto-cap must restore the saved manual cap");
    decision = classifyProfileUpdate(
        overlappingCaps, resumedManualCap, 3, true
    );
    expect(decision.action == ProfileUpdateAction::ApplyLive &&
            decision.baseFpsCapChanged,
        "Restoring the manual cap must reset presentation timing live");

    auto oddTarget = next;
    oddTarget.target_fps = 165;
    expect(effectiveBaseFpsCap(oddTarget) == 82.5,
        "Adaptive auto-cap lost a fractional half-target cadence");

    auto fixedAutoCap = next;
    fixedAutoCap.adaptive = false;
    fixedAutoCap.base_fps_cap = 30;
    expect(effectiveBaseFpsCap(fixedAutoCap) == 30.0,
        "Adaptive auto-cap incorrectly overrode Fixed mode's manual cap");

    next = current;
    next.frame_generation_enabled = false;
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive,
        "Turning generation off must not tear down the context");

    auto disabled = current;
    disabled.frame_generation_enabled = false;
    decision = classifyProfileUpdate(disabled, current, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive,
        "Turning generation back on must reuse retained resources");
    decision = classifyProfileUpdate(disabled, current, 0, false);
    expect(decision.action == ProfileUpdateAction::DeferUntilProcessRestart &&
            decision.processRestartDeferred &&
            !liveProfileResourceRecreationAvailable(decision, false),
        "Turning generation on without resources must wait for process restart");
    auto unavailableEnableWithCap = current;
    unavailableEnableWithCap.base_fps_cap = 48;
    auto unavailableEnablePlan = planProfileUpdate(
        disabled, unavailableEnableWithCap, 0, false
    );
    expect(unavailableEnablePlan.decision.action ==
                ProfileUpdateAction::ApplyLive &&
            unavailableEnablePlan.decision.processRestartDeferred &&
            !unavailableEnablePlan.appliedProfile.frame_generation_enabled &&
            unavailableEnablePlan.appliedProfile.base_fps_cap == 48,
        "An unavailable generation enable blocked an unrelated live cap");

    next = current;
    next.flow_scale = 0.75F;
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action ==
            ProfileUpdateAction::DeferUntilSwapchainRecreation &&
            decision.frameGenerationBackendChanged &&
            decision.swapchainRecreationDeferred,
        "Flow-scale changes alter backend model construction");
    expect(liveProfileResourceRecreationAvailable(decision, true) &&
            !liveProfileResourceRecreationAvailable(decision, false),
        "Flow-scale recreation must require retained frame-generation resources");

    next = current;
    next.performance_mode = true;
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action ==
            ProfileUpdateAction::DeferUntilSwapchainRecreation &&
            decision.frameGenerationBackendChanged &&
            decision.swapchainRecreationDeferred,
        "Performance-mode changes alter backend model construction");

    auto mixedDeferred = current;
    mixedDeferred.flow_scale = 0.75F;
    mixedDeferred.base_fps_cap = 47;
    auto mixedPlan = planProfileUpdate(current, mixedDeferred, 3, true);
    expect(mixedPlan.decision.action == ProfileUpdateAction::ApplyLive &&
            mixedPlan.decision.swapchainRecreationDeferred &&
            mixedPlan.appliedProfile.flow_scale == current.flow_scale &&
            mixedPlan.appliedProfile.base_fps_cap == 47,
        "A pending flow-scale change blocked an unrelated live cap");

    auto laterLiveChange = mixedDeferred;
    laterLiveChange.target_fps = 144;
    mixedPlan = planProfileUpdate(
        mixedPlan.appliedProfile, laterLiveChange, 3, true
    );
    expect(mixedPlan.decision.action == ProfileUpdateAction::ApplyLive &&
            mixedPlan.decision.swapchainRecreationDeferred &&
            mixedPlan.appliedProfile.flow_scale == current.flow_scale &&
            mixedPlan.appliedProfile.target_fps == 144,
        "A retained recreation change blocked a later live target update");

    auto mixedDisabled = mixedDeferred;
    mixedDisabled.frame_generation_enabled = false;
    mixedPlan = planProfileUpdate(current, mixedDisabled, 3, true);
    expect(mixedPlan.decision.action == ProfileUpdateAction::ApplyLive &&
            !mixedPlan.appliedProfile.frame_generation_enabled &&
            mixedPlan.decision.swapchainRecreationDeferred,
        "A pending recreation change blocked the live generation-off switch");
    mixedPlan = planProfileUpdate(
        mixedPlan.appliedProfile, mixedDeferred, 3, true
    );
    expect(mixedPlan.decision.action == ProfileUpdateAction::ApplyLive &&
            mixedPlan.appliedProfile.frame_generation_enabled &&
            mixedPlan.decision.swapchainRecreationDeferred,
        "A pending recreation change blocked live generation re-enable");

    auto revertedDeferred = mixedDeferred;
    revertedDeferred.flow_scale = current.flow_scale;
    mixedPlan = planProfileUpdate(current, revertedDeferred, 3, true);
    expect(mixedPlan.decision.action == ProfileUpdateAction::ApplyLive &&
            !mixedPlan.decision.swapchainRecreationDeferred &&
            !mixedPlan.decision.processRestartDeferred,
        "Reverting a pending recreation field did not clear its deferral");

    auto gpuAndCap = current;
    gpuAndCap.gpu = "1002:164e";
    gpuAndCap.base_fps_cap = 55;
    mixedPlan = planProfileUpdate(current, gpuAndCap, 3, true);
    expect(mixedPlan.decision.action == ProfileUpdateAction::ApplyLive &&
            mixedPlan.decision.processRestartDeferred &&
            mixedPlan.appliedProfile.gpu == current.gpu &&
            mixedPlan.appliedProfile.base_fps_cap == 55,
        "A process-static GPU change blocked an unrelated live cap");

    auto gpuFlowAndCap = gpuAndCap;
    gpuFlowAndCap.flow_scale = 0.75F;
    mixedPlan = planProfileUpdate(current, gpuFlowAndCap, 3, true);
    expect(mixedPlan.decision.action == ProfileUpdateAction::ApplyLive &&
            mixedPlan.decision.swapchainRecreationDeferred &&
            mixedPlan.decision.processRestartDeferred &&
            mixedPlan.appliedProfile.gpu == current.gpu &&
            mixedPlan.appliedProfile.flow_scale == current.flow_scale &&
            mixedPlan.appliedProfile.base_fps_cap == 55,
        "Independent recreation and process deferrals were not both retained");

    next = current;
    next.ultra_performance = true;
    expect(ls::effectiveFlowScale(next) ==
            ls::GameConfDefaults::ultraPerformanceFlowScale &&
            ls::effectivePerformanceMode(next),
        "Ultra Performance must force 75% flow and the lighter model");
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action ==
            ProfileUpdateAction::DeferUntilProcessRestart &&
            decision.processRestartDeferred &&
            !decision.swapchainRecreationDeferred &&
            !liveProfileResourceRecreationAvailable(decision, true),
        "Ultra Performance changes must never apply to a live context");
    next.scaling_enabled = true;
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.spatialScalingChanged &&
            decision.processRestartDeferred &&
            !liveProfileResourceRecreationAvailable(decision, true),
        "A process-static edit must suppress combined forced recreation");

    auto runningUltra = current;
    runningUltra.ultra_performance = true;
    runningUltra.flow_scale = ls::GameConfDefaults::ultraPerformanceFlowScale;
    runningUltra.performance_mode = true;
    auto updatedRunningUltra = runningUltra;
    updatedRunningUltra.target_fps = 120;
    decision = classifyProfileUpdate(runningUltra, updatedRunningUltra, 2, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive &&
            decision.generationPolicyChanged &&
            !decision.processRestartDeferred,
        "Compatible scheduler changes must remain live inside Ultra Performance");
    updatedRunningUltra.adaptive_max_multiplier = 4;
    decision = classifyProfileUpdate(runningUltra, updatedRunningUltra, 2, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive &&
            decision.swapchainRecreationDeferred &&
            decision.generatedFrameCapacityExceeded &&
            liveProfileResourceRecreationAvailable(decision, true),
        "Ultra Performance capacity growth must preserve live policy while requesting recreation");

    next = current;
    next.scaling_enabled = true;
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action ==
            ProfileUpdateAction::DeferUntilProcessRestart &&
            decision.spatialScalingChanged &&
            decision.processRestartDeferred &&
            !decision.swapchainRecreationDeferred,
        "Scaling Engine enablement must require a process restart");
    expect(!liveProfileResourceRecreationAvailable(decision, false),
        "A swapchain recreation must not satisfy Scaling Engine provisioning");

    auto nativeScalingEngine = current;
    nativeScalingEngine.scaling_enabled = true;
    nativeScalingEngine.scaling_method = ls::ScalingMethod::Native;
    decision = classifyProfileUpdate(current, nativeScalingEngine, 3, true);
    expect(decision.action ==
            ProfileUpdateAction::DeferUntilProcessRestart &&
            decision.processRestartDeferred &&
            !decision.swapchainRecreationDeferred &&
            nativeScalingEngine.frame_generation_enabled,
        "Native Scaling Engine activation must retain FG without a redundant live recreation");
    auto dormantScalingChoice = current;
    dormantScalingChoice.scaling_method = ls::ScalingMethod::Ls1;
    dormantScalingChoice.scaling_factor = 2.0F;
    dormantScalingChoice.scaling_sharpness = 0.75F;
    decision = classifyProfileUpdate(current, dormantScalingChoice, 3, true);
    expect(decision.action == ProfileUpdateAction::NoRuntimeChange &&
            !decision.spatialScalingChanged &&
            !decision.swapchainRecreationDeferred,
        "Dormant scaler choices must save without disrupting an engine-off process");
    auto makoScalingEngine = nativeScalingEngine;
    makoScalingEngine.scaling_method = ls::ScalingMethod::Mako;
    decision = classifyProfileUpdate(
        nativeScalingEngine, makoScalingEngine, 3, true
    );
    expect(decision.action ==
            ProfileUpdateAction::DeferUntilSwapchainRecreation &&
            decision.spatialScalingChanged &&
            makoScalingEngine.frame_generation_enabled,
        "Native-to-MAKO method changes must remain live FG-compatible recreations");

    LiveProfileResourceRecreation resourceRecreation;
    const auto resourceChangeStarted =
        LiveProfileResourceRecreation::Clock::time_point{};
    resourceRecreation.update(current, next, 7, resourceChangeStarted);
    expect(resourceRecreation.pending() && resourceRecreation.armed(),
        "A changed resource profile must arm one live recreation signal");
    expect(!resourceRecreation.signalAfterSuccessfulPresent(
            resourceChangeStarted +
                LiveProfileResourceRecreation::quietPeriod -
                std::chrono::milliseconds(1)),
        "A live resource request must debounce an in-progress control edit");
    expect(resourceRecreation.signalAfterSuccessfulPresent(
            resourceChangeStarted +
                LiveProfileResourceRecreation::quietPeriod) == 7,
        "The first successful lower present must receive the request revision");
    expect(resourceRecreation.pending() && !resourceRecreation.armed() &&
            !resourceRecreation.signalAfterSuccessfulPresent(
                resourceChangeStarted +
                    LiveProfileResourceRecreation::quietPeriod),
        "A resource request must emit only one out-of-date signal");
    resourceRecreation.update(current, next, 8,
        resourceChangeStarted + std::chrono::seconds(1));
    expect(!resourceRecreation.armed(),
        "Polling the same pending resource profile must not rearm its signal");
    auto differentScalingRequest = next;
    differentScalingRequest.scaling_method = ls::ScalingMethod::Ls1;
    resourceRecreation.update(current, differentScalingRequest, 9,
        resourceChangeStarted + std::chrono::seconds(1));
    expect(resourceRecreation.armed() &&
            resourceRecreation.signalAfterSuccessfulPresent(
                resourceChangeStarted + std::chrono::seconds(1) +
                    LiveProfileResourceRecreation::quietPeriod) == 9,
        "A different pending scaling profile must receive one fresh signal");
    resourceRecreation.update(current, current, 10,
        resourceChangeStarted + std::chrono::seconds(2));
    expect(!resourceRecreation.pending() && !resourceRecreation.armed(),
        "Returning to the active resource profile must cancel recreation");

    auto frameGenerationBackendRequest = current;
    frameGenerationBackendRequest.flow_scale = 0.75F;
    frameGenerationBackendRequest.performance_mode = true;
    resourceRecreation.update(current, frameGenerationBackendRequest, 11,
        resourceChangeStarted + std::chrono::seconds(3));
    expect(resourceRecreation.armed() &&
            resourceRecreation.signalAfterSuccessfulPresent(
                resourceChangeStarted + std::chrono::seconds(3) +
                    LiveProfileResourceRecreation::quietPeriod) == 11,
        "Flow-scale and model changes must share the live recreation boundary");

    auto capacityRequest = current;
    capacityRequest.adaptive_max_multiplier = 4;
    resourceRecreation.update(current, capacityRequest, 12,
        resourceChangeStarted + std::chrono::seconds(4));
    expect(resourceRecreation.armed() &&
            resourceRecreation.signalAfterSuccessfulPresent(
                resourceChangeStarted + std::chrono::seconds(4) +
                    LiveProfileResourceRecreation::quietPeriod) == 12,
        "Generated-capacity growth must share the live recreation boundary");
    const auto capacityLiveProfile = maskRecreatedProfileResourcesForLiveUpdate(
        current, capacityRequest, true
    );
    expect(capacityLiveProfile.adaptive == current.adaptive &&
            capacityLiveProfile.multiplier == current.multiplier &&
            capacityLiveProfile.adaptive_max_multiplier ==
                current.adaptive_max_multiplier,
        "Pending generated capacity must retain the active scheduling shape");

    auto scalingCurrent = current;
    scalingCurrent.scaling_enabled = true;
    next = scalingCurrent;
    next.scaling_factor = 2.0F;
    decision = classifyProfileUpdate(scalingCurrent, next, 3, true);
    expect(decision.action ==
            ProfileUpdateAction::DeferUntilSwapchainRecreation &&
            decision.spatialScalingChanged,
        "Scaling extent changes must not mutate a live swapchain");

    next = scalingCurrent;
    next.scaling_sharpness = 0.75F;
    decision = classifyProfileUpdate(scalingCurrent, next, 3, true);
    expect(decision.action ==
            ProfileUpdateAction::DeferUntilSwapchainRecreation &&
            decision.spatialScalingChanged,
        "Scaling shader parameters are recreation-bound in the initial pipeline");

    next = current;
    next.scaling_enabled = true;
    next.frame_generation_enabled = false;
    next.flow_scale = 0.75F;
    next.performance_mode = true;
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive &&
            decision.swapchainRecreationDeferred &&
            decision.processRestartDeferred &&
            decision.spatialScalingChanged &&
            decision.frameGenerationBackendChanged &&
            decision.frameGenerationChanged,
        "A combined resource and frame-generation update must apply the safe subset and retain every deferral");

    auto liveProfile = maskRecreatedProfileResourcesForLiveUpdate(current, next);
    expect(!liveProfile.scaling_enabled &&
            liveProfile.scaling_factor == current.scaling_factor &&
            liveProfile.scaling_sharpness == current.scaling_sharpness &&
            liveProfile.flow_scale == current.flow_scale &&
            liveProfile.performance_mode == current.performance_mode &&
            !liveProfile.frame_generation_enabled,
        "A live projection must retain GPU resources while applying the FG switch");
    decision = classifyProfileUpdate(current, liveProfile, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive &&
            decision.frameGenerationChanged &&
            !decision.spatialScalingChanged &&
            !decision.frameGenerationBackendChanged,
        "Masking pending resource changes must expose the independent live FG update");

    auto liveCurrent = liveProfile;
    auto laterRequested = next;
    laterRequested.frame_generation_enabled = true;
    laterRequested.target_fps = 120;
    laterRequested.adaptive_stable_cadence = true;
    decision = classifyProfileUpdate(
        liveCurrent, laterRequested, 3, true
    );
    expect(decision.action == ProfileUpdateAction::ApplyLive &&
            decision.swapchainRecreationDeferred,
        "The requested scaling change must remain pending while later live changes apply");
    liveProfile = maskRecreatedProfileResourcesForLiveUpdate(
        liveCurrent, laterRequested
    );
    decision = classifyProfileUpdate(liveCurrent, liveProfile, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive &&
            decision.frameGenerationChanged &&
            decision.generationPolicyChanged &&
            !decision.spatialScalingChanged &&
            !decision.frameGenerationBackendChanged,
        "Pending resource changes must not block a later live FG/Adaptive update");
    expect(!liveProfile.scaling_enabled &&
            liveProfile.flow_scale == current.flow_scale &&
            liveProfile.performance_mode == current.performance_mode &&
            liveProfile.frame_generation_enabled &&
            liveProfile.target_fps == 120 &&
            liveProfile.adaptive_stable_cadence,
        "Sequential live projections must preserve current resources and newest policy");

    auto equivalentUltra = current;
    equivalentUltra.flow_scale = ls::GameConfDefaults::ultraPerformanceFlowScale;
    equivalentUltra.performance_mode = true;
    auto equivalentUltraEnabled = equivalentUltra;
    equivalentUltraEnabled.ultra_performance = true;
    decision = classifyProfileUpdate(
        equivalentUltra, equivalentUltraEnabled, 3, true
    );
    expect(decision.action == ProfileUpdateAction::DeferUntilProcessRestart &&
            decision.processRestartDeferred &&
            !decision.swapchainRecreationDeferred,
        "Ultra Performance must require restart even when model settings already match");

    next = current;
    next.name = "Renamed";
    next.active_in = {"renamed-game"};
    expect(classifyProfileUpdate(current, next, 3, true).action ==
            ProfileUpdateAction::NoRuntimeChange,
        "Profile metadata must not disturb a running context");

    auto fixed = current;
    fixed.adaptive = false;
    fixed.multiplier = 2;

    auto fixedWithDormantFourX = fixed;
    fixedWithDormantFourX.adaptive_max_multiplier = 4;
    expect(classifyProfileUpdate(
            current, fixedWithDormantFourX, 2, true).action ==
            ProfileUpdateAction::ApplyLive,
        "Adaptive-to-Fixed must ignore dormant Adaptive capacity growth");
    expect(generatedFrameCapacityForActivePolicy(fixedWithDormantFourX) == 1,
        "Fixed 2x active capacity must ignore the dormant Adaptive ceiling");

    auto adaptiveFourX = fixedWithDormantFourX;
    adaptiveFourX.adaptive = true;
    expect(classifyProfileUpdate(
            fixedWithDormantFourX, adaptiveFourX, 2, true).action ==
            ProfileUpdateAction::DeferUntilSwapchainRecreation,
        "Fixed-to-Adaptive 4x must recreate when active capacity is unavailable");
    expect(classifyProfileUpdate(
            fixedWithDormantFourX, adaptiveFourX, 3, true).action ==
            ProfileUpdateAction::ApplyLive,
        "Fixed-to-Adaptive 4x must apply when active capacity is available");
    expect(generatedFrameCapacityForActivePolicy(adaptiveFourX) == 3,
        "Adaptive 4x active capacity was not selected");

    auto adaptiveFourXWithLiveTarget = adaptiveFourX;
    adaptiveFourXWithLiveTarget.target_fps = 120;
    adaptiveFourXWithLiveTarget.base_fps_cap = 49;
    mixedPlan = planProfileUpdate(
        fixedWithDormantFourX, adaptiveFourXWithLiveTarget, 2, true
    );
    expect(mixedPlan.decision.action == ProfileUpdateAction::ApplyLive &&
            mixedPlan.decision.swapchainRecreationDeferred &&
            !mixedPlan.appliedProfile.adaptive &&
            mixedPlan.appliedProfile.target_fps == 120 &&
            mixedPlan.appliedProfile.base_fps_cap == 49,
        "Unavailable Adaptive capacity blocked an unrelated live cap");

    auto unavailableFixedFourX = current;
    unavailableFixedFourX.adaptive = false;
    unavailableFixedFourX.multiplier = 4;
    mixedPlan = planProfileUpdate(current, unavailableFixedFourX, 2, true);
    expect(mixedPlan.decision.action ==
                ProfileUpdateAction::DeferUntilSwapchainRecreation &&
            mixedPlan.decision.swapchainRecreationDeferred &&
            mixedPlan.appliedProfile.adaptive &&
            mixedPlan.appliedProfile.multiplier == 4,
        "A dormant Fixed multiplier was misclassified as active while its mode switch waited");

    next = current;
    expect(classifyProfileUpdate(fixed, next, 1, true).action ==
            ProfileUpdateAction::DeferUntilSwapchainRecreation,
        "Fixed-to-Adaptive must recreate when the fixed context lacks Adaptive capacity");
    expect(classifyProfileUpdate(fixed, next, 2, true).action ==
            ProfileUpdateAction::ApplyLive,
        "Fixed-to-Adaptive must apply live when shared capacity is available");

    next = fixed;
    next.multiplier = 3;
    expect(classifyProfileUpdate(fixed, next, 1, true).action ==
            ProfileUpdateAction::DeferUntilSwapchainRecreation,
        "Fixed multiplier growth must recreate when shared capacity is unavailable");
    expect(classifyProfileUpdate(fixed, next, 2, true).action ==
            ProfileUpdateAction::ApplyLive,
        "Fixed multiplier changes must apply live within shared capacity");

    auto fixedThreeX = fixed;
    fixedThreeX.multiplier = 3;
    const auto fixedThreeToTwo = planProfileUpdate(
        fixedThreeX, fixed, 2, true
    );
    expect(fixedThreeToTwo.decision.action == ProfileUpdateAction::ApplyLive &&
            fixedThreeToTwo.decision.fixedMultiplierChanged &&
            !fixedThreeToTwo.decision.swapchainRecreationDeferred &&
            fixedThreeToTwo.appliedProfile.multiplier == 2,
        "Fixed 3x-to-2x must apply live without recreating the swapchain");

    expect(generatedFrameCapacityForProfile(fixed) == 2,
        "Fixed 2x should reserve the configured Adaptive 3x capacity");
    auto fixedUltra = fixed;
    fixedUltra.ultra_performance = true;
    expect(generatedFrameCapacityForProfile(fixedUltra) == 1,
        "Fixed 2x Ultra Performance must allocate only its active output");
    fixedUltra.adaptive = true;
    expect(generatedFrameCapacityForProfile(fixedUltra) == 2,
        "Adaptive 3x Ultra Performance must allocate only its active ceiling");
    expect(fixedGeneratedFrameCount(2, 2) == 1,
        "Fixed 2x must schedule one frame even with two reserved outputs");
    expect(fixedGeneratedFrameCount(3, 2) == 2,
        "Fixed 3x must use two reserved outputs");
    fixed.multiplier = 4;
    expect(generatedFrameCapacityForProfile(fixed) == 3,
        "Fixed 4x should reserve its larger Fixed capacity");
    expect(fixedGeneratedFrameCount(4, 3) == 3,
        "Fixed 4x must use three reserved outputs");

    std::cout << "profile update tests passed\n";
    return 0;
}
