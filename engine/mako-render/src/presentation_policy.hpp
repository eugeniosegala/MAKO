/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace mako::layer {

    /// SteamOS/Gamescope integration boundary.
    ///
    /// This is not a general Vulkan rule that Linux SDR requires FIFO and HDR
    /// requires MAILBOX. Gamescope's WSI layer runs above MAKO, implements the
    /// application's pacing there, then forwards a MAILBOX lower swapchain.
    /// MAKO runs below that hook and expands one application present into
    /// synthetic present(s) plus the original, so those injected presents do
    /// not pass through Gamescope's upper QueuePresent policy individually.
    ///
    /// The fork's established SDR path therefore owns a private FIFO sequence:
    /// it provides ordering and backpressure for every generated/original
    /// image. HDR-capable Gamescope swapchains retain Gamescope's lower WSI
    /// contract because its format/colour-space normalization and HDR feedback
    /// are part of that bridge. The decision is made once from create-time
    /// capability and must remain stable for the lifetime of the swapchain.
    enum class PresentationTransport {
        OrderedSdr,
        GamescopeHdr,
    };

    /// Gamescope can normalize the colour space before MAKO sees it, so the
    /// HDR-capable create-time format is the stable discriminator while live
    /// application-HDR feedback is still provisional. Live feedback may
    /// rebuild colour resources; it must not change the transport underneath
    /// an already-created VkSwapchainKHR.
    [[nodiscard]] inline PresentationTransport selectPresentationTransport(
            const bool gamescopeManaged,
            const bool hdrCapableSwapchain) {
        return gamescopeManaged && hdrCapableSwapchain
            ? PresentationTransport::GamescopeHdr
            : PresentationTransport::OrderedSdr;
    }

    /// Generated images on the Gamescope HDR bridge are opportunistic: waiting
    /// for one blocks the application's real present and caused deterministic
    /// 7-13 ms stalls at 120 Hz. A zero timeout means "native frame wins", not
    /// a backend failure. Ordered/legacy paths retain their configured ceiling
    /// because their synchronous FIFO contract is intentionally different.
    [[nodiscard]] inline uint64_t generatedImageAcquireTimeout(
            const bool gamescopeManaged,
            const std::optional<uint64_t> configuredTimeout) {
        if (gamescopeManaged)
            return 0;
        return configuredTimeout.value_or(std::numeric_limits<uint64_t>::max());
    }

    struct GeneratedImageAdmissionRecovery {
        bool resumed{false};
        size_t missedAttempts{0};
        size_t bypassedFrames{0};
    };

    /// Tracks temporary generated-swapchain pressure without turning it into
    /// an engine or temporal-history failure. Gamescope admission is always
    /// non-blocking; this state exists only to aggregate diagnostics and report
    /// the eventual recovery.
    class GeneratedImageAdmission {
    public:
        [[nodiscard]] bool underPressure() const {
            return this->pressure;
        }

        /// Record an unavailable image. Returns true for the first miss and
        /// then at power-of-two intervals, allowing diagnostics to remain
        /// useful without synchronously logging every real frame.
        [[nodiscard]] bool reportUnavailable() {
            if (!this->pressure) {
                this->pressure = true;
                this->missedAttempts = 1;
                this->bypassedFrames = 0;
                return true;
            }

            this->missedAttempts++;
            return (this->missedAttempts & (this->missedAttempts - 1)) == 0;
        }

        void reportBypassedFrame() {
            if (this->pressure)
                this->bypassedFrames++;
        }

        [[nodiscard]] GeneratedImageAdmissionRecovery reportAvailable() {
            const GeneratedImageAdmissionRecovery recovery{
                .resumed = this->pressure,
                .missedAttempts = this->missedAttempts,
                .bypassedFrames = this->bypassedFrames,
            };
            this->reset();
            return recovery;
        }

        void reset() {
            this->pressure = false;
            this->missedAttempts = 0;
            this->bypassedFrames = 0;
        }

    private:
        bool pressure{false};
        size_t missedAttempts{0};
        size_t bypassedFrames{0};
    };

    struct PipelineBusyDecision {
        bool diagnostic{false};
        bool requestHistoryWarmup{false};
        size_t consecutiveFrames{0};
        size_t totalBypassedFrames{0};
        std::chrono::steady_clock::duration duration{};
    };

    struct PipelineBusyRecoveryEvent {
        bool resumed{false};
        bool diagnostic{false};
        bool historyWarmupRequested{false};
        size_t bypassedFrames{0};
        size_t totalRecoveries{0};
        std::chrono::steady_clock::duration duration{};
    };

    /// Classifies overlap with previously submitted GPU work separately from
    /// a genuine pipeline stall. A one-frame busy result is normal at high
    /// real-frame rates and must not invalidate temporal history. Only one
    /// uninterrupted busy interval that reaches the sustained threshold asks
    /// the caller to warm history again.
    class PipelineBusyRecovery {
    public:
        using TimePoint = std::chrono::steady_clock::time_point;

        [[nodiscard]] static constexpr auto sustainedThreshold() {
            return std::chrono::milliseconds{250};
        }

        [[nodiscard]] PipelineBusyDecision reportBusy(const TimePoint now) {
            if (!this->startedAt) {
                this->startedAt = now;
                this->bypassedFrames = 0;
                this->historyWarmupRequested = false;
            }

            this->bypassedFrames++;
            this->totalBypassedFrames++;
            const auto duration = now - *this->startedAt;
            const bool requestHistoryWarmup =
                !this->historyWarmupRequested &&
                duration >= sustainedThreshold();
            if (requestHistoryWarmup)
                this->historyWarmupRequested = true;

            const bool powerOfTwo =
                (this->totalBypassedFrames &
                    (this->totalBypassedFrames - 1)) == 0;
            return {
                .diagnostic = powerOfTwo || requestHistoryWarmup,
                .requestHistoryWarmup = requestHistoryWarmup,
                .consecutiveFrames = this->bypassedFrames,
                .totalBypassedFrames = this->totalBypassedFrames,
                .duration = duration,
            };
        }

        [[nodiscard]] PipelineBusyRecoveryEvent reportReady(
                const TimePoint now) {
            if (this->startedAt)
                this->totalRecoveries++;
            const bool diagnostic = this->startedAt &&
                (this->historyWarmupRequested ||
                 (this->totalRecoveries & (this->totalRecoveries - 1)) == 0);
            const PipelineBusyRecoveryEvent event{
                .resumed = this->startedAt.has_value(),
                .diagnostic = diagnostic,
                .historyWarmupRequested = this->historyWarmupRequested,
                .bypassedFrames = this->bypassedFrames,
                .totalRecoveries = this->totalRecoveries,
                .duration = this->startedAt
                    ? now - *this->startedAt
                    : std::chrono::steady_clock::duration{},
            };
            this->clearInterval();
            return event;
        }

        void reset() {
            this->clearInterval();
            this->totalBypassedFrames = 0;
            this->totalRecoveries = 0;
        }

    private:
        void clearInterval() {
            this->startedAt.reset();
            this->bypassedFrames = 0;
            this->historyWarmupRequested = false;
        }

        std::optional<TimePoint> startedAt;
        size_t bypassedFrames{0};
        bool historyWarmupRequested{false};
        size_t totalBypassedFrames{0};
        size_t totalRecoveries{0};
    };

    /// Delivery evaluations tolerate up to five percent pressure over a full
    /// evaluation window. Short windows must still deliver every requested
    /// frame, so a single total miss cannot validate a multiplier.
    [[nodiscard]] inline bool generatedDeliveryHealthy(
            const size_t planned, const size_t delivered) {
        if (planned == 0)
            return true;
        const size_t clampedDelivered = std::min(planned, delivered);
        const size_t toleratedMisses = planned / 20;
        return planned - clampedDelivered <= toleratedMisses;
    }

    /// Deterministically suppress synthetic frames which cannot be scanned out
    /// at the confirmed Gamescope refresh rate. Fixed mode remains at its full
    /// multiplier whenever that output fits the display budget.
    class FixedRefreshBudget {
    public:
        using TimePoint = std::chrono::steady_clock::time_point;

        [[nodiscard]] size_t plan(const TimePoint now,
                const std::optional<uint32_t> refreshHz,
                const size_t maximumGeneratedFrames) {
            if (!refreshHz || *refreshHz == 0 || maximumGeneratedFrames == 0) {
                this->lastRealFrame = now;
                return maximumGeneratedFrames;
            }
            if (!this->lastRealFrame) {
                this->lastRealFrame = now;
                return 0;
            }

            const double rawInterval = std::chrono::duration<double>(
                now - *this->lastRealFrame
            ).count();
            this->lastRealFrame = now;
            if (rawInterval <= 0.0 || rawInterval > 0.25) {
                this->smoothedIntervalSeconds = 0.0;
                this->outputCredit = 0.0;
                return 0;
            }
            if (this->smoothedIntervalSeconds == 0.0)
                this->smoothedIntervalSeconds = rawInterval;
            else
                this->smoothedIntervalSeconds =
                    this->smoothedIntervalSeconds * 0.75 + rawInterval * 0.25;

            const double desiredOutputs = std::clamp(
                this->smoothedIntervalSeconds * static_cast<double>(*refreshHz),
                1.0,
                static_cast<double>(maximumGeneratedFrames + 1)
            );
            this->outputCredit += desiredOutputs;
            const size_t requestedOutputs = std::max<size_t>(
                1, static_cast<size_t>(std::floor(this->outputCredit + 1e-9))
            );
            const size_t generated = std::min(
                requestedOutputs - 1, maximumGeneratedFrames
            );
            this->outputCredit -= static_cast<double>(generated + 1);
            if (this->outputCredit < 0.0)
                this->outputCredit = 0.0;
            if (generated == maximumGeneratedFrames && this->outputCredit >= 1.0)
                this->outputCredit = std::fmod(this->outputCredit, 1.0);
            return generated;
        }

        void reset() {
            this->lastRealFrame.reset();
            this->smoothedIntervalSeconds = 0.0;
            this->outputCredit = 0.0;
        }

    private:
        std::optional<TimePoint> lastRealFrame;
        double smoothedIntervalSeconds{0.0};
        double outputCredit{0.0};
    };

}
