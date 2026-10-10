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
        }
    }
}
