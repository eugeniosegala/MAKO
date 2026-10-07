/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "profile_update.hpp"
#include "mako-common/configuration/detection.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using namespace mako::layer;

namespace {
    void expect(const bool condition, const std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }

    // Compare through the canonical writer so new native fields participate
    // without another power-specific field list in this test.
    std::string serialized(const ls::GameConf& profile,
            const std::filesystem::path& path) {
        ls::ConfigFile config;
        config.profiles() = {profile};
        config.write(path);
        std::ifstream input(path);
        return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
    }

    ls::GameConf selected(const ls::ConfigFile& config) {
        ls::Identification identity;
        identity.override = "game";
        const auto match = ls::findProfile(config, identity, false);
        expect(match.has_value(), "Power selection lost the matching game");
        return match->second;
    }
}

int main() {
    auto pattern = (std::filesystem::temp_directory_path() /
        "mako-power-update-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const auto* temporary = mkdtemp(buffer.data());
    expect(temporary != nullptr, "Unable to create power-update fixture");
    const std::filesystem::path directory(temporary);
    const auto path = directory / "conf.toml";
    const auto comparison = directory / "comparison.toml";

    ls::GameConf base;
    base.name = "game";
    base.active_in = {"Game.exe"};
    base.frame_generation_provisioned = true;
    base.frame_generation_enabled = true;
    base.adaptive = true;
    base.adaptive_max_multiplier = 3;
    base.adaptive_auto_base_fps_cap = false;
    base.target_fps = 90;
    base.scaling_enabled = true;
    base.scaling_method = ls::ScalingMethod::Mako;
    base.scaling_factor = 1.25F;

    auto handheld = base;
    handheld.target_fps = 60;
    handheld.base_fps_cap = 30;
    auto docked = base;
    docked.target_fps = 144;
    docked.base_fps_cap = 90;
    docked.adaptive_stable_cadence = true;
    docked.gamescope_vrr_mode = ls::GamescopeVrrMode::On;
    base.power_profiles = {handheld, docked};
    ls::ConfigFile file;
    file.profiles() = {base};
    file.write(path);
    auto config = ls::ConfigFile(path);
    config.current_profile = "game";

    for (const auto source : {ls::PowerSource::Unknown,
            ls::PowerSource::Handheld, ls::PowerSource::Docked}) {
        config.power_source = source;
        auto expected = source == ls::PowerSource::Unknown ? base
            : source == ls::PowerSource::Handheld ? handheld : docked;
        expected.power_profiles.clear();
        const auto expectedText = serialized(expected, comparison);
        std::vector<ls::Identification> identities(6);
        identities[0].override = "game";
        identities[1].executable = "/games/Game.exe";
        identities[2].wine_executable = "/games/Game.exe";
        identities[3].process_name = "Game.exe";
        identities[4].fallback = "game";
        identities[5].follow_current_profile = true;
        for (const auto& identity : identities) {
            const auto match = ls::findProfile(config, identity, false);
            expect(match && match->second.power_profiles.empty() &&
                    serialized(match->second, comparison) == expectedText,
                "Every matching path must forward the complete power set and shared identity");
        }
    }

    config.power_source = ls::PowerSource::Handheld;
    auto applied = selected(config);
    config.power_source = ls::PowerSource::Docked;
    auto requested = selected(config);
    auto plan = planProfileUpdate(applied, requested, 3, true, true, true);
    expect(plan.decision.action == ProfileUpdateAction::ApplyLive &&
            plan.appliedProfile.target_fps == 144 &&
            effectiveBaseFpsCap(plan.appliedProfile) == 90 &&
            plan.appliedProfile.adaptive_stable_cadence &&
            plan.appliedProfile.gamescope_vrr_mode == ls::GamescopeVrrMode::On &&
            !plan.decision.processRestartDeferred &&
            !plan.decision.swapchainRecreationDeferred,
        "An AC switch with the same profile name must use the normal live-policy planner");
    applied = plan.appliedProfile;
    expect(planProfileUpdate(applied, requested, 3, true, true, true)
            .decision.action == ProfileUpdateAction::NoRuntimeChange,
        "Unchanged power settings must not repeatedly reset live policy");

    // An inactive Battery edit must survive saving without changing AC.
    config.profiles().front().power_profiles[0].target_fps = 72;
    config.write(path);
    config = ls::ConfigFile(path);
    config.power_source = ls::PowerSource::Docked;
    expect(planProfileUpdate(applied, selected(config), 3, true, true, true)
            .decision.action == ProfileUpdateAction::NoRuntimeChange,
        "Editing inactive Battery settings must not reset the active AC policy");
    config.power_source = ls::PowerSource::Handheld;
    plan = planProfileUpdate(applied, selected(config), 3, true, true, true);
    expect(plan.appliedProfile.target_fps == 72 &&
            plan.appliedProfile.base_fps_cap == 30 &&
            plan.decision.action == ProfileUpdateAction::ApplyLive,
        "Returning to Battery must apply its latest saved live values");
    applied = plan.appliedProfile;

    // Process-static differences must not block unrelated private or policy
    // changes. Root projects these fields before the same context planner.
    auto& ac = config.profiles().front().power_profiles[1];
    ac.gpu = "Other GPU";
    ac.frame_generation_provisioned = false;
    ac.scaling_enabled = false;
    ac.swapchain_image_count_compatibility = true;
    ac.flow_scale = 0.8F;
    ac.performance_mode = true;
    ac.scaling_method = ls::ScalingMethod::Ls1;
    ac.scaling_sharpness = 0.7F;
    config.power_source = ls::PowerSource::Docked;
    requested = selected(config);
    const auto projection = projectProcessStaticProfileForLiveUpdate(
        applied, requested, true, true, false);
    plan = planProfileUpdate(applied, projection.runtimeProfile, 3, true, true, true);
    expect(projection.restartRequired() && plan.decision.frameGenerationPrivateRebuild &&
            plan.decision.spatialScalingLiveRebuild &&
            plan.decision.action == ProfileUpdateAction::ApplyLive &&
            plan.appliedProfile.target_fps == 144 &&
            plan.appliedProfile.gpu == applied.gpu &&
            plan.appliedProfile.scaling_enabled &&
            plan.appliedProfile.frame_generation_provisioned &&
            plan.appliedProfile.flow_scale == applied.flow_scale &&
            plan.appliedProfile.scaling_method == applied.scaling_method,
        "Pending startup changes must retain live policy and normal private-resource handoffs");

    ac.scaling_factor = 1.75F;
    const auto extentProjection = projectProcessStaticProfileForLiveUpdate(
        applied, selected(config), true, true, false);
    plan = planProfileUpdate(applied, extentProjection.runtimeProfile, 3, true, true, true);
    expect(plan.decision.swapchainRecreationDeferred &&
            plan.appliedProfile.scaling_factor == applied.scaling_factor &&
            plan.appliedProfile.target_fps == 144,
        "Extent changes must remain pending while independent policy applies live");
    config.power_source = ls::PowerSource::Handheld;
    const auto reverted = planProfileUpdate(applied, selected(config), 3, true, true, true);
    expect(reverted.decision.action == ProfileUpdateAction::NoRuntimeChange &&
            !reverted.decision.processRestartDeferred &&
            !reverted.decision.swapchainRecreationDeferred,
        "Reverting power must not leave a superseded AC request pending");

    applied.frame_generation_provisioned = false;
    applied.frame_generation_enabled = false;
    requested = handheld;
    requested.target_fps = 100;
    const auto unprovisioned = projectProcessStaticProfileForLiveUpdate(
        applied, requested, false, true, false);
    plan = planProfileUpdate(applied, unprovisioned.runtimeProfile, 0, false);
    expect(unprovisioned.restartRequired() && plan.decision.processRestartDeferred &&
            !plan.appliedProfile.frame_generation_enabled &&
            !plan.appliedProfile.frame_generation_provisioned &&
            plan.appliedProfile.target_fps == 100,
        "A power switch must not invent FG resources omitted at startup");

    // Saving a clone during a stream changes identity through the same matcher
    // and live planner, including power modes and startup-only projections.
    config.profiles() = {base, base};
    auto& clone = config.profiles()[1];
    clone.name = "Future streams";
    for (auto& mode : clone.power_profiles)
        mode.name = clone.name;
    config.current_profile = clone.name;
    config.write(path);
    config = ls::ConfigFile(path);
    config.power_source = ls::PowerSource::Docked;
    ls::Identification streamIdentity;
    streamIdentity.override = "game";
    streamIdentity.follow_current_profile = true;
    auto streamProfile = ls::findProfile(config, streamIdentity, false)->second;
    applied = ls::profileForPowerSource(base, ls::PowerSource::Docked);
    plan = planProfileUpdate(applied, streamProfile, 3, true, true, true);
    expect(plan.appliedProfile.name == "Future streams" &&
            plan.decision.action == ProfileUpdateAction::NoRuntimeChange,
        "Saving identical settings under a new name must update identity without resetting policy");
    applied = plan.appliedProfile;
    config.profiles()[1].power_profiles[1].target_fps = 120;
    config.profiles()[1].gpu = "Other GPU";
    config.profiles()[1].power_profiles[1].gpu = "Other GPU";
    config.write(path);
    config = ls::ConfigFile(path);
    config.power_source = ls::PowerSource::Docked;
    streamProfile = ls::findProfile(config, streamIdentity, false)->second;
    const auto streamProjection = projectProcessStaticProfileForLiveUpdate(
        applied, streamProfile, true, true, false);
    plan = planProfileUpdate(applied, streamProjection.runtimeProfile, 3, true, true, true);
    expect(streamProjection.restartRequired() &&
            plan.appliedProfile.target_fps == 120 && plan.appliedProfile.gpu == applied.gpu &&
            plan.decision.action == ProfileUpdateAction::ApplyLive,
        "Edits to the new stream profile must apply live fields and retain startup boundaries");

    std::filesystem::remove_all(directory);
    std::cout << "power-profile live-update integration tests passed\n";
}
