/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-backend/mako.hpp"

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

    [[nodiscard]] constexpr bool usesHdrModel(
            FrameEncoding encoding, bool reduced = false) noexcept {
        return encoding == FrameEncoding::ScRgbLinear ||
            requiresPqConversion(encoding, reduced);
    }
}
