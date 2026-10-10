/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-backend/mako.hpp"

#include <algorithm>

namespace mako::backend {
    /// Approximate interpolation in bounded PQ code values. This is an
    /// explicit quality tradeoff, never a reinterpretation of linear scRGB.
    [[nodiscard]] constexpr bool usesReducedHdrGeneration(
            FrameEncoding encoding, bool requested) noexcept {
        return requested && (encoding == FrameEncoding::Hdr10Pq ||
            encoding == FrameEncoding::Hdr10PqPacked);
    }

    [[nodiscard]] constexpr bool requiresPqConversion(
            FrameEncoding encoding, bool reduced = false) noexcept {
        return !usesReducedHdrGeneration(encoding, reduced) &&
            (encoding == FrameEncoding::Hdr10Pq ||
             encoding == FrameEncoding::Hdr10PqPacked);
    }

    /// Halve motion-analysis dimensions in approximate HDR10 mode, bounded
    /// by the supported quarter-resolution flow floor. Reconstruction still
    /// samples full-resolution source images and writes full-resolution HDR.
    /// The input is the inverse of the user's Flow Scale, after presets.
    [[nodiscard]] constexpr float generationFlowFactor(
            FrameEncoding encoding, bool reduced, float inverseFlow) noexcept {
        return usesReducedHdrGeneration(encoding, reduced)
            ? std::min(inverseFlow * 2.0F, 4.0F)
            : inverseFlow;
    }

    [[nodiscard]] constexpr bool usesHdrModel(
            FrameEncoding encoding, bool reduced = false) noexcept {
        return encoding == FrameEncoding::ScRgbLinear ||
            requiresPqConversion(encoding, reduced);
    }
}
