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
            .dynamic_cadence_probe_interval_seconds = 2,
            .flow_scale = 1.0F,
            .performance_mode = false,
            .pacing = ls::Pacing::None,
        };
    }
}

int main() {
    const auto current = adaptiveProfile();
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
                current.adaptive_max_multiplier,
        "Adaptive must retain its configured target when refresh is available");

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
            fixedRecoveryPolicy->dynamicCadenceRecovery,
        "Fixed recovery must follow refresh with its multiplier as a ceiling");
    expect(!generationSchedulerPolicy(next, std::nullopt),
        "Fixed recovery must not borrow Adaptive's hidden target");
    expect(generationSchedulerPolicy(next, 90)->targetFps == 90,
        "Fixed recovery must recalibrate when confirmed refresh changes");
    expect(generationSchedulerPolicy(next, 90)
                ->dynamicCadenceProbeIntervalSeconds == 2,
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
    fasterRecovery.dynamic_cadence_probe_interval_seconds = 1;
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
    expect(decision.action == ProfileUpdateAction::DeferUntilSwapchainRecreation,
        "Adaptive capacity growth must be deferred when images are unavailable");

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
    expect(decision.action == ProfileUpdateAction::DeferUntilSwapchainRecreation,
        "Turning generation on without resources must wait for recreation");

    next = current;
    next.flow_scale = 0.75F;
    expect(classifyProfileUpdate(current, next, 3, true).action ==
            ProfileUpdateAction::DeferUntilSwapchainRecreation,
        "Flow-scale changes alter backend model construction");

    next = current;
    next.performance_mode = true;
    expect(classifyProfileUpdate(current, next, 3, true).action ==
            ProfileUpdateAction::DeferUntilSwapchainRecreation,
        "Performance-mode changes alter backend model construction");

    next = current;
    next.ultra_performance = true;
    expect(ls::effectiveFlowScale(next) ==
            ls::GameConfDefaults::ultraPerformanceFlowScale &&
            ls::effectivePerformanceMode(next),
        "Ultra Performance must force 80% flow and the lighter model");
    expect(classifyProfileUpdate(current, next, 3, true).action ==
            ProfileUpdateAction::DeferUntilSwapchainRecreation,
        "Ultra Performance changes must never apply to a live context");

    auto equivalentUltra = current;
    equivalentUltra.flow_scale = ls::GameConfDefaults::ultraPerformanceFlowScale;
    equivalentUltra.performance_mode = true;
    auto equivalentUltraEnabled = equivalentUltra;
    equivalentUltraEnabled.ultra_performance = true;
    expect(classifyProfileUpdate(
            equivalentUltra, equivalentUltraEnabled, 3, true).action ==
            ProfileUpdateAction::DeferUntilSwapchainRecreation,
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
        "Fixed-to-Adaptive 4x must defer when active capacity is unavailable");
    expect(classifyProfileUpdate(
            fixedWithDormantFourX, adaptiveFourX, 3, true).action ==
            ProfileUpdateAction::ApplyLive,
        "Fixed-to-Adaptive 4x must apply when active capacity is available");
    expect(generatedFrameCapacityForActivePolicy(adaptiveFourX) == 3,
        "Adaptive 4x active capacity was not selected");

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
        "Fixed multiplier growth must defer when shared capacity is unavailable");
    expect(classifyProfileUpdate(fixed, next, 2, true).action ==
            ProfileUpdateAction::ApplyLive,
        "Fixed multiplier changes must apply live within shared capacity");

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
