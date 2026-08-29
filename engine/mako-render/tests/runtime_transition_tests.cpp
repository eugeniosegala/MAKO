/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "runtime_transition.hpp"
#include "gamescope_hdr_feedback.hpp"
#include "presentation_policy.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace mako::layer;
using namespace std::chrono_literals;

namespace {
    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

}

int main() {
    const auto start = StableBooleanFeedback::TimePoint{};

    PrivateResourceTransition<int> resources;
    resources.request(2, 10, 500ms, start);
    expect(resources.pendingRequest() &&
            resources.phase() ==
                PrivateResourceTransitionPhase::Debouncing,
        "a private resource request must enter the debounce phase");
    expect(!resources.beginPreparation(start + 499ms) &&
            resources.beginPreparation(start + 500ms),
        "private resource preparation must respect the quiet period");
    resources.prepared();
    expect(resources.draining(),
        "prepared private resources must wait for old work to drain");
    resources.request(4, 11, 500ms, start + 600ms);
    expect(resources.phase() ==
                PrivateResourceTransitionPhase::Debouncing &&
            resources.value() == 4,
        "a newer request must replace an uncommitted prepared request");
    expect(resources.beginPreparation(start + 1100ms),
        "the replacement request did not become ready");
    resources.failed(1s, start + 1100ms);
    expect(resources.phase() == PrivateResourceTransitionPhase::Failed &&
            !resources.beginPreparation(start + 2099ms) &&
            resources.beginPreparation(start + 2100ms),
        "failed private resource preparation must use bounded retry");
    resources.prepared();
    expect(resources.committed() == 11 && !resources.pendingRequest() &&
            resources.phase() == PrivateResourceTransitionPhase::Idle,
        "a committed private resource request must publish its revision and return idle");
    resources.request(3, 12, 0ms, start + 3s);
    resources.cancel();
    expect(!resources.pendingRequest() &&
            resources.phase() == PrivateResourceTransitionPhase::Idle,
        "cancelling a private resource request must discard all transition state");

    resources.request(7, 23, 0ms, start + 4s);
    expect(resources.beginPreparation(start + 4s),
        "the request used to test restart must prepare immediately");
    resources.prepared();
    resources.restartPreparation(500ms, start + 4001ms);
    expect(resources.phase() ==
                PrivateResourceTransitionPhase::Debouncing &&
            resources.value() == 7 && resources.stateRevision() == 23,
        "restart must preserve the last request and its revision");
    expect(!resources.beginPreparation(start + 4499ms) &&
            resources.beginPreparation(start + 4501ms),
        "restart must establish a fresh quiet period");

    expect(resolvePresentationEnvironmentPolicy("1", "1", nullptr)
            .hdrExposureDisabled,
        "the explicit SDR boundary must override DXVK HDR exposure");
    expect(resolvePresentationEnvironmentPolicy(nullptr, "0", nullptr)
            .hdrExposureDisabled,
        "DXVK_HDR=0 must select the SDR boundary");
    expect(!resolvePresentationEnvironmentPolicy("0", "1", nullptr)
            .hdrExposureDisabled,
        "an explicit HDR test must remain possible");
    expect(!resolvePresentationEnvironmentPolicy(nullptr, nullptr, nullptr)
            .hdrExposureDisabled,
        "an absent DXVK capability signal must not invent an HDR decision");
    const auto isolatedWsi = resolvePresentationEnvironmentPolicy(
        "0", "1", "1"
    );
    expect(isolatedWsi.gamescopeWsiDisabled &&
            isolatedWsi.hdrExposureDisabled,
        "WSI isolation must also close the unavailable HDR bridge");

    expect(gamescopeFeedbackPollInterval(false, false) == 1s,
        "ordinary desktop feedback polling should remain idle");
    expect(gamescopeFeedbackPollInterval(true, false) == 250ms,
        "the Gamescope environment hint should retain responsive polling");
    expect(gamescopeFeedbackPollInterval(false, true) == 250ms,
        "detected Gamescope feedback should retain responsive polling");

    // A live SDR<->HDR resource transition requires 750 ms of continuous
    // feedback. Flapping or resolver outages must not rebuild the pipeline or
    // inherit time accumulated by an earlier candidate.
    StableBooleanFeedback feedback(750ms);
    feedback.seed(false);
    expect(feedback.value() == false, "The initial Gamescope feedback should be seeded");
    expect(!feedback.observe(true, start),
        "A single HDR feedback sample must not change the confirmed state");
    expect(!feedback.observe(false, start + 100ms),
        "A transient HDR feedback sample should be cancelled by the old state");
    expect(!feedback.observe(true, start + 200ms),
        "A new HDR candidate should start a fresh stability window");
    expect(!feedback.observe(true, start + 949ms),
        "HDR feedback must remain pending until the whole stability window passes");
    const auto hdrEnabled = feedback.observe(true, start + 950ms);
    expect(hdrEnabled && *hdrEnabled,
        "Stable Gamescope feedback should confirm active HDR");
    expect(!feedback.observe(true, start + 2s),
        "Repeated confirmed feedback must not produce another transition");

    feedback.seed(false);
    expect(!feedback.observe(true, start + 3s),
        "a new HDR candidate should remain provisional");
    expect(!feedback.observe(std::nullopt, start + 4s),
        "unknown feedback must not alter the confirmed SDR state");
    expect(!feedback.observe(true, start + 5s),
        "feedback after an outage must start a fresh settling window");
    expect(!feedback.observe(true, start + 5749ms),
        "an interrupted candidate settled before a complete fresh window");
    const auto hdrEnabledAfterOutage = feedback.observe(
        true, start + 5750ms
    );
    expect(hdrEnabledAfterOutage && *hdrEnabledAfterOutage,
        "stable feedback did not recover after an interrupted candidate");

    // The application normally runs on a nested Xwayland server, while the HDR
    // feedback atom belongs to server zero of the same Gamescope process. Never
    // borrow another compositor's root display merely because it is visible.
    const GamescopeXwaylandDisplay gameDisplay{
        .display = ":1", .gamescopePid = 42, .serverId = 1,
    };
    const std::vector<GamescopeXwaylandDisplay> displays{
        {.display = ":2", .gamescopePid = 99, .serverId = 0},
        {.display = ":0", .gamescopePid = 42, .serverId = 0},
    };
    expect(selectGamescopeRootDisplay(gameDisplay, displays) == ":0",
        "HDR feedback must resolve Gamescope server zero for the same process");
    expect(!selectGamescopeRootDisplay(
            {.display = ":8"}, displays),
        "an unrelated X11 display must not be guessed as Gamescope root");
    expect(selectGamescopeRootDisplay(
            {.display = ":7", .gamescopePid = 42, .serverId = 0},
            displays) == ":7",
        "a game already on server zero must keep its current display");
    expect(!selectGamescopeRootDisplay(
            {.display = ":1", .gamescopePid = 43, .serverId = 1},
            displays),
        "server zero from another Gamescope process must be rejected");

    // Gamescope starts with app-HDR cached false and can therefore leave its
    // Boolean property absent. Prefer explicit app evidence and accept app HDR
    // metadata as an equivalent positive signal. Output capability is never
    // application intent.
    const auto confirmedHdr = decideGamescopeHdrActivation({
        .appWantsHdr = true,
        .outputHdrEnabled = true,
        .gamescopeDetected = true,
    });
    expect(confirmedHdr.active && *confirmedHdr.active &&
            confirmedHdr.source == "gamescope-app-colorspace",
        "confirmed Gamescope app HDR should be authoritative");

    const auto confirmedSdr = decideGamescopeHdrActivation({
        .appWantsHdr = false,
        .outputHdrEnabled = true,
        .appHdrMetadataPresent = true,
        .gamescopeDetected = true,
    });
    expect(confirmedSdr.active && !*confirmedSdr.active,
        "confirmed SDR must override stale metadata");

    const auto metadataHdr = decideGamescopeHdrActivation({
        .outputHdrEnabled = true,
        .appHdrMetadataPresent = true,
        .gamescopeDetected = true,
    });
    expect(metadataHdr.active && *metadataHdr.active &&
            metadataHdr.source == "gamescope-app-hdr-metadata",
        "valid app HDR metadata should recover an unset Boolean property");

    const auto automaticSdr = decideGamescopeHdrActivation({
        .outputHdrEnabled = true,
        .gamescopeDetected = true,
    });
    expect(!automaticSdr.active,
        "an HDR display alone must not promote an automatic SDR launch");

    const auto blockedHdr = decideGamescopeHdrActivation({
        .appWantsHdr = true,
        .outputHdrEnabled = true,
        .appHdrMetadataPresent = true,
        .hdrExposureDisabled = true,
        .gamescopeDetected = true,
    });
    expect(blockedHdr.active && !*blockedHdr.active &&
            blockedHdr.source == "hdr-exposure-disabled",
        "the SDR compatibility boundary must disable every HDR evidence path");

    const GamescopeHdrFeedbackSample ordinaryGamescopeStartup{
        .active = true,
        .outputHdrEnabled = true,
        .gamescopeDetected = true,
        .status = "confirmed",
        .activationSource = "gamescope-app-colorspace",
    };
    expect(!initialGamescopeHdrActivation(ordinaryGamescopeStartup),
        "ordinary Gamescope app feedback must retain its startup settling guard");

    const GamescopeHdrFeedbackSample outputOnlyStartup{
        .outputHdrEnabled = true,
        .gamescopeDetected = true,
        .status = "feedback-property-unset",
        .activationSource = "unavailable",
    };
    expect(!initialGamescopeHdrActivation(outputOnlyStartup),
        "an HDR-capable output must not initialize the application HDR pipeline");

    const GamescopeHdrFeedbackSample blockedStartup{
        .active = false,
        .gamescopeDetected = true,
        .status = "hdr-exposure-disabled",
        .activationSource = "hdr-exposure-disabled",
    };
    expect(initialGamescopeHdrActivation(blockedStartup) == false,
        "blocked HDR exposure must initialize the proven SDR path immediately");

    std::cout << "runtime transition tests passed\n";
    return 0;
}
