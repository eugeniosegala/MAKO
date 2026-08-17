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
            .adaptive = true,
            .target_fps = 90,
            .adaptive_max_multiplier = 3,
            .adaptive_stable_cadence = false,
            .flow_scale = 1.0F,
            .performance_mode = false,
            .pacing = ls::Pacing::None,
        };
    }
}

int main() {
    const auto current = adaptiveProfile();

    auto next = current;
    next.target_fps = 120;
    auto decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive,
        "Adaptive target changes must apply without rebuilding GPU resources");
    expect(decision.adaptivePolicyChanged,
        "Adaptive target changes must reset scheduler policy");

    next = current;
    next.adaptive_stable_cadence = true;
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive,
        "Stable cadence must be a live policy update");

    next = current;
    next.adaptive_max_multiplier = 4;
    decision = classifyProfileUpdate(current, next, 3, true);
    expect(decision.action == ProfileUpdateAction::ApplyLive,
        "Adaptive 4x must use the capacity already reserved by Adaptive");
    decision = classifyProfileUpdate(current, next, 2, true);
    expect(decision.action == ProfileUpdateAction::DeferUntilSwapchainRecreation,
        "Adaptive capacity growth must be deferred when images are unavailable");

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
    next.name = "Renamed";
    next.active_in = {"renamed-game"};
    expect(classifyProfileUpdate(current, next, 3, true).action ==
            ProfileUpdateAction::NoRuntimeChange,
        "Profile metadata must not disturb a running context");

    auto fixed = current;
    fixed.adaptive = false;
    fixed.multiplier = 2;
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
    expect(fixedGeneratedFrameCount(2, 2) == 1,
        "Fixed 2x must schedule one frame even with two reserved outputs");
    expect(fixedFrameTimestamp(0, 2) == 0.5F,
        "Fixed 2x must retain its midpoint interpolation timestamp");
    expect(fixedGeneratedFrameCount(3, 2) == 2 &&
            fixedFrameTimestamp(0, 3) > 0.33F &&
            fixedFrameTimestamp(0, 3) < 0.34F &&
            fixedFrameTimestamp(1, 3) > 0.66F &&
            fixedFrameTimestamp(1, 3) < 0.67F,
        "Fixed 3x must use two evenly-spaced reserved outputs");
    fixed.multiplier = 4;
    expect(generatedFrameCapacityForProfile(fixed) == 3,
        "Fixed 4x should reserve its larger Fixed capacity");
    expect(fixedGeneratedFrameCount(4, 3) == 3 &&
            fixedFrameTimestamp(0, 4) == 0.25F &&
            fixedFrameTimestamp(1, 4) == 0.5F &&
            fixedFrameTimestamp(2, 4) == 0.75F,
        "Fixed 4x must use three quarter-spaced reserved outputs");

    std::cout << "profile update tests passed\n";
    return 0;
}
