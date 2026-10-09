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

    void testGamescopeLaunchIdentity() {
        constexpr std::string_view shortcutGameId = "12884902416314531840";
        constexpr uint32_t shortcutAppId = 3000000123u;
        expect(gamescopeSteamGameId("1462040") == 1462040 &&
                gamescopeSteamGameId(shortcutGameId) == shortcutAppId &&
                gamescopeSteamGameId("18446744069448138752") == 0xffffffffu,
            "Steam app and unsigned 64-bit shortcut IDs must resolve without truncation");
        for (const auto invalid : {
                "", "0", "769", "-1", "+42", " 42", "42 ", "42x",
                "18446744073709551616", "18446744073709551615",
                "33554432", "4294967296", "180422180864", "3302863405056",
                "12884902416314531841", "12884902416297754624",
                "12884902416331309056"}) {
            expect(!gamescopeSteamGameId(invalid),
                "malformed, overflowing, reserved, mod or P2P IDs must stay unknown");
        }

        struct Case {
            GamescopeApplicationIdentityHints hints;
            std::optional<uint32_t> expected;
            std::string_view message;
        };
        const Case cases[]{
            {{"1462040", "1462040", "1462040", ""}, 1462040,
                "ordinary Steam launch lost its app identity"},
            {{"42", "77", "99", ""}, 42,
                "ordinary SteamAppId precedence changed"},
            {{"", "1462040", "", ""}, 1462040,
                "compatibility app fallback was lost"},
            {{"0", "1462040", "", ""}, 1462040,
                "zero SteamAppId masked a valid compatibility identity"},
            {{"invalid", "1462040", "", ""}, 1462040,
                "malformed SteamAppId masked a valid compatibility identity"},
            {{"3000000123", "0", shortcutGameId, ""}, shortcutAppId,
                "direct non-Steam app identity was lost"},
            {{"0", "0", shortcutGameId, ""}, shortcutAppId,
                "direct non-Steam shortcut fallback was lost"},
            {{"0", "prefix-hash", "0", shortcutGameId}, shortcutAppId,
                "Heroic UMU-0 launch did not retain Steam's shortcut identity"},
            {{"292030", "prefix-hash", "292030", shortcutGameId}, shortcutAppId,
                "UMU store identity displaced Steam's actual launch identity"},
            {{"0", "prefix-hash", "0", "1462040"}, 1462040,
                "UMU preserved Steam app identity was lost"},
            {{"1462040", "0", "", "invalid"}, 1462040,
                "malformed UMU hint displaced a valid normal Steam launch"},
            {{"", "", "1462040", ""}, 1462040,
                "plain SteamGameId fallback was lost"},
            {{"", "", "", ""}, std::nullopt,
                "desktop launch fabricated a Steam identity"},
            {{"0", "prefix-hash", "0", ""}, std::nullopt,
                "UMU outside Steam fabricated a shortcut identity"},
            {{"769", "0", "42x", "18446744073709551616"}, std::nullopt,
                "invalid launch hints fabricated a Steam identity"},
        };
        for (const auto& test : cases) {
            const auto id = resolveGamescopeApplicationId(test.hints);
            expect(id == test.expected, test.message);
            expect(!classifyGamescopeFocus(id, 77, 77) &&
                    !classifyGamescopeFocus(id, 769, 77),
                "another game's focus became menu evidence for this launch");
            if (!id) {
                expect(!classifyGamescopeFocus(id, 769, 769),
                    "Steam UI without a known launch identity suspended generation");
                continue;
            }

            // Exercise resolution through the existing debounce and return
            // tracker, the shared evidence source for every generation mode.
            GamescopeFocusTracker tracker;
            const auto start = GamescopeFocusFeedback::Clock::time_point{};
            static_cast<void>(tracker.observe(start, classifyGamescopeFocus(id, id, id)));
            expect(tracker.observe(start + 250ms,
                    classifyGamescopeFocus(id, id, id)).gameFocused == true,
                "resolved launch did not establish gameplay focus");
            static_cast<void>(tracker.observe(start + 500ms,
                classifyGamescopeFocus(id, 769, id)));
            expect(tracker.observe(start + 750ms,
                    classifyGamescopeFocus(id, 769, id)).menuOpen(start + 750ms),
                "resolved launch did not suspend on confirmed Steam menu focus");
            expect(tracker.observe(start + 1s,
                    classifyGamescopeFocus(id, 769, 769)).menuOpen(start + 1s),
                "full Steam UI lost an established menu suspension");
            static_cast<void>(tracker.observe(start + 1250ms,
                classifyGamescopeFocus(id, id, id)));
            const auto returned = tracker.observe(start + 1500ms,
                classifyGamescopeFocus(id, id, id));
            expect(returned.gameFocused == true && returned.returnSequence == 1,
                "resolved launch failed to resume exactly once after the menu");
        }
    }

}

int main() {
    testGamescopeLaunchIdentity();
    const auto start = StableBooleanFeedback::TimePoint{};

    expect(gamescopeApplicationId("1462040") == 1462040 &&
            !gamescopeApplicationId("") && !gamescopeApplicationId("0") &&
            !gamescopeApplicationId("769") && !gamescopeApplicationId("12x") &&
            !gamescopeApplicationId("4294967296"),
        "Steam application identity must be complete and representable");
    expect(gamescopeBooleanFeedback(0) == false &&
            gamescopeBooleanFeedback(1) == true &&
            !gamescopeBooleanFeedback(std::nullopt) &&
            !gamescopeBooleanFeedback(2),
        "Gamescope Boolean properties must reject missing or malformed values");
    const GamescopePresentationFeedback unknownPresentation;
    expect(unknownPresentation.fixedRefreshPacingEligible(),
        "missing Gamescope VRR properties changed legacy pacing");
    const GamescopePresentationFeedback requestedVrr{
        .vrrEnabled = true,
        .vrrCapable = true,
        .vrrActive = false,
        .allowTearing = true,
    };
    expect(requestedVrr.variableRefreshRequested() &&
            !requestedVrr.fixedRefreshPacingEligible(),
        "requested capable VRR retained fixed-refresh pacing");
    const GamescopePresentationFeedback unavailableVrr{
        .vrrEnabled = true,
        .vrrCapable = false,
        .vrrActive = false,
    };
    expect(unavailableVrr.fixedRefreshPacingEligible(),
        "an unavailable VRR connector disabled fixed-refresh pacing");
    const GamescopePresentationFeedback activeVrr{
        .vrrEnabled = false,
        .vrrCapable = true,
        .vrrActive = true,
    };
    expect(activeVrr.variableRefreshRequested(),
        "active compositor VRR was overridden by stale preference feedback");
    const auto retainedVrr = mergeGamescopePresentationFeedback(
        requestedVrr,
        GamescopePresentationFeedback{
            .vrrEnabled = std::nullopt,
            .vrrCapable = false,
            .vrrActive = std::nullopt,
            .allowTearing = false,
        }
    );
    expect(retainedVrr.vrrEnabled == true &&
            retainedVrr.vrrCapable == false &&
            retainedVrr.vrrActive == false &&
            retainedVrr.allowTearing == false,
        "missing Gamescope properties erased previously confirmed values");
    expect(classifyGamescopeFocus(42, 42, 42) == true &&
            classifyGamescopeFocus(42, 769, 42) == false &&
            classifyGamescopeFocus(42, 769, 769) == false &&
            !classifyGamescopeFocus(42, 77, 42) &&
            !classifyGamescopeFocus(42, 42, 77) &&
            !classifyGamescopeFocus(42, std::nullopt, 42) &&
            !classifyGamescopeFocus(42, 0, 42) &&
            !classifyGamescopeFocus(std::nullopt, 769, 42),
        "focus must distinguish the game's input, Steam UI, and unknown peers");
    GamescopeFocusTracker focusTracker;
    expect(!focusTracker.observe(start, true).gameFocused,
        "one unconfirmed focus sample was trusted");
    auto focus = focusTracker.observe(start + 250ms, true);
    expect(focus.gameFocused == true && focus.returnSequence == 0,
        "initial foreground observation manufactured a menu return");
    static_cast<void>(focusTracker.observe(start + 500ms, false));
    focus = focusTracker.observe(start + 600ms, true);
    expect(focus.gameFocused == true && focus.returnSequence == 0,
        "brief focus flicker interrupted gameplay");
    static_cast<void>(focusTracker.observe(start + 750ms, false));
    focus = focusTracker.observe(start + 1s, false);
    expect(focus.menuOpen(start + 1s),
        "confirmed Steam focus was not isolated from gameplay recovery");
    static_cast<void>(focusTracker.observe(start + 1250ms, true));
    focus = focusTracker.observe(start + 1500ms, true);
    expect(focus.gameFocused == true && focus.returnSequence == 1 &&
            focus.openedAt == start + 750ms &&
            focus.returnedAt == start + 1500ms &&
            focus.fresh(start + 1500ms) && !focus.fresh(start + 3s),
        "confirmed return was lost or stale focus remained authoritative");
    static_cast<void>(focusTracker.observe(start + 1750ms, false));
    static_cast<void>(focusTracker.observe(start + 2s, false));
    focus = focusTracker.observe(start + 2250ms, std::nullopt);
    expect(!focus.gameFocused && !focus.returnedAt,
        "unknown input focus retained a menu recovery episode");
    static_cast<void>(focusTracker.observe(start + 2500ms, true));
    focus = focusTracker.observe(start + 2750ms, true);
    expect(focus.returnSequence == 1 && !focus.returnedAt,
        "unknown-to-game focus fabricated a close event");
    static_cast<void>(focusTracker.observe(start + 3s, false));
    static_cast<void>(focusTracker.observe(start + 3250ms, false));
    static_cast<void>(focusTracker.observe(start + 6s, true));
    focus = focusTracker.observe(start + 6250ms, true);
    expect(focus.returnSequence == 1 && !focus.returnedAt,
        "a stalled monitor bridged stale open/close evidence");

    auto focusTime = start + 6250ms;
    for (uint64_t sequence = 2; sequence <= 21; ++sequence) {
        focusTime += 250ms;
        static_cast<void>(focusTracker.observe(focusTime, false));
        focusTime += 250ms;
        expect(focusTracker.observe(focusTime, false).menuOpen(focusTime),
            "repeated menu opening failed to suspend gameplay");
        focusTime += 250ms;
        static_cast<void>(focusTracker.observe(focusTime, true));
        focusTime += 250ms;
        focus = focusTracker.observe(focusTime, true);
        expect(focus.returnSequence == sequence &&
                focus.returnedAt == focusTime,
            "repeated menu returns were delayed or lost");
    }
    for (size_t sample = 0; sample < 480; ++sample) {
        focusTime += 250ms;
        focus = focusTracker.observe(focusTime, false);
    }
    expect(focus.menuOpen(focusTime),
        "two minutes in a menu activated gameplay recovery");
    focusTime += 250ms;
    static_cast<void>(focusTracker.observe(focusTime, true));
    focusTime += 250ms;
    focus = focusTracker.observe(focusTime, true);
    expect(focus.returnSequence == 22 && focus.returnedAt == focusTime,
        "a long menu visit lost its immediately available return event");

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
            !isolatedWsi.hdrExposureDisabled,
        "HDR opt-in must preserve WSI isolation");
    for (const char* value : {static_cast<const char*>(nullptr), "", "1", "invalid"})
        expect(resolvePresentationEnvironmentPolicy(value, "1", "1").hdrExposureDisabled,
            "isolated HDR requires the toggle's explicit zero export");
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

    const GamescopeHdrFeedbackSample deckOutputSample{
        .gamescopeDetected = true,
        .gamescopePid = 42,
        .xwaylandServerId = 0,
        .outputWidth = 1280,
        .outputHeight = 800,
    };
    expect(confirmedGamescopePresentationTarget(deckOutputSample) ==
            GamescopePresentationTarget{1280, 800},
        "Gamescope server-zero geometry must become the presentation target");
    auto nestedOutputSample = deckOutputSample;
    nestedOutputSample.xwaylandServerId = 1;
    expect(!confirmedGamescopePresentationTarget(nestedOutputSample),
        "nested Gamescope Xwayland geometry must not become an output target");
    auto incompleteOutputSample = deckOutputSample;
    incompleteOutputSample.outputHeight.reset();
    expect(!confirmedGamescopePresentationTarget(incompleteOutputSample),
        "incomplete Gamescope root geometry must fail closed");

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
