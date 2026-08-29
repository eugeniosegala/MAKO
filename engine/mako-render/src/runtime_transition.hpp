/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

namespace mako::layer {

    enum class PrivateResourceTransitionPhase : uint8_t {
        Idle,
        Debouncing,
        Preparing,
        Draining,
        Failed,
    };

    /// Coordinate last-value-wins replacement of MAKO-owned GPU resources.
    /// The coordinator owns no Vulkan objects and performs no work itself; it
    /// makes the prepare, drain, commit, retry, and cancellation boundaries
    /// explicit so every private-resource domain follows the same lifecycle.
    template<typename Request>
    class PrivateResourceTransition {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        void request(Request requested, const uint64_t stateRevision,
                const std::chrono::milliseconds quietPeriod,
                const TimePoint now = Clock::now()) {
            if (this->pending && this->pending->value == requested) {
                this->pending->value = std::move(requested);
                this->pending->stateRevision = stateRevision;
                return;
            }
            this->pending = Pending{
                .value = std::move(requested),
                .stateRevision = stateRevision,
                .prepareAfter = now + quietPeriod,
            };
            this->currentPhase = PrivateResourceTransitionPhase::Debouncing;
            this->retryAt.reset();
        }

        void cancel() {
            this->pending.reset();
            this->retryAt.reset();
            this->currentPhase = PrivateResourceTransitionPhase::Idle;
        }

        /// Rebuild an already-pending request after another private resource
        /// domain changes one of its construction inputs. The requested value
        /// and revision are preserved, while any prepared resources remain the
        /// caller's responsibility to retire before invoking this method.
        void restartPreparation(const std::chrono::milliseconds quietPeriod,
                const TimePoint now = Clock::now()) {
            if (!this->pending)
                return;
            this->pending->prepareAfter = now + quietPeriod;
            this->retryAt.reset();
            this->currentPhase = PrivateResourceTransitionPhase::Debouncing;
        }

        [[nodiscard]] bool beginPreparation(
                const TimePoint now = Clock::now()) {
            if (!this->pending)
                return false;
            if (this->currentPhase ==
                    PrivateResourceTransitionPhase::Draining ||
                    this->currentPhase ==
                    PrivateResourceTransitionPhase::Preparing) {
                return false;
            }
            if (now < this->pending->prepareAfter ||
                    (this->retryAt && now < *this->retryAt)) {
                return false;
            }
            this->currentPhase = PrivateResourceTransitionPhase::Preparing;
            return true;
        }

        void prepared() {
            if (!this->pending || this->currentPhase !=
                    PrivateResourceTransitionPhase::Preparing) {
                return;
            }
            this->currentPhase = PrivateResourceTransitionPhase::Draining;
            this->retryAt.reset();
        }

        void failed(const std::chrono::milliseconds retryDelay,
                const TimePoint now = Clock::now()) {
            if (!this->pending)
                return;
            this->currentPhase = PrivateResourceTransitionPhase::Failed;
            this->retryAt = now + retryDelay;
        }

        [[nodiscard]] std::optional<uint64_t> committed() {
            if (!this->pending || this->currentPhase !=
                    PrivateResourceTransitionPhase::Draining) {
                return std::nullopt;
            }
            const auto revision = this->pending->stateRevision;
            this->cancel();
            return revision;
        }

        [[nodiscard]] bool pendingRequest() const {
            return this->pending.has_value();
        }

        [[nodiscard]] bool draining() const {
            return this->currentPhase ==
                PrivateResourceTransitionPhase::Draining;
        }

        [[nodiscard]] PrivateResourceTransitionPhase phase() const {
            return this->currentPhase;
        }

        [[nodiscard]] const Request& value() const {
            return this->pending->value;
        }

        [[nodiscard]] uint64_t stateRevision() const {
            return this->pending ? this->pending->stateRevision : 0;
        }

    private:
        struct Pending {
            Request value;
            uint64_t stateRevision{0};
            TimePoint prepareAfter{};
        };

        std::optional<Pending> pending;
        std::optional<TimePoint> retryAt;
        PrivateResourceTransitionPhase currentPhase{
            PrivateResourceTransitionPhase::Idle
        };
    };

    /// Debounce asynchronous compositor feedback before rebuilding private
    /// colour resources. The last confirmed SDR/HDR state remains usable while
    /// a candidate settles; an unknown sample breaks candidate continuity so a
    /// resolver outage cannot turn one stale sample into a live transition.
    class StableBooleanFeedback {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        explicit StableBooleanFeedback(
                const std::chrono::milliseconds settleDelay =
                    std::chrono::milliseconds(750)) :
            settleDelay(settleDelay) {}

        void seed(const std::optional<bool> value) {
            this->confirmed = value;
            this->candidate.reset();
            this->candidateSince.reset();
        }

        [[nodiscard]] std::optional<bool> observe(
                const std::optional<bool> value, const TimePoint now) {
            if (!value) {
                // An unavailable resolver/property sample breaks continuity.
                // Keep the last confirmed state, but require a fresh full
                // settling interval before accepting a later candidate.
                this->candidate.reset();
                this->candidateSince.reset();
                return std::nullopt;
            }

            if (this->confirmed && *this->confirmed == *value) {
                this->candidate.reset();
                this->candidateSince.reset();
                return std::nullopt;
            }

            if (!this->candidate || *this->candidate != *value) {
                this->candidate = value;
                this->candidateSince = now;
                return std::nullopt;
            }

            if (!this->candidateSince ||
                    now - *this->candidateSince < this->settleDelay)
                return std::nullopt;

            this->confirmed = this->candidate;
            this->candidate.reset();
            this->candidateSince.reset();
            return this->confirmed;
        }

        [[nodiscard]] std::optional<bool> value() const {
            return this->confirmed;
        }

    private:
        std::chrono::milliseconds settleDelay;
        std::optional<bool> confirmed;
        std::optional<bool> candidate;
        std::optional<TimePoint> candidateSince;
    };

}
