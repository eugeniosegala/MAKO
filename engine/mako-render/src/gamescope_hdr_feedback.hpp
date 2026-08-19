/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mako::layer {

    [[nodiscard]] inline bool environmentFlagEnabled(const char* value) {
        if (!value)
            return false;
        const std::string_view flag(value);
        return flag == "1" || flag == "true" ||
            flag == "yes" || flag == "on";
    }

    /// The plugin's restart-time SDR boundary is authoritative even when
    /// Gamescope or DXVK exposes HDR capability. DXVK_HDR=0 is also
    /// conclusively SDR, while an absent DXVK_HDR alone does not force a mode.
    [[nodiscard]] inline bool hdrExposureDisabledFromEnvironment(
            const char* explicitDisable, const char* dxvkHdr) {
        return environmentFlagEnabled(explicitDisable) ||
            (dxvkHdr && !environmentFlagEnabled(dxvkHdr));
    }

    struct GamescopeHdrFeedbackSample {
        std::optional<bool> active;
        std::optional<uint32_t> refreshHz;
        std::optional<bool> outputHdrEnabled;
        bool appHdrMetadataPresent{false};
        bool gamescopeDetected{false};
        std::optional<uint32_t> gamescopePid;
        std::optional<uint32_t> xwaylandServerId;
        std::string status;
        std::string activationSource;
        std::string display;
        std::string resolverStatus;
        std::string resolverCandidates;
    };

    /// Keep compositor feedback responsive when Gamescope is present, but do
    /// not wake an ordinary desktop Vulkan process four times per second just
    /// because it has an X11 display. The idle probe preserves late discovery
    /// without making non-Gamescope monitoring a hot background loop.
    [[nodiscard]] inline std::chrono::milliseconds
    gamescopeFeedbackPollInterval(
            const bool environmentHint,
            const bool gamescopeDetected) {
        return environmentHint || gamescopeDetected
            ? std::chrono::milliseconds{250}
            : std::chrono::milliseconds{1'000};
    }

    struct GamescopeHdrActivationEvidence {
        std::optional<bool> appWantsHdr;
        std::optional<bool> outputHdrEnabled;
        bool appHdrMetadataPresent{false};
        bool hdrExposureDisabled{false};
        bool gamescopeDetected{false};
    };

    struct GamescopeHdrActivationDecision {
        std::optional<bool> active;
        std::string_view source{"unavailable"};
    };

    /// Resolve application HDR only from application-owned evidence. The
    /// compositor output being HDR-capable is deliberately diagnostic only:
    /// it tells us Gamescope may expose HDR formats, not that this game selected
    /// one. Treating output capability as application intent caused 10-bit SDR
    /// swapchains to enter the HDR pipeline before the game opted into HDR.
    [[nodiscard]] inline GamescopeHdrActivationDecision
    decideGamescopeHdrActivation(
            const GamescopeHdrActivationEvidence& evidence) {
        if (evidence.hdrExposureDisabled)
            return {.active = false, .source = "hdr-exposure-disabled"};
        if (evidence.appWantsHdr)
            return {
                .active = evidence.appWantsHdr,
                .source = "gamescope-app-colorspace",
            };
        if (evidence.appHdrMetadataPresent)
            return {.active = true, .source = "gamescope-app-hdr-metadata"};
        return {};
    }

    /// Decide which feedback can safely select the colour pipeline before the
    /// first swapchain is created. Gamescope's per-application properties can
    /// still describe the previously held commit during process startup, so
    /// Gamescope-managed values retain the normal settling delay. Explicitly
    /// blocked exposure is conclusively SDR; non-Gamescope explicit HDR colour
    /// spaces remain directly classifiable from VkSwapchainCreateInfoKHR.
    [[nodiscard]] inline std::optional<bool>
    initialGamescopeHdrActivation(
            const GamescopeHdrFeedbackSample& sample) {
        if (sample.status == "hdr-exposure-disabled")
            return sample.active;
        if (!sample.gamescopeDetected)
            return sample.active;
        return std::nullopt;
    }

    struct GamescopeXwaylandDisplay {
        std::string display;
        std::optional<uint32_t> gamescopePid;
        std::optional<uint32_t> serverId;
    };

    /// Choose Gamescope's server-zero Xwayland display from displays belonging
    /// to the same compositor process as the game's current display.
    [[nodiscard]] inline std::optional<std::string> selectGamescopeRootDisplay(
            const GamescopeXwaylandDisplay& current,
            const std::vector<GamescopeXwaylandDisplay>& candidates) {
        if (!current.gamescopePid || !current.serverId)
            return std::nullopt;
        if (*current.serverId == 0)
            return current.display;

        const auto candidate = std::ranges::find_if(candidates,
            [&current](const GamescopeXwaylandDisplay& value) {
                return value.gamescopePid == current.gamescopePid &&
                    value.serverId && *value.serverId == 0;
            });
        if (candidate == candidates.end())
            return std::nullopt;
        return candidate->display;
    }

    /// Monitors Gamescope's application-HDR feedback without adding a
    /// mandatory X11 link-time dependency or an X11 round trip to the frame
    /// presentation path.
    class GamescopeHdrFeedbackReader {
    public:
        GamescopeHdrFeedbackReader();
        ~GamescopeHdrFeedbackReader();

        GamescopeHdrFeedbackReader(const GamescopeHdrFeedbackReader&) = delete;
        GamescopeHdrFeedbackReader& operator=(const GamescopeHdrFeedbackReader&) = delete;
        GamescopeHdrFeedbackReader(GamescopeHdrFeedbackReader&&) noexcept;
        GamescopeHdrFeedbackReader& operator=(GamescopeHdrFeedbackReader&&) noexcept;

        /// Return the latest sample collected by the background monitor.
        [[nodiscard]] std::optional<bool> sample() const;

        /// Return the value plus a stable diagnostic reason. This makes an
        /// unavailable feedback path distinguishable from confirmed SDR.
        [[nodiscard]] GamescopeHdrFeedbackSample diagnosticSample() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

}
