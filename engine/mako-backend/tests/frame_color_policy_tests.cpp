/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "helpers/frame_color_policy.hpp"
#include <cstdlib>
#include <iostream>
using namespace mako::backend;
int main() {
    for (const auto encoding : {FrameEncoding::Sdr8, FrameEncoding::SdrHighPrecision,
            FrameEncoding::ScRgbLinear, FrameEncoding::Hdr10Pq, FrameEncoding::Hdr10PqPacked}) {
        const bool pq = encoding == FrameEncoding::Hdr10Pq || encoding == FrameEncoding::Hdr10PqPacked;
        for (const bool requested : {false, true}) {
            if (usesReducedHdrGeneration(encoding, requested) != (pq && requested) ||
                    requiresPqConversion(encoding, requested) != (pq && !requested) ||
                    usesHdrModel(encoding, requested) != (encoding == FrameEncoding::ScRgbLinear || (pq && !requested))) {
                std::cerr << "HDR precision changed an unsupported encoding or retained linear conversion\n";
                return EXIT_FAILURE;
            }
            // Preserve every non-HDR10 path and the exact requested flow when
            // disabled; exercise the default 0.8, half and quarter-flow limits.
            for (const float inverseFlow : {1.0F, 1.25F, 2.0F, 3.0F, 4.0F}) {
                const float actual = generationFlowFactor(encoding, requested, inverseFlow);
                const float expected = pq && requested
                    ? (inverseFlow <= 2.0F ? 2.0F * inverseFlow : 4.0F)
                    : inverseFlow;
                if (actual != expected || actual < inverseFlow || actual > 4.0F) {
                    std::cerr << "HDR motion precision escaped its encoding, toggle or flow bounds\n";
                    return EXIT_FAILURE;
                }
            }
        }
    }
}
