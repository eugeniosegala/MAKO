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
    void mako_test_feedback_identity(unsigned, unsigned);
    void mako_test_feedback_hdr_output(int);
    void mako_test_feedback_change_pid_on_hdr_read();
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
        for (bool disabled : {false, true}) {
            mako_test_feedback_reset();
            mako::layer::GamescopeHdrFeedbackReader reader({
                .gamescopeWsiDisabled = true, .hdrExposureDisabled = disabled,
            });
            expect(!reader.queryOutputHdrEnabled(0) && !reader.queryOutputHdrEnabled(88),
                "missing or foreign compositor identity must reject capability");
            expect(mako_test_feedback_hdr_reads() == 0, "rejected identity must not read HDR");
            expect(reader.queryOutputHdrEnabled(77) ==
                (disabled ? std::optional<bool>{} : std::optional<bool>{true}),
                "allowed bridge may query same-compositor root output on demand");
            mako_test_feedback_hdr_output(0);
            expect(reader.queryOutputHdrEnabled(77) ==
                (disabled ? std::optional<bool>{} : std::optional<bool>{false}),
                "root SDR must not become HDR");
            for (int invalid : {-1, 2}) {
                mako_test_feedback_hdr_output(invalid);
                expect(!reader.queryOutputHdrEnabled(77), "missing or malformed root capability is unknown");
            }
            mako_test_feedback_hdr_output(1);
            mako_test_feedback_identity(77, 4);
            expect(!reader.queryOutputHdrEnabled(77), "a non-root display cannot supply fallback capability");
            mako_test_feedback_identity(77, 0);
            mako_test_feedback_change_pid_on_hdr_read();
            expect(!reader.queryOutputHdrEnabled(77), "identity change during query must reject capability");
            expect(!disabled || (mako_test_feedback_hdr_atoms() == 0 && mako_test_feedback_hdr_reads() == 0),
                "HDR off must skip all on-demand HDR access");
            expect(!reader.diagnosticSample().active.value_or(true),
                "on-demand capability must never activate the application's HDR pipeline");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
