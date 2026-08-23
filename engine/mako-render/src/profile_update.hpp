/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "adaptive_policy_limits.hpp"
#include "mako-common/configuration/config.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mako::layer {

    enum class ProfileUpdateAction : uint8_t {
        NoRuntimeChange,
        ApplyLive,
        DeferUntilSwapchainRecreation,
    };

    struct ProfileUpdateDecision {
        ProfileUpdateAction action{ProfileUpdateAction::NoRuntimeChange};
        bool frameGenerationChanged{false};
        bool generationPolicyChanged{false};
        bool generationModeChanged{false};
        bool fixedMultiplierChanged{false};
        bool baseFpsCapChanged{false};
    };

    /// Auto-cap aligns the common healthy path with an exact 2x cadence.
    /// Adaptive can still raise its multiplier when the game falls below this
    /// ceiling. Keep the engine's 10 FPS policy floor for unusually low targets.
    [[nodiscard]] inline double effectiveBaseFpsCap(
            const ls::GameConf& profile) {
        if (profile.adaptive && profile.adaptive_auto_base_fps_cap) {
            return std::max(
                adaptiveMinimumBaseFps,
                static_cast<double>(profile.target_fps) / 2.0
            );
        }
        return static_cast<double>(profile.base_fps_cap);
    }

    [[nodiscard]] inline bool dynamicCadenceRecoveryEnabled(
            const ls::GameConf& profile) {
        return profile.dynamic_cadence_recovery &&
            effectiveBaseFpsCap(profile) <= 0.0;
    }

    struct GenerationSchedulerPolicy {
        uint32_t targetFps{0};
        size_t maximumMultiplier{0};
        bool stableCadence{false};
        bool dynamicCadenceRecovery{false};
    };

    /// Adaptive owns an explicit output target. Fixed + Dynamic Cadence
    /// Recovery instead follows a confirmed Gamescope refresh rate and treats
    /// the selected Fixed multiplier as a ceiling. Without that external
    /// signal, Fixed remains exact rather than borrowing a hidden target.
    [[nodiscard]] inline std::optional<GenerationSchedulerPolicy>
    generationSchedulerPolicy(const ls::GameConf& profile,
            const std::optional<uint32_t> gamescopeRefreshHz) {
        if (profile.adaptive) {
            return GenerationSchedulerPolicy{
                .targetFps = profile.target_fps,
                .maximumMultiplier = profile.adaptive_max_multiplier,
                .stableCadence = profile.adaptive_stable_cadence,
                .dynamicCadenceRecovery =
                    dynamicCadenceRecoveryEnabled(profile),
            };
        }
        if (!dynamicCadenceRecoveryEnabled(profile) ||
                !gamescopeRefreshHz ||
                *gamescopeRefreshHz < ls::GameConfLimits::minimumTargetFps ||
                *gamescopeRefreshHz > ls::GameConfLimits::maximumTargetFps ||
                profile.multiplier <
                    ls::GameConfLimits::minimumAdaptiveMaxMultiplier ||
                profile.multiplier >
                    ls::GameConfLimits::maximumAdaptiveMaxMultiplier) {
            return std::nullopt;
        }
        return GenerationSchedulerPolicy{
            .targetFps = *gamescopeRefreshHz,
            .maximumMultiplier = profile.multiplier,
            .stableCadence = false,
            .dynamicCadenceRecovery = true,
        };
    }

    /// Reserve one private output set that can serve both Fixed and Adaptive.
    /// Keeping the game-owned swapchain shape independent from the selected
    /// policy is what makes ordinary Decky mode changes safe to apply live.
    [[nodiscard]] inline size_t generatedFrameCapacityForProfile(
            const ls::GameConf& profile) {
        return std::max(profile.multiplier, profile.adaptive_max_multiplier) - 1;
    }

    [[nodiscard]] inline size_t fixedGeneratedFrameCount(
            const size_t multiplier, const size_t generatedFrameCapacity) {
        return std::min(multiplier - 1, generatedFrameCapacity);
    }

    /// Classify a profile change without touching Vulkan state.
    ///
    /// Only switches that alter CPU-side policy or select the already-created
    /// frame-generation resources are safe during vkQueuePresentKHR. Changes
    /// that alter resource shape or backend model construction are retained by
    /// Root for the next game-owned swapchain creation.
    [[nodiscard]] inline ProfileUpdateDecision classifyProfileUpdate(
            const ls::GameConf& current, const ls::GameConf& next,
            const size_t generatedFrameCapacity,
            const bool frameGenerationResourcesAvailable) {
        const bool frameGenerationChanged =
            current.frame_generation_enabled != next.frame_generation_enabled;
        const bool generationModeChanged = current.adaptive != next.adaptive;
        const bool fixedMultiplierChanged = current.multiplier != next.multiplier;
        const bool baseFpsCapChanged =
            effectiveBaseFpsCap(current) != effectiveBaseFpsCap(next);
        const bool generationPolicyChanged =
            current.dynamic_cadence_recovery !=
                next.dynamic_cadence_recovery ||
            (current.adaptive && next.adaptive && (
                current.target_fps != next.target_fps ||
                current.adaptive_max_multiplier !=
                    next.adaptive_max_multiplier ||
                current.adaptive_stable_cadence !=
                    next.adaptive_stable_cadence
            ));

        const bool backendConstructionChanged =
            current.gpu != next.gpu ||
            current.flow_scale != next.flow_scale ||
            current.performance_mode != next.performance_mode;
        const bool presentationShapeChanged = current.pacing != next.pacing;
        const bool generatedCapacityExceeded =
            generatedFrameCapacityForProfile(next) > generatedFrameCapacity;
        const bool resourcesNeeded = !current.frame_generation_enabled &&
            next.frame_generation_enabled &&
            !frameGenerationResourcesAvailable;

        if (backendConstructionChanged || presentationShapeChanged ||
                generatedCapacityExceeded || resourcesNeeded) {
            return {
                .action = ProfileUpdateAction::DeferUntilSwapchainRecreation,
                .frameGenerationChanged = frameGenerationChanged,
                .generationPolicyChanged = generationPolicyChanged,
                .generationModeChanged = generationModeChanged,
                .fixedMultiplierChanged = fixedMultiplierChanged,
                .baseFpsCapChanged = baseFpsCapChanged,
            };
        }

        if (frameGenerationChanged || generationPolicyChanged ||
                generationModeChanged || fixedMultiplierChanged ||
                baseFpsCapChanged) {
            return {
                .action = ProfileUpdateAction::ApplyLive,
                .frameGenerationChanged = frameGenerationChanged,
                .generationPolicyChanged = generationPolicyChanged,
                .generationModeChanged = generationModeChanged,
                .fixedMultiplierChanged = fixedMultiplierChanged,
                .baseFpsCapChanged = baseFpsCapChanged,
            };
        }

        return {};
    }

}
