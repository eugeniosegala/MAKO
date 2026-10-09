/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gamescope_hdr_feedback.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

extern "C" {
    void mako_test_feedback_reset();
    unsigned mako_test_feedback_hdr_atoms();
    unsigned mako_test_feedback_hdr_reads();
    void mako_test_feedback_refresh(unsigned);
}
namespace {
    void expect(bool value, const char* message) {
        if (!value) throw std::runtime_error(message);
    }
}
int main() {
    try {
        setenv("DISPLAY", ":777", 1);
        setenv("SteamAppId", "730", 1);
        unsetenv("UMU_STEAM_GAME_ID");
        unsetenv("SteamGameId");
        for (bool isolated : {false, true}) {
            for (bool disabled : {false, true}) {
                mako_test_feedback_reset();
                mako::layer::GamescopeHdrFeedbackReader reader({
                    .gamescopeWsiDisabled = isolated, .hdrExposureDisabled = disabled,
                });
                auto sample = reader.diagnosticSample();
                expect(sample.gamescopeDetected && sample.refreshHz == 90 &&
                    sample.outputWidth == 1920 && sample.presentation.vrrActive == true,
                    "HDR policy must preserve presentation feedback");
                const bool legacyHdr = !isolated && !disabled;
                expect(sample.active == legacyHdr, "HDR activation source does not match transport policy");
                mako_test_feedback_refresh(144);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                do {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    sample = reader.diagnosticSample();
                } while (sample.refreshHz != 144 && std::chrono::steady_clock::now() < deadline);
                expect(sample.refreshHz == 144, "presentation monitoring must remain live");
                expect((mako_test_feedback_hdr_atoms() != 0) == legacyHdr &&
                    (mako_test_feedback_hdr_reads() != 0) == legacyHdr,
                    "isolated or disabled HDR must perform no HDR atom lookup or property read");
                expect(legacyHdr || (!sample.outputHdrEnabled && !sample.appHdrMetadataPresent),
                    "skipped HDR evidence must not be fabricated");
                expect(legacyHdr || sample.activationSource ==
                    (disabled ? "hdr-exposure-disabled" : "isolated-swapchain-colorspace"),
                    "diagnostic source must distinguish disabled HDR from per-swapchain HDR");
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
