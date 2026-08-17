/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/configuration/config.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace mako::layer {

    enum class ProfileUpdateAction : uint8_t {
        NoRuntimeChange,
        ApplyLive,
        DeferUntilSwapchainRecreation,
    };

    struct ProfileUpdateDecision {
        ProfileUpdateAction action{ProfileUpdateAction::NoRuntimeChange};
        bool frameGenerationChanged{false};
        bool adaptivePolicyChanged{false};
        bool generationModeChanged{false};
        bool fixedMultiplierChanged{false};
    };

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

    [[nodiscard]] inline float fixedFrameTimestamp(
            const size_t generatedFrameIndex, const size_t multiplier) {
        return static_cast<float>(generatedFrameIndex + 1) /
            static_cast<float>(multiplier);
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
        const bool adaptivePolicyChanged = current.adaptive && next.adaptive && (
            current.target_fps != next.target_fps ||
            current.adaptive_max_multiplier != next.adaptive_max_multiplier ||
            current.adaptive_stable_cadence != next.adaptive_stable_cadence
        );

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
                .adaptivePolicyChanged = adaptivePolicyChanged,
                .generationModeChanged = generationModeChanged,
                .fixedMultiplierChanged = fixedMultiplierChanged,
            };
        }

        if (frameGenerationChanged || adaptivePolicyChanged ||
                generationModeChanged || fixedMultiplierChanged) {
            return {
                .action = ProfileUpdateAction::ApplyLive,
                .frameGenerationChanged = frameGenerationChanged,
                .adaptivePolicyChanged = adaptivePolicyChanged,
                .generationModeChanged = generationModeChanged,
                .fixedMultiplierChanged = fixedMultiplierChanged,
            };
        }

        return {};
    }

}
